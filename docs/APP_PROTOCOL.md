# App-protokol (Bluetooth LE GATT)
Service `4e4f5641-5249-4445-0000-000000000001`. Alle tal er little-endian.
| Karakteristik | UUID (slutter på) | Egenskaber |
|---|---|---|
| CTRL | `…0002` | write, kræver kryptering |
| DATA | `…0003` | write without response, kræver kryptering |
| STAT | `…0004` | read, notify |

**CTRL (første byte = kommando)**: `01` INFO · `10` OTA_BEGIN `u32 størrelse, u32 crc32` · `11` OTA_END · `12` OTA_ABORT · `20` THEME `u8` · `21` BOOT_STYLE `u8` (0–3) · `22` BOOT_TEXT ASCII (≤15) ·
`23` IMG_BEGIN · `24` IMG_CHUNK `u16 offset, data` · `25` IMG_END (1024 bytes, 128×64, 1 bit, MSB først) · `26` PREVIEW · `30` MUSIC `u8` (0 forrige, 1 afspil/pause, 2 næste).

**STAT (første byte = type)**: `01` INFO (`proto, tema, bootstil, flag, "version\0profil\0tekst\0"`; flag: 1 vindue åbent, 2 billede gemt, 4 telefon, 8 løbehjul) · `10` OTA (`tilstand, fejl, u32 modtaget, u32 total`) · `30` ACK (`kommando, 0 = ok`).

**OTA**: BEGIN → data i 160-byte stykker på DATA (højst ~6 KB ude ad gangen; boardet melder fremdrift hver 4 KB) → boardet kontrollerer CRC, skriver og genstarter.
Fejlkoder: 1 vindue lukket · 2 løbehjul kører · 3 størrelse · 4 ikke ESP32-app · 5 CRC · 6 flash · 7 start mislykkedes · 8 overløb · 9 optaget · 10 timeout.

**Start-begrænsning**: CTRL `50` STARTCAP `u8` (1 = hold hjulet på 20 km/t ved hver tænding, 0 = fra). Kun appen kan sætte den. INFO-flag bit 6 (64) = slået til.
