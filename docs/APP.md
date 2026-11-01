# NOVA RIDE – telefon-app (v0.8.0)

En web-app i Flipper Zero-stil: Enhed, Opdater, Temaer, Boot og Musik. Den taler med boardet over Bluetooth (Web Bluetooth).
Ingen App Store, ingen Mac, ingen konto.

## Sådan får du den på telefonen
1. Upload projektet til GitHub (som til firmwaren). I repositoryet: **Settings → Pages → Source: GitHub Actions**.
2. Workflowet *deploy-app* lægger `app/index.html` på en https-adresse (står under Actions/Pages).
3. **iPhone:** installér den gratis app **Bluefy** (Safari kan ikke Bluetooth på sider) og åbn adressen dér.
   **Android/Windows:** åbn adressen i Chrome eller Edge.
4. Test uden board: tryk **Prøv demo uden board**.

## Forbind
Boardet: Settings → **PHONE ON**. Appen: **Find NOVA RIDE**. Første gang parrer iPhonen (samme parring som musiktaster).

## Opdater firmware med en .bin
1. Byg en ny firmware med GitHub Actions og hent **`NOVA_RIDE_<profil>.bin`** (ikke `.factory.bin`).
2. Boardet: Settings → **UPDATE** → hold knappen 1,5 s → **WINDOW OPEN** (2 minutter).
3. Appen: fanen **Opdater** → vælg filen → **Start opdatering**. Boardet viser fremdrift og genstarter.
Sikkerhed: krypteret (parret) forbindelse, vinduet skal åbnes på boardet, afvises mens hjulet kører, filen kontrolleres (størrelse, ESP32-header, CRC-32). Der er **ingen signatur**: installér kun filer, du selv har bygget. Går noget galt, kan du altid flashe via USB.

## Temaer og bootskærm
* 6 temaer; ændres med det samme på skærmen (også fra Settings på boardet).
* Bootskærm: Klassisk, Navn (din tekst), Billede (128×64, sort/hvid, laves i appen) og Minimal.

## Musik
Knapperne i appen får boardet til at sende medietaster til telefonen. Titel/kunstner og beskeder er ikke med endnu.

## Ikke testet
Appen er testet mod en simuleret enhed (opdatering, tema, bootskærm) og CRC-32 er sammenlignet med firmwarens. Den er ikke prøvet med et rigtigt board eller i Bluefy. Protokollen står i `docs/APP_PROTOCOL.md`.
