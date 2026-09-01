# Powerwall dashboard (POC)

Firmware ESP-IDF + LVGL 8 per [Waveshare ESP32-S3-Touch-LCD-7](https://docs.waveshare.com/ESP32-S3-Touch-LCD-7): monitoraggio live e storico di un impianto fotovoltaico Tesla Powerwall 3 tramite [MyTeslaMate Fleet API](https://www.myteslamate.com/api/).

## Cosa fa

- Schermata **Live**: flusso energetico (Solare, Casa, Powerwall, Rete) con kW e stato di carica
- Schermata **Energia**: grafici giorno / settimana / mese e mix “Usato da”
- Schermata **Impostazioni** (e wizard al primo avvio): WiFi con tastiera touch, token API, host, site id, coordinate GPS
- In alto a destra sulla Live: **meteo** (Open-Meteo) al posto dell’IP; l’IP resta in Impostazioni
- Pagina web sulla LAN (e sull’access point di setup) per WiFi e token
- Se non c’è internet, parte da solo l’AP **Powerwall-LCD** (password `powerwall`) su `http://192.168.4.1/`
- Persistenza in **NVS** (niente reflash se cambiano WiFi o token)
- Console seriale per incollare il token dal Mac
- **4G LTE** (SIMCom A7670E) in fallback se la WiFi 2.4 GHz non c’è: PPP via `esp_modem`, APN in Impostazioni / pagina web / `apn`
- Funzioni opzionali **spente** se manca il parametro: niente meteo senza GPS, niente 4G senza APN, niente Tesla senza token
- **OTA**: carica un `.bin` da `http://<IP>/` (PIN), due slot da 2,5 MB sulla flash 8 MB

## Toolchain

Serve ESP-IDF **v5.5.x** (l’adapter LVGL Waveshare richiede IDF >= 5.5), target `esp32s3`. Su questo Mac è in `~/esp/esp-idf`.

```bash
export PATH="/opt/homebrew/bin:/usr/bin:/bin"
. ~/esp/esp-idf/export.sh
cd /path/to/this/repo
idf.py -p /dev/cu.usbmodem5C941570181 build flash monitor
```

Uscita dal monitor: `Ctrl+]`.

Flash dal Type-C **UART / UART1** (non **USB**, che di default è in modalità CAN), dip-switch su **UART1**. Su macOS usa `--before default_reset` (è il default di `idf.py flash`): `--before no_reset` fa fallire il sync perché aprire la porta CDC resetta il chip.

Se compare `No serial data received`: dip-switch su UART1, nessun altro programma sulla porta, poi di nuovo `idf.py flash`. Premi **RESET** se lo schermo resta nero.

## Primo avvio

1. Se il pannello non entra in rete, sul display compare **Powerwall-LCD**. Dal telefono collegati a quella rete (password `powerwall`) e apri `http://192.168.4.1/` (spesso si apre da sola).
2. Scegli la WiFi **2.4 GHz** di casa, inserisci la password, **Connetti WiFi**. L’ESP32-S3 non vede le reti 5 GHz.
3. Quando è in rete, apri `http://<IP>/` dalla WiFi di casa (l’IP è in **Impostazioni**) e incolla il token MyTeslaMate. Inserisci anche **latitudine/longitudine**: in alto a destra sulla Live compare il meteo.
4. In alternativa: wizard sul LCD, oppure da seriale:

```
pw> token <incolla-qui>
pw> show
pw> test
```

Altri comandi: `wifi <ssid> [password]`, `host <url>`, `site <energy_site_id>`, `gps <lat> <lon>`, `apn <nome> [pin_sim]`.

Default:

- host `https://api.myteslamate.com`
- site id `1689571507463295` (Sergio Molinaro)

Il token **non** va nel repository.

## OTA

Dopo il primo flash da USB, gli aggiornamenti si fanno dalla pagina web (sblocco PIN): sezione **Aggiornamento firmware**, file `build/powerwall_dashboard.bin`. Il pannello riavvia da solo sullo slot nuovo.

```bash
. ~/esp/esp-idf/export.sh
idf.py build
# poi da browser: http://<IP>/  →  Carica e riavvia
```

## Hardware

- USB UART: `/dev/cu.usbmodem5C941570181`
- LCD RGB 800×480, touch GT911, expander CH422G, PSRAM octal

### A7670E (4G)

La console di debug resta sul Type-C **UART** (GPIO43/44). Il modem usa **UART1** sui pin MCU dell’RS485, così non serve spostare il dip-switch.

Cablaggio TTL 3.3 V (non collegare TX/RX del modulo ai morsetti RS485 **A/B**, sono differenziali):

| A7670E | ESP32-S3-Touch-LCD-7 |
|--------|----------------------|
| TXD    | GPIO16 (RS485_RXD lato MCU) |
| RXD    | GPIO15 (RS485_TXD lato MCU) |
| GND    | GND |
| VBAT   | 3.7–4.2 V / 5 V del HAT, picchi ~2 A **non** dall’ESP |

Il firmware **non** impulsi PWRKEY (il HAT di solito è già acceso). Senza APN il 4G non viene toccato. Se l’AT non risponde, riprova dopo qualche minuto invece di riavviare.

In **Impostazioni** (o `http://IP/`): APN (es. `internet`, `ibox.tim.it`, `web.omnitel.it`, `iliad`) e PIN SIM se serve. Il supervisor prova prima la WiFi; se fallisce e l’APN è impostato, avvia il PPP. Quando la WiFi torna, il 4G viene staccato. L’AP `Powerwall-LCD` resta disponibile per la configurazione locale anche in 4G.
