NTP OLED clock for Heltec WiFi LoRa V2 board https://www.aliexpress.com/item/1005005967763162.html
- reads time from pool.ntp.org every 2.5 hours
- shows time with precision <0.1s (maybe even less, like 0.02s, but I can't guarrantee it. Update: the board clock drifts 0.03s per hour - it's compensated every 2.5 hours, but inside it can be up to 0.075 (theoretically, practically seen - 0.14), and due to many other factors the difference can be more, of course)
- reads weather (temperature only) from open-meteo.com every 15 minues

TODO: detect and process WiFi disconnection - reconnect
