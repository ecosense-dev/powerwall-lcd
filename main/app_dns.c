#include "app_dns.h"

#include <string.h>

#include <unistd.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "dns";
static int s_sock = -1;
static TaskHandle_t s_task;

/* Reply to every A query with 192.168.4.1 so phones open the captive portal. */
static void dns_task(void *arg)
{
    (void)arg;
    uint8_t buf[512];
    while (s_sock >= 0) {
        struct sockaddr_in from = {0};
        socklen_t fromlen = sizeof(from);
        int len = recvfrom(s_sock, buf, sizeof(buf), 0, (struct sockaddr *)&from, &fromlen);
        if (len < 12) {
            continue;
        }
        /* QR=1 response, RA=1, copy QNAME, append A record pointing at AP IP */
        if (len + 16 > (int)sizeof(buf)) {
            continue;
        }
        buf[2] = 0x81;
        buf[3] = 0x80;
        buf[6] = 0;
        buf[7] = 1; /* ANCOUNT = 1 */
        int q = 12;
        while (q < len && buf[q] != 0) {
            q += buf[q] + 1;
        }
        q += 5; /* zero + type + class */
        if (q > len) {
            continue;
        }
        int a = q;
        buf[a++] = 0xC0;
        buf[a++] = 0x0C;
        buf[a++] = 0x00;
        buf[a++] = 0x01; /* A */
        buf[a++] = 0x00;
        buf[a++] = 0x01; /* IN */
        buf[a++] = 0x00;
        buf[a++] = 0x00;
        buf[a++] = 0x00;
        buf[a++] = 0x1E; /* TTL */
        buf[a++] = 0x00;
        buf[a++] = 0x04;
        buf[a++] = 192;
        buf[a++] = 168;
        buf[a++] = 4;
        buf[a++] = 1;
        sendto(s_sock, buf, a, 0, (struct sockaddr *)&from, fromlen);
    }
    vTaskDelete(NULL);
}

esp_err_t app_dns_start(void)
{
    if (s_sock >= 0) {
        return ESP_OK;
    }
    s_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_sock < 0) {
        return ESP_FAIL;
    }
    int yes = 1;
    setsockopt(s_sock, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(53),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(s_sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(s_sock);
        s_sock = -1;
        ESP_LOGE(TAG, "bind :53 failed");
        return ESP_FAIL;
    }
    xTaskCreate(dns_task, "dns", 4096, NULL, 3, &s_task);
    ESP_LOGI(TAG, "captive DNS on :53");
    return ESP_OK;
}

void app_dns_stop(void)
{
    if (s_sock >= 0) {
        int fd = s_sock;
        s_sock = -1;
        shutdown(fd, SHUT_RDWR);
        close(fd);
    }
}
