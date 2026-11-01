# Funktionsstatus v1.0.0

## Verificeret (på PC, uden hardware)
* Pure logik (gestus, hold-tracker, touch-kalibrering, sikkerhedsregler, demo-data, "--"-formatering): **54921 checks, 0 fejl** (`tools/run_host_tests.sh`, C++11).
* Alle `.cpp`-filer syntakstjekket (`g++ -fsyntax-only`) mod midlertidige stub-headere for Arduino/TFT_eSPI/NimBLE.
  Stubs afspejler *mine* antagelser om bibliotekernes API – det er **ikke** en rigtig kompilering.

## IKKE verificeret
* Kompilering med PlatformIO, linkning, firmwarestørrelse og versionslåste biblioteker (kræver netværk – gøres af GitHub Actions).
* Opstart, display, farver/inversion, touch, kalibrering, lysstyrke, hastighed/flicker på dit board.
* BLE-scanning (NimBLE) og RAM-forbrug under scanning.
* Alt vedr. E-Wheels e2s Urban Max: **ingen forbindelse, ingen protokol, ingen telemetri**.

## Funktioner
| Funktion | Status |
|---|---|
| Dashboard (hastighed, batteri, spænding, effekt, trip/total, mode/fejl) | Implementeret; viser kun demo-data (mærket DEMO) eller `--` |
| Klokkeslæt | Ikke tilgængeligt (ingen RTC/tidskilde) – viser `--:--` |
| BLE | Scanning + læse-only forbindelse til valgt hjul (profil-detektion, notifikationer, læseforespørgsler). **Ikke testet på hardware.** Ingen skrivning |
| Scooter-protokol | XBOT læsning ported fra kilde (afkodning testet mod optaget ramme). Verificeret **ikke** på Urban Max. Skrivning ikke understøttet (`kVerified=false`, `kWriteSupported=false`) |
| 20 km/t + lås | Swipe op: på rigtigt hjul sænkes Sport/Drive/Eco-grænserne til 20 (hold 1,5 s), kan gendannes, og LOCK/UNLOCK. Vises kun aktiv efter aflæsning. **Ikke testet på hjulet.** Uden forbindelse: lokal demo |
| Musik (iPhone) | Bluetooth: PREV / PLAY-PAUSE / NEXT som medietaster (se `docs/PHONE.md`). **Ikke testet.** Titel/kunstner (AMS) ikke lavet |
| Beskeder | Ikke lavet (ANCS er næste trin); intet gemmes |
| Forside | Ringmåler med hastighed, tilstandsnavn, effekt, batteri, spænding, trip/total, status; SET-knap øverst til højre; tilstandsknapper nederst (hold 0,8 s). **Tegnekoden er ikke set på en rigtig skærm** (kun i forhåndsvisningen) |
| Temperaturer og strøm | Under hastigheden: controller- og motortemperatur (`0xBB`, `0x3F`); under effektbjælken: batteristrøm (`0x48`). Motortemperatur vises kun, hvis hjulet melder en værdi forskellig fra 0 (kilden: en flad 0 betyder ofte ingen sensor). **Motorstrøm findes ikke i protokollen** (kun batteristrøm). Ikke testet på hjulet |
| Farver / skærm | Standard er nu normal polaritet (inversion fra), og `COLORS` (A/B/C) vælger rød/blå-rækkefølge på ST7789 uden ny flash. **Se på skærmen og tryk COLORS, til farverne er rigtige** |
| Ny forside | Glattere ring (54 segmenter), nye knapper, musikknapper (swipe langs knaprækken), swipe til musik/beskeder |
| Titel og beskeder | Kun i `cyd_st7789_apple` (AMS/ANCS). Parsere testet; **radiodelen er ikke testet på en rigtig iPhone** |
| Widgets | Eget dashboard (JSON → 360 bytes), 4 start-widgets, editor og deling via GitHub. Firmware-parseren er fuzz-testet mod appens compiler |
| iPhone-app (IPA) | Swift-kildekode + GitHub-workflow (`build-ios`). **Ikke bygget** |
| Telefon-app (web) | Opdater firmware med .bin, temaer, bootskærm (tekst/billede), musikfjernbetjening; se `docs/APP.md`. Testet mod simuleret enhed; **ikke testet med rigtigt board/Bluefy** |
| OTA på boardet | Krypteret link, åbent vindue, ikke mens hjulet kører, størrelse/header/CRC-kontrol. **Ikke testet på hardware** |
| Temaer / bootskærm | 6 temaer, 4 bootstile, billede 128×64. Ikke set på rigtig skærm |
| Fart pr. tilstand | CTRL: -/+ pr. tilstand (1 km/t), aflæses og bekræftes. Ikke testet på hjulet |
| Ride controls | CTRL: demo = lokal simulation. Rigtigt hjul: lys, zero start og køretilstand kan skrives efter UNLOCK (hold 1,5 s), kun hvidlistede registre, og kun markeret bekræftet efter aflæsning. **Ikke testet på hjulet.** Hazard: ingen kendt kommando |
| Indstillinger gemmes | Implementeret (NVS) – ikke testet på hardware |
| Start-begrænsning + service-menu | Appen slår 20 km/t til/fra (op 0x50). Firmwaren begrænser ved hver forbindelse til hjulet. Hold SPORT 2 s + speeder (strøm > 120 W som erstatning) åbner SERVICE, som frigiver grænsen for én tur. Logik testet på PC. **Ikke bygget, ikke testet på board eller hjul** |
| Nyt dashboard (STEEL) | Bue med knop, pille-bjælker, outlinede knapper efter reference. Kun set i forhåndsvisningen |
| Widgets 2.0 + studio | 13 elementtyper, egne farver, betingelser, flere sider, knapper, grafer, ikoner. Format, parser, compiler og app-editor testet på PC (fuzz af alle starter-filer). Tegneren `WidgetDraw.cpp` er kun syntakstjekket, **ikke kørt på board** |
| Live-visning i app | Boardet sender data to gange i sekundet (STAT 0x40). Demo-board testet |
