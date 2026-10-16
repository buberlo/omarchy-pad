import XCTest
@testable import DesktopKit

final class LinuxTextInputModeTests: XCTestCase {
    override func setUp() {
        super.setUp()
        UserDefaults.standard.removeObject(forKey: LinuxTextInputMode.storageKey)
        UserDefaults.standard.removeObject(forKey: LinuxTextInputMode.keysAppsKey)
    }

    override func tearDown() {
        UserDefaults.standard.removeObject(forKey: LinuxTextInputMode.storageKey)
        UserDefaults.standard.removeObject(forKey: LinuxTextInputMode.keysAppsKey)
        super.tearDown()
    }

    private func set(_ mode: LinuxTextInputMode, apps: String? = nil) {
        UserDefaults.standard.set(mode.rawValue, forKey: LinuxTextInputMode.storageKey)
        if let apps { UserDefaults.standard.set(apps, forKey: LinuxTextInputMode.keysAppsKey) }
    }

    func testDefaultIsAutomaticWithTheGameListed() {
        XCTAssertEqual(LinuxTextInputMode.current, .automatic)
        XCTAssertEqual(LinuxTextInputMode.keysApps, ["hollow_knight.x86_64"])
        XCTAssertTrue(LinuxTextInputMode.sendsKeysDirectly(appID: "hollow_knight.x86_64"))
    }

    func testAutomaticOnlySendsKeysForListedApps() {
        set(.automatic, apps: "hollow_knight.x86_64,foot")
        XCTAssertTrue(LinuxTextInputMode.sendsKeysDirectly(appID: "hollow_knight.x86_64"))
        XCTAssertTrue(LinuxTextInputMode.sendsKeysDirectly(appID: "foot"))
        XCTAssertFalse(LinuxTextInputMode.sendsKeysDirectly(appID: "code"), "an editor keeps composing")
        XCTAssertFalse(LinuxTextInputMode.sendsKeysDirectly(appID: ""), "a view without an app id keeps composing")
    }

    func testAppIDsMatchWhateverTheirCase() {
        set(.automatic, apps: "Hollow_Knight.X86_64")
        XCTAssertTrue(LinuxTextInputMode.sendsKeysDirectly(appID: "hollow_knight.x86_64"))
        set(.automatic, apps: "hollow_knight.x86_64")
        XCTAssertTrue(LinuxTextInputMode.sendsKeysDirectly(appID: "Hollow_Knight.X86_64"))
    }

    func testAlwaysKeysSendsEveryAppItsKeys() {
        set(.keys, apps: "")
        XCTAssertTrue(LinuxTextInputMode.sendsKeysDirectly(appID: "hollow_knight.x86_64"))
        XCTAssertTrue(LinuxTextInputMode.sendsKeysDirectly(appID: "unknown-app"))
    }

    func testAlwaysComposeTextEvenForAListedApp() {
        set(.compose, apps: "hollow_knight.x86_64")
        XCTAssertFalse(LinuxTextInputMode.sendsKeysDirectly(appID: "hollow_knight.x86_64"))
    }

    func testAppListTrimsDropsEmptiesAndLowercases() {
        set(.automatic, apps: " Foot , ,CODE ,,")
        XCTAssertEqual(LinuxTextInputMode.keysApps, ["foot", "code"])
    }

    func testAnUnknownStoredModeFallsBackToAutomatic() {
        UserDefaults.standard.set("nonsense", forKey: LinuxTextInputMode.storageKey)
        XCTAssertEqual(LinuxTextInputMode.current, .automatic)
    }

    func testEveryModeHasATitle() {
        for mode in LinuxTextInputMode.allCases {
            XCTAssertFalse(mode.title.isEmpty, mode.rawValue)
        }
        XCTAssertEqual(LinuxTextInputMode.allCases.count, 3)
    }
}
