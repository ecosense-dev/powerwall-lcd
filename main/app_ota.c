#include "app_ota.h"

#include <stdlib.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ota";

const char *app_ota_version(void)
{
    const esp_app_desc_t *d = esp_app_get_description();
    return d && d->version[0] ? d->version : "?";
}

static void *memmem_l(const void *h, size_t hl, const void *n, size_t nl)
{
    if (!h || !n || nl == 0 || hl < nl) {
        return NULL;
    }
    const uint8_t *p = h;
    for (size_t i = 0; i + nl <= hl; i++) {
        if (memcmp(p + i, n, nl) == 0) {
            return (void *)(p + i);
        }
    }
    return NULL;
}

static void extract_boundary(const char *ctype, char *out, size_t out_len)
{
    out[0] = '\0';
    if (!ctype) {
        return;
    }
    const char *b = strstr(ctype, "boundary=");
    if (!b) {
        return;
    }
    b += 9;
    if (*b == '"' || *b == '\'') {
        b++;
    }
    strlcpy(out, b, out_len);
    char *cut = strpbrk(out, " \";\r\n");
    if (cut) {
        *cut = '\0';
    }
}

static esp_err_t send_txt(httpd_req_t *req, const char *msg, bool err)
{
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    if (err) {
        httpd_resp_set_status(req, "400 Bad Request");
    }
    return httpd_resp_sendstr(req, msg);
}

esp_err_t app_ota_httpd_post(httpd_req_t *req)
{
    if (req->content_len <= 0 || req->content_len > 3 * 1024 * 1024) {
        return send_txt(req, "Firmware assente o troppo grande (max 3 MB).\n", true);
    }

    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        return send_txt(req, "Nessuna partizione OTA.\n", true);
    }

    char ctype[128] = {0};
    httpd_req_get_hdr_value_str(req, "Content-Type", ctype, sizeof(ctype));
    char boundary[72] = {0};
    extract_boundary(ctype, boundary, sizeof(boundary));
    bool multipart = boundary[0] != '\0';

    char endmark[80];
    size_t endlen = 0;
    if (multipart) {
        snprintf(endmark, sizeof(endmark), "\r\n--%s", boundary);
        endlen = strlen(endmark);
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(part, OTA_WITH_SEQUENTIAL_WRITES, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_begin: %s", esp_err_to_name(err));
        return send_txt(req, "Impossibile avviare OTA.\n", true);
    }

    char buf[1024];
    int remaining = req->content_len;
    bool in_body = !multipart;
    char head[1024];
    size_t head_n = 0;
    char tail[160];
    size_t tail_n = 0;
    size_t written = 0;
    bool failed = false;

    while (remaining > 0 && !failed) {
        int want = remaining > (int)sizeof(buf) ? (int)sizeof(buf) : remaining;
        int n = httpd_req_recv(req, buf, want);
        if (n == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (n <= 0) {
            failed = true;
            break;
        }
        remaining -= n;

        const char *chunk = buf;
        size_t clen = (size_t)n;

        if (!in_body) {
            if (head_n + clen > sizeof(head)) {
                failed = true;
                break;
            }
            memcpy(head + head_n, chunk, clen);
            head_n += clen;
            char *sep = memmem_l(head, head_n, "\r\n\r\n", 4);
            if (!sep) {
                continue;
            }
            in_body = true;
            size_t skip = (size_t)(sep + 4 - head);
            chunk = head + skip;
            clen = head_n - skip;
            if (clen == 0) {
                continue;
            }
        }

        if (multipart) {
            if (tail_n + clen > sizeof(tail)) {
                size_t flush = tail_n + clen - (sizeof(tail) - 1);
                if (flush > tail_n) {
                    err = esp_ota_write(handle, tail, tail_n);
                    written += tail_n;
                    tail_n = 0;
                    if (err != ESP_OK) {
                        failed = true;
                        break;
                    }
                    size_t keep = sizeof(tail) - 1;
                    if (clen > keep) {
                        err = esp_ota_write(handle, chunk, clen - keep);
                        written += clen - keep;
                        if (err != ESP_OK) {
                            failed = true;
                            break;
                        }
                        memcpy(tail, chunk + (clen - keep), keep);
                        tail_n = keep;
                    } else {
                        memcpy(tail, chunk, clen);
                        tail_n = clen;
                    }
                } else {
                    err = esp_ota_write(handle, tail, flush);
                    written += flush;
                    if (err != ESP_OK) {
                        failed = true;
                        break;
                    }
                    memmove(tail, tail + flush, tail_n - flush);
                    tail_n -= flush;
                    memcpy(tail + tail_n, chunk, clen);
                    tail_n += clen;
                }
            } else {
                memcpy(tail + tail_n, chunk, clen);
                tail_n += clen;
            }
        } else {
            err = esp_ota_write(handle, chunk, clen);
            written += clen;
            if (err != ESP_OK) {
                failed = true;
                break;
            }
        }
    }

    if (!failed && multipart && tail_n) {
        char *cut = endlen ? memmem_l(tail, tail_n, endmark, endlen) : NULL;
        size_t use = cut ? (size_t)(cut - tail) : tail_n;
        if (use) {
            err = esp_ota_write(handle, tail, use);
            written += use;
            if (err != ESP_OK) {
                failed = true;
            }
        }
    }

    if (failed || written < 64 * 1024) {
        esp_ota_abort(handle);
        return send_txt(req, "Upload OTA fallito o file troppo piccolo.\n", true);
    }

    err = esp_ota_end(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ota_end: %s", esp_err_to_name(err));
        return send_txt(req, "Firmware non valido (firma/immagine).\n", true);
    }
    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        return send_txt(req, "Impossibile selezionare lo slot OTA.\n", true);
    }

    ESP_LOGI(TAG, "OTA ok %u byte -> %s, reboot", (unsigned)written, part->label);
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_sendstr(req,
                       "<!DOCTYPE html><html><body style=\"background:#000;color:#f5f5f5;font-family:sans-serif;padding:24px\">"
                       "<h1>Firmware caricato</h1><p>Riavvio in corso…</p></body></html>");
    vTaskDelay(pdMS_TO_TICKS(400));
    esp_restart();
    return ESP_OK;
}
