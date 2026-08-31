# Powerwall dashboard (POC)

Firmware ESP-IDF + LVGL 8 per [Waveshare ESP32-S3-Touch-LCD-7](https://docs.waveshare.com/ESP32-S3-Touch-LCD-7): monitoraggio live e storico di un impianto fotovoltaico Tesla Powerwall 3 tramite [MyTeslaMate Fleet API](https://www.myteslamate.com/api/).

## Cosa fa

- Schermata **Live**: flusso energetico (Solare, Casa, Powerwall, Rete) con kW e stato di carica
- Schermata **Energia**: grafico giornaliero e mix “Usato da”
- Schermata **Impostazioni** (e wizard al primo avvio): WiFi con tastiera touch, token API, host, site id
- Pagina web sulla LAN (e sull’access point di setup) per WiFi e token
- Se non c’è internet, parte da solo l’AP **Powerwall-LCD** (password `powerwall`) su `http://192.168.4.1/`
- Persistenza in **NVS** (niente reflash se cambiano WiFi o token)
- Console seriale per incollare il token dal Mac

Il 4G (SIMCOM A7670) non è incluso in questa POC.

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
3. Quando è in rete, in alto a destra compare l’**IP**. Dalla WiFi di casa apri `http://<IP>/` e incolla il token MyTeslaMate.
4. In alternativa: wizard sul LCD, oppure da seriale:

```
pw> token <incolla-qui>
pw> show
pw> test
```

Altri comandi: `wifi <ssid> [password]`, `host <url>`, `site <energy_site_id>`.

Default:

- host `https://api.myteslamate.com`
- site id `1689571507463295` (Sergio Molinaro)

Il token **non** va nel repository.

## Hardware

- USB UART: `/dev/cu.usbmodem5C941570181`
- LCD RGB 800×480, touch GT911, expander CH422G, PSRAM octal
