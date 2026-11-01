# Widgets 2.0 – byg din egen skærm

Åbn appen → **Tilpas → Widgets → Åbn editoren**. Tryk på et element for at redigere det, træk for at flytte, træk i det runde håndtag for at ændre størrelse. **Installér** sender layoutet til boardet. Et tryk i øverste højre hjørne på boardet åbner altid indstillinger.

## Elementer
| Type | Bruges til |
|---|---|
| `text` | fast tekst (skrift 1, 2, 4) |
| `num` | tal fra løbehjulet, med tekst foran/efter (skrift 1–8; 6, 7, 8 viser kun tal) |
| `bar` | bjælke, valgfrit vandret/lodret, kant, afrundet, "fra nul" (fx regenerering) |
| `arc` | rund måler: egen start, åbning, runde ender, knop, streger, omvendt |
| `segbar` | række af segmenter (2–40) |
| `spark` | graf over de sidste ca. 2 minutter |
| `rect`, `circle`, `line` | paneler, ringe og streger |
| `button` | knap med handling: sidevalg, køretilstand (hold 0,8 s), musik, indstillinger, beskeder, 20-skærm |
| `dot` | lampe der lyser efter en kilde |
| `icon` | lyn, batteri (med niveau), termometer, måler, advarsel, lås, telefon, lys |
| `modebar` | genvej: fire tilstandsknapper |

## Kilder
Tal: `speed power battery volt current temp motortemp trip total maxspeed maxpower minvolt energy ridetime`, tekst: `mode fault`.
Status (0/1): `ble phone limit demo alert regen moving`. En værdi uden gyldig måling vises som `--` og opdigtes aldrig.

## Regler pr. element
* **Farver:** `text neon ok warn bad dim panel bg glow white black` eller egen `#rrggbb` (højst 16 forskellige).
* **Grænser:** `warn`/`bad` skifter farve (gult/rødt); `below: true` for ting der skal være *over* grænsen, fx batteri.
* **Vis kun når:** `show: {src, op, val}` med `> < == != >= <=`. Ukendt måling skjuler elementet.
* **Sider:** `pages` 1–4 i toppen, `page` pr. element (udelades = alle sider). Stryg til siden eller brug knapper med `nextpage`/`prevpage`.
* Højst 48 elementer, 24 forskellige tekster (15 tegn, kun ASCII).

## Filformat (JSON v2)
```json
{ "v": 2, "name": "Mit design", "author": "mig", "pages": 2, "bg": "#101820",
  "items": [
    { "t": "arc", "x": 90, "y": 104, "r": 82, "th": 9, "a0": 135, "sweep": 270, "src": "speed", "max": 35,
      "color": "neon", "color2": "panel", "caps": true, "knob": true, "warn": 25, "bad": 32 },
    { "t": "button", "x": 178, "y": 175, "w": 97, "h": 47, "action": "sport", "text": "SPORT", "font": "4", "rad": 13 } ] }
```
Gamle v1-filer virker stadig: de opgraderes automatisk (ring → bue, `modebar` → fire knapper).

## Binært format (boardet)
Overskrift 12 byte (`NW`, version 2, antal, tekster, farver, længde, sider, baggrund, flag), tekster à 16 byte, egne farver (RGB565), derefter elementer à 32 byte. Firmwaren (`src/WidgetLayout.h`) tjekker hver regel igen, så en forkert eller ondsindet fil aldrig bliver gemt eller tegnet. Tegningen sker i `src/WidgetDraw.cpp`: kun det der ændrer sig tegnes igen, og lagene bevares.

## Standard-designet
Skærmen på boardet er fra start et design (`app/widgets/starter/standard.json`, indbygget i firmwaren via `src/DefaultLayout.h`). Det kan redigeres som alle andre: tekster, knapper og deres handlinger, farver og sider. Ændrer du standard-designet i repoet, så kør `node tools/widget_cli.js header src/DefaultLayout.h app/widgets/starter/standard.json` (testene fejler, hvis filerne ikke passer sammen). "Gendan standard-design" i appen sætter det på boardet igen.
