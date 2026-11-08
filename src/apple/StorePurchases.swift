import AppKit
import StoreKit
import Security
import LocalAuthentication

// Trial dates come from verified Apple transactions, never first-launch flags.
struct TrialPolicy {
    static let duration: TimeInterval = 3 * 24 * 60 * 60
    static func expires(start: Date?) -> Date? { start?.addingTimeInterval(duration) }
    static func active(start: Date?, now: Date) -> Bool {
        guard let start, let end = expires(start: start) else { return false }
        return now >= start && now < end
    }
}

@MainActor
final class StorePurchases {
    // The immutable product identifier was reserved before the pricing scope
    // changed. Its Connect display name and entitlement cover the whole app.
    static let productID = "ai.ffclip.studio.avatar.lifetime"
    static let trialID = "ai.ffclip.studio.trial3days"
    var context: UnsafeMutableRawPointer?
    var callback: (@convention(c) (UnsafeMutableRawPointer?, UnsafePointer<CChar>?) -> Void)?
    var product: Product?
    var trialProduct: Product?
    var ready = false
    var unlocked = false
    var trialStart: Date?
    var busy = false
    var message = ""
    var listener: Task<Void, Never>?
    var operation: Task<Void, Never>?
    var timer: Timer?
    private var clockFloor = Date.distantPast
    private var clockUptime = ProcessInfo.processInfo.systemUptime
    private var savedFloor = Date.distantPast
    private let clockQuery: [String: Any] = [kSecClass as String: kSecClassGenericPassword,
        kSecUseDataProtectionKeychain as String: true,
        kSecAttrService as String: (Bundle.main.bundleIdentifier ?? "ai.ffclip.studio") + ".trial-clock", kSecAttrAccount as String: "last-seen"]

    init(context: UnsafeMutableRawPointer?, callback: @escaping @convention(c) (UnsafeMutableRawPointer?, UnsafePointer<CChar>?) -> Void) {
        self.context = context
        self.callback = callback
        var query = clockQuery; query[kSecReturnData as String] = true
        // Clock hardening must never present a keychain password dialog at launch.
        let authentication = LAContext()
        authentication.interactionNotAllowed = true
        query[kSecUseAuthenticationContext as String] = authentication
        var value: CFTypeRef?
        if SecItemCopyMatching(query as CFDictionary, &value) == errSecSuccess,
           let data = value as? Data, let text = String(data: data, encoding: .utf8),
           let seconds = Double(text), seconds.isFinite {
            clockFloor = Date(timeIntervalSince1970: seconds); savedFloor = clockFloor
        }
        listener = Task { [weak self] in
            for await result in Transaction.updates {
                guard !Task.isCancelled, let self else { return }
                guard [Self.productID, Self.trialID].contains(result.unsafePayloadValue.productID) else { continue }
                await self.entitlements()
                if case .verified(let transaction) = result { await transaction.finish() }
                self.publish()
            }
        }
        timer = Timer.scheduledTimer(withTimeInterval: 15, repeats: true) { [weak self] _ in
            MainActor.assumeIsolated { self?.publish() }
        }
    }

    // Remember the maximum observed clock in the keychain. System uptime also
    // keeps a running trial moving forward if the wall clock is moved backwards.
    func effectiveNow(signedDate: Date? = nil) -> Date {
        let uptime = ProcessInfo.processInfo.systemUptime
        let elapsed = max(0, uptime - clockUptime)
        clockFloor = max(Date(), clockFloor.addingTimeInterval(elapsed), signedDate ?? .distantPast)
        clockUptime = uptime
        if trialStart != nil && clockFloor.timeIntervalSince(savedFloor) >= 60 {
            let data = Data(String(clockFloor.timeIntervalSince1970).utf8)
            let result = SecItemUpdate(clockQuery as CFDictionary, [kSecValueData as String: data] as CFDictionary)
            if result == errSecItemNotFound {
                var item = clockQuery; item[kSecValueData as String] = data
                item[kSecAttrAccessible as String] = kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly
                SecItemAdd(item as CFDictionary, nil)
            }
            savedFloor = clockFloor
        }
        return clockFloor
    }

    func publish() {
        let now = effectiveNow()
        let state: [String: Any] = ["ready": ready, "unlocked": unlocked, "busy": busy,
            "available": product != nil && AppStore.canMakePayments,
            "trialAvailable": trialProduct != nil && AppStore.canMakePayments,
            "trialStarted": trialStart != nil,
            "trialExpires": (TrialPolicy.expires(start: trialStart)?.timeIntervalSince1970 ?? 0) * 1000,
            "now": now.timeIntervalSince1970 * 1000,
            "price": product?.displayPrice ?? "", "message": message]
        guard let callback, let data = try? JSONSerialization.data(withJSONObject: state),
              let json = String(data: data, encoding: .utf8) else { return }
        json.withCString { callback(context, $0) }
    }

