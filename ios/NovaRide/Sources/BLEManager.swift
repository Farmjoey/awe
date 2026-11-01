import Foundation
import CoreBluetooth

final class BLEManager: NSObject, ObservableObject, CBCentralManagerDelegate, CBPeripheralDelegate {
    @Published var status = "Ikke forbundet"
    @Published var connected = false
    @Published var info: DeviceInfo?
    @Published var ota = OtaStatus()
    @Published var message = ""
    @Published var busy = false

    private var central: CBCentralManager!
    private var peripheral: CBPeripheral?
    private var ctrlChar: CBCharacteristic?
    private var dataChar: CBCharacteristic?
    private var statChar: CBCharacteristic?
    private var writeQueue: [[UInt8]] = []
    private var writing = false

    // firmware update
    private var otaData: Data?
    private var otaSent = 0
    private var otaSending = false

    override init() {
        super.init()
        central = CBCentralManager(delegate: self, queue: .main)
    }

    // MARK: connection
    func connect() {
        message = ""
        guard central.state == .poweredOn else { status = "Bluetooth er slået fra"; return }
        status = "Søger efter NOVA RIDE…"
        central.scanForPeripherals(withServices: nil, options: nil)
    }

    func disconnect() {
        if let p = peripheral { central.cancelPeripheralConnection(p) }
    }

    func centralManagerDidUpdateState(_ central: CBCentralManager) {
        if central.state != .poweredOn { status = "Bluetooth er ikke klar" }
    }

    func centralManager(_ central: CBCentralManager, didDiscover peripheral: CBPeripheral,
                        advertisementData: [String: Any], rssi RSSI: NSNumber) {
        let name = (advertisementData[CBAdvertisementDataLocalNameKey] as? String) ?? peripheral.name ?? ""
        guard name.hasPrefix("NOVA RIDE") else { return }
        central.stopScan()
        self.peripheral = peripheral
        peripheral.delegate = self
        status = "Forbinder…"
        central.connect(peripheral, options: nil)
    }

    func centralManager(_ central: CBCentralManager, didConnect peripheral: CBPeripheral) {
        status = "Finder tjenester…"
        peripheral.discoverServices([Nova.service])
    }

    func centralManager(_ central: CBCentralManager, didFailToConnect peripheral: CBPeripheral, error: Error?) {
        status = "Kunne ikke forbinde"
        message = "Prøv igen. Hvis det bliver ved: sluk/tænd boardet."
    }

    func centralManager(_ central: CBCentralManager, didDisconnectPeripheral peripheral: CBPeripheral, error: Error?) {
        connected = false
        info = nil
        busy = false
        otaSending = false
        writeQueue.removeAll()
        writing = false
        status = ota.state == 4 ? "Opdatering færdig – boardet genstarter" : "Ikke forbundet"
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverServices error: Error?) {
        guard let svc = peripheral.services?.first(where: { $0.uuid == Nova.service }) else {
            status = "App-tjenesten mangler"
            message = "Firmwaren er ældre end 0.8, eller iPhonen husker en gammel tjenesteliste. Indstillinger > Bluetooth > NOVA RIDE (i) > Glem denne enhed, og par igen."
            return
        }
        peripheral.discoverCharacteristics([Nova.ctrl, Nova.data, Nova.stat], for: svc)
    }

    func peripheral(_ peripheral: CBPeripheral, didDiscoverCharacteristicsFor service: CBService, error: Error?) {
        for c in service.characteristics ?? [] {
            if c.uuid == Nova.ctrl { ctrlChar = c }
            if c.uuid == Nova.data { dataChar = c }
            if c.uuid == Nova.stat { statChar = c }
        }
        guard let s = statChar, ctrlChar != nil, dataChar != nil else { status = "Kanaler mangler"; return }
        peripheral.setNotifyValue(true, for: s)
    }

    func peripheral(_ peripheral: CBPeripheral, didUpdateNotificationStateFor characteristic: CBCharacteristic, error: Error?) {
        if characteristic.uuid == Nova.stat, error == nil {
            connected = true
            status = "Forbundet"
            send([Nova.Op.info])
        }
    }

    // MARK: writing
    /// Control writes are queued and sent one at a time (the board answers each one at ATT level).
    func send(_ bytes: [UInt8]) {
        writeQueue.append(bytes)
        pumpWrites()
    }

    private func pumpWrites() {
        guard !writing, let p = peripheral, let c = ctrlChar, !writeQueue.isEmpty else { return }
        writing = true
        p.writeValue(Data(writeQueue.removeFirst()), for: c, type: .withResponse)
    }

