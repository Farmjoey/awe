# NOVA RIDE til iPhone (IPA)

Der er to versioner af appen med samme funktioner:
* **Web-app** (`app/index.html`): virker i Bluefy på iPhone, og i Chrome/Edge på Android og Windows. Har også widget-editor og deling.
* **iPhone-app (IPA)**: ægte app med CoreBluetooth. Kildekoden ligger i `ios/NovaRide/`. Har Enhed, Opdater, Temaer, Boot, Widgets (start-widgets) og Musik. Editor og deling af widgets er kun i web-appen.

## Byg IPA'en uden Mac
1. Upload projektet til GitHub (inkl. `.github/`).
2. Actions → **build-ios** → *Run workflow*. En GitHub-maskine med macOS bygger appen.
3. Hent artifact **NovaRide-ipa** (`NovaRide.ipa`).

IPA'en er **usigneret** (Apple kræver din egen signatur). Installér fra Windows:
* **Sideloadly** (sideloadly.io): tilslut iPhonen, træk `NovaRide.ipa` ind, log ind med dit Apple-ID. Appen udløber efter 7 dage med et gratis Apple-ID og skal så installeres igen.
* Eller **AltStore**. Til App Store/TestFlight kræves en betalt Apple Developer-konto.
På iPhonen: Indstillinger → Generelt → VPN og enhedsstyring → stol på dit Apple-ID. Aktivér *Udviklertilstand*, hvis iOS beder om det.

## Ikke testet
Swift-koden er skrevet uden adgang til en Swift-compiler og er **ikke bygget eller prøvet**. Første kørsel på GitHub kan give compilerfejl; send mig loggen fra det røde trin, så retter jeg dem.
