import AppKit
@main struct Host {
    @MainActor static func main() {
        let app = NSApplication.shared
        app.setActivationPolicy(.accessory)
        app.run()
    }
}
