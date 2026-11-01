import SwiftUI
import UniformTypeIdentifiers

private let accent = Color(red: 1.0, green: 0.51, blue: 0.0)   // Flipper-style orange

struct ContentView: View {
    @EnvironmentObject var ble: BLEManager
    var body: some View {
        TabView {
            DeviceView().tabItem { Label("Enhed", systemImage: "scooter") }
            UpdateView().tabItem { Label("Opdater", systemImage: "arrow.up.circle") }
            ThemesView().tabItem { Label("Temaer", systemImage: "paintpalette") }
            BootView().tabItem { Label("Boot", systemImage: "sparkles") }
            WidgetsView().tabItem { Label("Widgets", systemImage: "square.grid.2x2") }
            MusicView().tabItem { Label("Musik", systemImage: "music.note") }
        }
        .accentColor(accent)
    }
}

struct Card<Content: View>: View {
    let content: Content
    init(@ViewBuilder _ c: () -> Content) { content = c() }
    var body: some View {
        content.padding(16).frame(maxWidth: .infinity, alignment: .leading)
            .background(Color(white: 0.11)).cornerRadius(18)
    }
}

struct Notice: View {
    @EnvironmentObject var ble: BLEManager
    var body: some View {
        if !ble.message.isEmpty {
            Text(ble.message).font(.callout).padding(12).frame(maxWidth: .infinity, alignment: .leading)
                .background(accent.opacity(0.15)).cornerRadius(12)
        }
    }
}

struct OrangeButton: ButtonStyle {
    func makeBody(configuration: Configuration) -> some View {
        configuration.label.font(.headline).foregroundColor(.black).frame(maxWidth: .infinity).padding(14)
            .background(accent.opacity(configuration.isPressed ? 0.7 : 1)).cornerRadius(14)
    }
}

struct DeviceView: View {
    @EnvironmentObject var ble: BLEManager
    var body: some View {
        ScrollView {
            VStack(spacing: 12) {
                Notice()
                Card {
                    Text("NOVA RIDE OS").font(.title2).bold()
                    Text(ble.status).foregroundColor(.gray)
                    if let i = ble.info {
                        Divider()
                        row("Firmware", i.version); row("Profil", i.profile)
                        row("Tema", Nova.themeNames.indices.contains(i.theme) ? Nova.themeNames[i.theme] : "?")
                        row("Bootskærm", Nova.bootStyles.indices.contains(i.style) ? Nova.bootStyles[i.style] : "?")
                        row("Telefon", i.phoneLinked ? "forbundet" : "ikke forbundet")
                        row("Løbehjul", i.scooterLinked ? "forbundet" : "ikke forbundet")
                        row("Dashboard", i.customLayout ? "eget" : "standard")
                    }
                }
                if ble.connected {
                    Button("Afbryd") { ble.disconnect() }.buttonStyle(OrangeButton())
                } else {
                    Button("Find NOVA RIDE") { ble.connect() }.buttonStyle(OrangeButton())
                    Text("Sæt PHONE ON i boardets Settings først.").font(.footnote).foregroundColor(.gray)
                }
            }.padding()
        }
    }
    private func row(_ a: String, _ b: String) -> some View {
        HStack { Text(a).foregroundColor(.gray); Spacer(); Text(b) }
    }
}

struct UpdateView: View {
    @EnvironmentObject var ble: BLEManager
    @State private var picking = false
    @State private var fileName = ""
    @State private var fileData: Data?
    var body: some View {
        ScrollView {
            VStack(spacing: 12) {
                Notice()
                Card {
                    Text("Opdater firmware").font(.title2).bold()
                    Text("1. På boardet: Settings > Tools > UPDATE, hold knappen til WINDOW OPEN.").font(.callout)
                    Text("2. Vælg NOVA_RIDE_<profil>.bin (ikke .factory.bin).").font(.callout)
                    Text("3. Tryk Start. Stå stille med hjulet.").font(.callout)
                }
                Card {
                    Button(fileName.isEmpty ? "Vælg .bin-fil" : fileName) { picking = true }.buttonStyle(OrangeButton())
                    if let d = fileData { Text("\(d.count / 1024) KB  CRC-32 \(String(format: "%08X", crc32(d)))").font(.footnote).foregroundColor(.gray) }
                    ProgressView(value: ble.ota.fraction).tint(accent)
                    Button("Start opdatering") { if let d = fileData { ble.startUpdate(d) } }
                        .buttonStyle(OrangeButton()).disabled(fileData == nil || !ble.connected || ble.busy)
                }
                Text("Ingen signatur: installér kun filer, du selv har bygget.").font(.footnote).foregroundColor(.gray)
            }.padding()
        }
        .fileImporter(isPresented: $picking, allowedContentTypes: [.data]) { result in
            guard case .success(let url) = result else { return }
            let ok = url.startAccessingSecurityScopedResource()
            defer { if ok { url.stopAccessingSecurityScopedResource() } }
            if let d = try? Data(contentsOf: url) { fileData = d; fileName = url.lastPathComponent }
        }
    }
}

