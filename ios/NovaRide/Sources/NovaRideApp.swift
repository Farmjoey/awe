import SwiftUI

@main
struct NovaRideApp: App {
    @StateObject private var ble = BLEManager()
    var body: some Scene {
        WindowGroup {
            ContentView().environmentObject(ble).preferredColorScheme(.dark)
        }
    }
}