    func peripheral(_ peripheral: CBPeripheral, didWriteValueFor characteristic: CBCharacteristic, error: Error?) {
        writing = false
        if let e = error {
            message = "Boardet afviste kommandoen. Par NOVA RIDE i iPhone-indstillinger > Bluetooth, og prøv igen. (\(e.localizedDescription))"
            writeQueue.removeAll()
            busy = false
        }
        pumpWrites()
    }

    // MARK: notifications
    func peripheral(_ peripheral: CBPeripheral, didUpdateValueFor characteristic: CBCharacteristic, error: Error?) {
        guard characteristic.uuid == Nova.stat, let d = characteristic.value else { return }
        let u = [UInt8](d)
        guard !u.isEmpty else { return }
        switch u[0] {
        case 0x01 where u.count > 5:
            let parts = String(decoding: u[5...], as: UTF8.self).split(separator: "\0", omittingEmptySubsequences: false).map(String.init)
            var i = DeviceInfo()
            i.theme = Int(u[2]); i.style = Int(u[3]); i.flags = u[4]
            i.version = parts.count > 0 ? parts[0] : ""
            i.profile = parts.count > 1 ? parts[1] : ""
            i.bootText = parts.count > 2 ? parts[2] : ""
            info = i
        case 0x10 where u.count >= 11:
            var o = OtaStatus()
            o.state = Int(u[1]); o.err = Int(u[2])
            o.received = Int(u[3]) | Int(u[4]) << 8 | Int(u[5]) << 16 | Int(u[6]) << 24
            o.total = Int(u[7]) | Int(u[8]) << 8 | Int(u[9]) << 16 | Int(u[10]) << 24
            ota = o
            if o.state == 5 {
                message = Nova.otaErrors.indices.contains(o.err) ? Nova.otaErrors[o.err] : "Fejl på boardet"
                otaSending = false; busy = false
                send([Nova.Op.otaAbort])
            } else if o.state == 2 { pumpOta() }
            else if o.state == 4 { otaSending = false; busy = false }
        case 0x30 where u.count >= 3:
            if u[2] != 0 { message = "Boardet afviste kommandoen (kode \(u[2]))" }
        default: break
        }
    }

    // MARK: settings
    func setTheme(_ i: Int) { send([Nova.Op.theme, UInt8(i)]); send([Nova.Op.info]) }
    func setBootStyle(_ i: Int) { send([Nova.Op.bootStyle, UInt8(i)]); send([Nova.Op.info]) }
    func setBootText(_ s: String) {
        let ascii = s.unicodeScalars.filter { $0.value >= 0x20 && $0.value < 0x7F }.prefix(15).map { UInt8($0.value) }
        send([Nova.Op.bootText] + ascii); send([Nova.Op.info])
    }
    func music(_ k: UInt8) { send([Nova.Op.music, k]) }

    /// Installs a compiled widget layout (.nwl, made by tools/widget_cli.js).
    func installLayout(_ bytes: [UInt8]) {
        send([Nova.Op.layoutBegin])
        var off = 0
        while off < bytes.count {
            let n = min(120, bytes.count - off)
            send([Nova.Op.layoutChunk, UInt8(off & 255), UInt8(off >> 8)] + Array(bytes[off..<off + n]))
            off += n
        }
        send([Nova.Op.layoutEnd])
        send([Nova.Op.info])
    }
    func useStandardLayout() { send([Nova.Op.layoutUse, 0]); send([Nova.Op.info]) }

    // MARK: firmware update
    func startUpdate(_ file: Data) {
        message = ""
        guard connected else { message = "Forbind først."; return }
        guard file.count >= 65536, file.count <= 0x140000 else { message = "Størrelsen skal være mellem 64 KB og 1280 KB."; return }
        guard file[0] == 0xE9, file[12] == 0, file[13] == 0 else { message = "Det ligner ikke en ESP32-app. Brug NOVA_RIDE_<profil>.bin (ikke .factory.bin)."; return }
        otaData = file; otaSent = 0; otaSending = true; busy = true
        ota = OtaStatus(state: 0, err: 0, received: 0, total: file.count)
        send([Nova.Op.otaBegin] + le32(UInt32(file.count)) + le32(crc32(file)))
    }

    private func pumpOta() {
        guard otaSending, let d = otaData, let p = peripheral, let c = dataChar else { return }
        let chunk = min(180, p.maximumWriteValueLength(for: .withoutResponse))
        while otaSent < d.count && otaSent - ota.received < 6144 && p.canSendWriteWithoutResponse {
            let n = min(chunk, d.count - otaSent)
            p.writeValue(d.subdata(in: otaSent..<otaSent + n), for: c, type: .withoutResponse)
            otaSent += n
        }
    }

    func peripheralIsReady(toSendWriteWithoutResponse peripheral: CBPeripheral) { pumpOta() }
}
