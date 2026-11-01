import Foundation
import CoreBluetooth

/// Same protocol as the web app (docs/APP_PROTOCOL.md).
enum Nova {
    static let service = CBUUID(string: "4e4f5641-5249-4445-0000-000000000001")
    static let ctrl = CBUUID(string: "4e4f5641-5249-4445-0000-000000000002")
    static let data = CBUUID(string: "4e4f5641-5249-4445-0000-000000000003")
    static let stat = CBUUID(string: "4e4f5641-5249-4445-0000-000000000004")

    enum Op {
        static let info: UInt8 = 0x01, otaBegin: UInt8 = 0x10, otaAbort: UInt8 = 0x12
        static let theme: UInt8 = 0x20, bootStyle: UInt8 = 0x21, bootText: UInt8 = 0x22
        static let layoutBegin: UInt8 = 0x40, layoutChunk: UInt8 = 0x41, layoutEnd: UInt8 = 0x42, layoutUse: UInt8 = 0x43
        static let music: UInt8 = 0x30
    }

    static let themeNames = ["NOVA BLUE", "EMBER", "MATRIX", "VIOLET", "ROSE", "MONO"]
    static let bootStyles = ["Klassisk", "Navn", "Billede", "Minimal"]
    static let otaErrors = ["", "Opdateringsvinduet er lukket på boardet", "Løbehjulet kører", "Filstørrelsen accepteres ikke",
                            "Ikke en ESP32-firmware (.bin)", "CRC-fejl", "Skrivefejl i flash", "Kunne ikke starte opdatering",
                            "Dataoverløb – prøv igen", "Boardet er optaget", "Timeout"]
}

struct DeviceInfo {
    var theme = 0, style = 0, flags: UInt8 = 0
    var version = "", profile = "", bootText = ""
    var windowOpen: Bool { flags & 1 != 0 }
    var phoneLinked: Bool { flags & 4 != 0 }
    var scooterLinked: Bool { flags & 8 != 0 }
    var customLayout: Bool { flags & 16 != 0 }
}

struct OtaStatus {
    var state = 0, err = 0
    var received = 0, total = 0
    var fraction: Double { total > 0 ? Double(received) / Double(total) : 0 }
}

func crc32(_ data: Data) -> UInt32 {
    var table = [UInt32](repeating: 0, count: 256)
    for n in 0..<256 {
        var c = UInt32(n)
        for _ in 0..<8 { c = (c & 1) != 0 ? (0xEDB88320 ^ (c >> 1)) : (c >> 1) }
        table[n] = c
    }
    var crc: UInt32 = 0xFFFFFFFF
    for b in data { crc = table[Int((crc ^ UInt32(b)) & 0xFF)] ^ (crc >> 8) }
    return crc ^ 0xFFFFFFFF
}

func le32(_ v: UInt32) -> [UInt8] { [UInt8(v & 255), UInt8((v >> 8) & 255), UInt8((v >> 16) & 255), UInt8((v >> 24) & 255)] }
