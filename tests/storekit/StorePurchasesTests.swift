import XCTest
import AppKit
import StoreKit
import StoreKitTest

@MainActor
final class StorePurchasesTests: XCTestCase {
    func testInteractiveCancellation() async throws {
        try XCTSkipUnless(ProcessInfo.processInfo.environment["MOSS_STOREKIT_INTERACTIVE"] == "1", "Run test_storekit.py --interactive-cancellation for the native Apple sheet.")
        let configuration = try XCTUnwrap(Bundle(for: Self.self).url(forResource:"Products",withExtension:"storekit"))
        let session = try SKTestSession(contentsOf:configuration)
        session.resetToDefaultState(); session.clearTransactions(); session.disableDialogs = false
        defer { session.clearTransactions() }
        let window = NSWindow(contentRect:NSRect(x:0,y:0,width:520,height:360),styleMask:[.titled],backing:.buffered,defer:false)
        window.title = "StoreKit LOCAL TEST — cancel purchase"
        window.center(); window.makeKeyAndOrderFront(nil); NSApp.activate(ignoringOtherApps:true)
        defer { window.orderOut(nil) }
        let store = StorePurchases(context:nil,callback:{_,_ in})
        defer { store.stop() }
        store.refresh(); try await waitUntil { store.operation == nil && store.product != nil }
        store.purchase(in:window)
        for _ in 0..<900 {
            if !store.busy { break }
            try await Task.sleep(for:.milliseconds(100))
        }
        XCTAssertFalse(store.busy,"Cancel the local test purchase sheet within 90 seconds")
        XCTAssertFalse(store.unlocked); XCTAssertTrue(store.message.isEmpty)
        XCTAssertTrue(session.allTransactions().isEmpty)
    }
    func waitUntil(_ condition: @MainActor () -> Bool, file: StaticString = #filePath, line: UInt = #line) async throws {
        for _ in 0..<150 {
            if condition() { return }
            try await Task.sleep(for: .milliseconds(100))
        }
        XCTFail("StoreKit operation timed out", file: file, line: line)
    }
    func testTrialBoundaries() {
        let start = Date(timeIntervalSince1970: 1_800_000_000)
        XCTAssertFalse(TrialPolicy.active(start: nil, now: start))
        XCTAssertFalse(TrialPolicy.active(start: start, now: start.addingTimeInterval(-1)))
        XCTAssertTrue(TrialPolicy.active(start: start, now: start))
        XCTAssertTrue(TrialPolicy.active(start: start, now: start.addingTimeInterval(259_199)))
        XCTAssertFalse(TrialPolicy.active(start: start, now: start.addingTimeInterval(259_200)))
    }
    func testExpiredTrialCannotRestart() async throws {
        let configuration = try XCTUnwrap(Bundle(for: Self.self).url(forResource: "Products", withExtension: "storekit"))
        let session = try SKTestSession(contentsOf: configuration)
        session.resetToDefaultState(); session.clearTransactions(); session.disableDialogs = true
        defer { session.clearTransactions() }
        let originalDate = Date().addingTimeInterval(-4 * 24 * 60 * 60)
        _ = try await session.buyProduct(identifier: StorePurchases.trialID, options: [.purchaseDate(originalDate)])
        let store = StorePurchases(context:nil, callback:{_,_ in})
        defer { store.stop() }
        await store.entitlements()
        let start = try XCTUnwrap(store.trialStart)
        XCTAssertEqual(start.timeIntervalSince1970, originalDate.timeIntervalSince1970, accuracy:1)
        XCTAssertFalse(TrialPolicy.active(start:start, now:store.effectiveNow()))
        store.purchase(trial:true)
        XCTAssertFalse(store.busy)
        XCTAssertEqual(session.allTransactions().count,1)
        store.restore(); try await waitUntil { !store.busy }
        XCTAssertEqual(store.trialStart,start)
        XCTAssertFalse(TrialPolicy.active(start:store.trialStart, now:store.effectiveNow()))
    }
    func testRealStoreKitLifecycle() async throws {
        let configuration = try XCTUnwrap(Bundle(for: Self.self).url(forResource: "Products", withExtension: "storekit"))
        let session = try SKTestSession(contentsOf: configuration)
        session.resetToDefaultState(); session.clearTransactions(); session.disableDialogs = true
        defer { session.clearTransactions() }
        let window = NSWindow(contentRect: NSRect(x:0,y:0,width:440,height:300), styleMask:[.titled], backing:.buffered, defer:false)
        window.title = "StoreKit local transaction tests"
        window.makeKeyAndOrderFront(nil)
        window.makeMain()
        NSApp.activate(ignoringOtherApps: true)
        defer { window.orderOut(nil) }
        let store = StorePurchases(context:nil, callback: { _,_ in })
        defer { store.stop() }
        store.refresh()
        try await waitUntil { store.operation == nil && store.ready && store.product != nil && store.trialProduct != nil }
        XCTAssertFalse(store.unlocked); XCTAssertNil(store.trialStart)
        XCTAssertEqual(store.product?.price, Decimal(3)); XCTAssertEqual(store.trialProduct?.price, Decimal(0))
        try await session.setSimulatedError(.generic(.networkError(URLError(.notConnectedToInternet))), forAPI: .purchase)
        store.purchase(in:window); try await waitUntil { !store.busy }
        XCTAssertFalse(store.unlocked); XCTAssertEqual(store.message,"purchaseFailed")
        try await session.setSimulatedError(nil, forAPI: .purchase)
        try await session.setSimulatedError(.verification(.invalidSignature), forAPI: .verification)
        store.purchase(in:window); try await waitUntil { !store.busy }
        XCTAssertFalse(store.unlocked); XCTAssertEqual(store.message,"verification")
        session.clearTransactions()
        try await session.setSimulatedError(nil, forAPI: .verification)
        store.purchase(trial:true, in:window)
        try await waitUntil { !store.busy }
        print("TRIAL_RESULT", store.message, "transactions", session.allTransactions().count)
        XCTAssertNotNil(store.trialStart)
        XCTAssertTrue(TrialPolicy.active(start:store.trialStart,now:store.effectiveNow()))
        let originalStart = store.trialStart
        let restored = StorePurchases(context:nil, callback:{_,_ in})
        defer { restored.stop() }
        await restored.entitlements()
        XCTAssertEqual(restored.trialStart, originalStart) // Reopening cannot restart the trial.
        restored.restore(); try await waitUntil { !restored.busy }
        XCTAssertEqual(restored.trialStart, originalStart)
        store.purchase(in:window)
        try await waitUntil { !store.busy && store.unlocked }
        await restored.entitlements(); XCTAssertTrue(restored.unlocked)
        let paid = try XCTUnwrap(session.allTransactions().first { $0.productIdentifier == StorePurchases.productID })
        try session.refundTransaction(identifier:paid.identifier)
        try await waitUntil { !store.unlocked }
        XCTAssertEqual(store.trialStart, originalStart)
        session.askToBuyEnabled = true
        store.purchase(in:window); try await waitUntil { !store.busy }
        XCTAssertEqual(store.message,"pending"); XCTAssertFalse(store.unlocked)
        let pending = try XCTUnwrap(session.allTransactions().last { $0.productIdentifier == StorePurchases.productID && $0.state == .deferred })
        try session.approveAskToBuyTransaction(identifier:pending.identifier)
        try await waitUntil { store.unlocked }
        restored.restore(); try await waitUntil { !restored.busy }
        XCTAssertTrue(restored.unlocked)
        try await session.setSimulatedError(.generic(.networkError(URLError(.notConnectedToInternet))), forAPI: .loadProducts)
        store.refresh(); try await waitUntil { store.operation == nil }
        XCTAssertTrue(store.unlocked); XCTAssertNil(store.product)
        try await session.setSimulatedError(nil, forAPI: .loadProducts)
        try await session.setSimulatedError(.generic(.networkError(URLError(.notConnectedToInternet))), forAPI: .appStoreSync)
        store.restore(); try await waitUntil { !store.busy }
        XCTAssertTrue(store.unlocked); XCTAssertEqual(store.message,"restoreFailed")
        try await session.setSimulatedError(nil, forAPI: .appStoreSync)
    }
}