struct ThemesView: View {
    @EnvironmentObject var ble: BLEManager
    private let colors: [Color] = [.cyan, .orange, .green, .purple, .pink, .white]
    var body: some View {
        ScrollView {
            VStack(spacing: 12) {
                Notice()
                ForEach(0..<Nova.themeNames.count, id: \.self) { i in
                    Button { ble.setTheme(i) } label: {
                        Card {
                            HStack {
                                Circle().fill(colors[i]).frame(width: 28, height: 28)
                                Text(Nova.themeNames[i]).font(.headline)
                                Spacer()
                                if ble.info?.theme == i { Image(systemName: "checkmark.circle.fill").foregroundColor(accent) }
                            }
                        }
                    }.buttonStyle(.plain).disabled(!ble.connected)
                }
            }.padding()
        }
    }
}

struct BootView: View {
    @EnvironmentObject var ble: BLEManager
    @State private var text = ""
    var body: some View {
        ScrollView {
            VStack(spacing: 12) {
                Notice()
                Card {
                    Text("Bootskærm").font(.title2).bold()
                    Picker("Stil", selection: Binding(get: { ble.info?.style ?? 0 }, set: { ble.setBootStyle($0) })) {
                        ForEach(0..<Nova.bootStyles.count, id: \.self) { Text(Nova.bootStyles[$0]).tag($0) }
                    }.pickerStyle(.segmented).disabled(!ble.connected)
                }
                Card {
                    Text("Tekst (maks 15 tegn)").foregroundColor(.gray)
                    TextField("Dit navn", text: $text).textFieldStyle(.roundedBorder)
                    Button("Send tekst") { ble.setBootText(text) }.buttonStyle(OrangeButton()).disabled(!ble.connected)
                    Text("Billede til bootskærmen laves i web-appen.").font(.footnote).foregroundColor(.gray)
                }
            }.padding()
        }
    }
}

struct WidgetsView: View {
    @EnvironmentObject var ble: BLEManager
    private var files: [URL] {
        (Bundle.main.urls(forResourcesWithExtension: "nwl", subdirectory: "widgets") ?? []).sorted { $0.lastPathComponent < $1.lastPathComponent }
    }
    var body: some View {
        ScrollView {
            VStack(spacing: 12) {
                Notice()
                Card { Text("Widgets").font(.title2).bold(); Text("Start-widgets. Egne og delte widgets laver du i web-appen.").font(.callout).foregroundColor(.gray) }
                ForEach(files, id: \.self) { url in
                    Card {
                        Text(url.deletingPathExtension().lastPathComponent.replacingOccurrences(of: "-", with: " ").capitalized).font(.headline)
                        Button("Installér") {
                            if let d = try? Data(contentsOf: url) { ble.installLayout([UInt8](d)) }
                        }.buttonStyle(OrangeButton()).disabled(!ble.connected)
                    }
                }
                Button("Brug standard-dashboard") { ble.useStandardLayout() }.buttonStyle(OrangeButton()).disabled(!ble.connected)
            }.padding()
        }
    }
}

struct MusicView: View {
    @EnvironmentObject var ble: BLEManager
    var body: some View {
        VStack(spacing: 24) {
            Notice()
            Text("Musik").font(.title2).bold()
            Text("Knapperne får boardet til at sende medietaster til telefonen.").font(.callout).foregroundColor(.gray).multilineTextAlignment(.center)
            HStack(spacing: 28) {
                round("backward.fill") { ble.music(0) }
                round("playpause.fill") { ble.music(1) }
                round("forward.fill") { ble.music(2) }
            }
            Spacer()
        }.padding()
    }
    private func round(_ icon: String, _ action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Image(systemName: icon).font(.title).foregroundColor(.black).frame(width: 76, height: 76).background(accent).clipShape(Circle())
        }.disabled(!ble.connected)
    }
}