    func entitlements() async {
        var entitled = false
        var start: Date?
        for await result in Transaction.currentEntitlements {
            guard case .verified(let transaction) = result,
                  transaction.productType == .nonConsumable,
                  transaction.revocationDate == nil else { continue }
            if transaction.productID == Self.productID { entitled = true }
            if transaction.productID == Self.trialID {
                start = min(start ?? .distantFuture, transaction.originalPurchaseDate)
                _ = effectiveNow(signedDate: transaction.signedDate)
            }
        }
        unlocked = entitled; trialStart = start; ready = true
    }

    func refresh() {
        guard operation == nil else { return }
        operation = Task { [weak self] in
            guard let self else { return }
            defer { self.operation = nil }
            await self.entitlements()
            self.publish()
            do {
                let products = try await Product.products(for: [Self.productID, Self.trialID])
                self.product = products.first { $0.id == Self.productID && $0.type == .nonConsumable }
                // Never let an accidentally mispriced trial charge the customer.
                self.trialProduct = products.first { $0.id == Self.trialID && $0.type == .nonConsumable && $0.price == 0 }
                self.message = self.product == nil ? "unavailable" : self.trialProduct == nil ? "trialUnavailable" : ""
            } catch { self.product = nil; self.trialProduct = nil; self.message = "unavailable" }
            self.publish()
        }
    }

    func purchase(trial: Bool = false, in presentingWindow: NSWindow? = nil) {
        guard !busy, !unlocked else { publish(); return }
        if trial && trialStart != nil { publish(); return }
        // Disclose the buyout price before starting the free trial.
        guard let selected = trial ? trialProduct : product, product != nil else { message = "unavailable"; publish(); return }
        guard AppStore.canMakePayments else { message = "restricted"; publish(); return }
        guard let window = presentingWindow ?? NSApp.keyWindow ?? NSApp.mainWindow ?? NSApp.windows.first(where: { $0.isVisible && $0.canBecomeKey }) else { message = "window"; publish(); return }
        busy = true; message = ""; publish()
        Task { [weak self] in
            guard let self else { return }
            defer { self.busy = false; self.publish() }
            do {
                switch try await selected.purchase(confirmIn: window) {
                case .success(let result):
                    guard case .verified(let transaction) = result,
                          transaction.productID == selected.id,
                          transaction.productType == .nonConsumable,
                          transaction.revocationDate == nil else { self.message = "verification"; return }
                    await self.entitlements()
                    await transaction.finish()
                case .pending: self.message = "pending"
                case .userCancelled: break
                @unknown default: self.message = "purchaseFailed"
                }
            } catch StoreKitError.userCancelled { /* Cancelling is not a failure. */ }
              catch { self.message = "purchaseFailed" }
        }
    }

    func restore() {
        guard !busy else { publish(); return }
        busy = true; message = ""; publish()
        Task { [weak self] in
            guard let self else { return }
            defer { self.busy = false; self.publish() }
            do {
                try await AppStore.sync()
                await self.entitlements()
                self.message = self.unlocked || self.trialStart != nil ? "restored" : "notPurchased"
            } catch { self.message = "restoreFailed" }
        }
    }

    func stop() { callback = nil; context = nil; listener?.cancel(); operation?.cancel(); timer?.invalidate() }
}

@_cdecl("moss_store_create")
public func mossStoreCreate(_ context: UnsafeMutableRawPointer?, _ callback: @escaping @convention(c) (UnsafeMutableRawPointer?, UnsafePointer<CChar>?) -> Void) -> UnsafeMutableRawPointer {
    let store = MainActor.assumeIsolated { StorePurchases(context: context, callback: callback) }
    return Unmanaged.passRetained(store).toOpaque()
}
@_cdecl("moss_store_destroy")
public func mossStoreDestroy(_ pointer: UnsafeMutableRawPointer) {
    MainActor.assumeIsolated { let store = Unmanaged<StorePurchases>.fromOpaque(pointer).takeRetainedValue(); store.stop() }
}
@_cdecl("moss_store_refresh")
public func mossStoreRefresh(_ pointer: UnsafeMutableRawPointer) {
    MainActor.assumeIsolated { Unmanaged<StorePurchases>.fromOpaque(pointer).takeUnretainedValue().refresh() }
}
@_cdecl("moss_store_purchase")
public func mossStorePurchase(_ pointer: UnsafeMutableRawPointer) {
    MainActor.assumeIsolated { Unmanaged<StorePurchases>.fromOpaque(pointer).takeUnretainedValue().purchase() }
}
@_cdecl("moss_store_trial")
public func mossStoreTrial(_ pointer: UnsafeMutableRawPointer) {
    MainActor.assumeIsolated { Unmanaged<StorePurchases>.fromOpaque(pointer).takeUnretainedValue().purchase(trial: true) }
}
@_cdecl("moss_store_restore")
public func mossStoreRestore(_ pointer: UnsafeMutableRawPointer) {
    MainActor.assumeIsolated { Unmanaged<StorePurchases>.fromOpaque(pointer).takeUnretainedValue().restore() }
}
