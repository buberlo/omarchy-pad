import XCTest
@testable import DesktopKit

/// A guest the test drives: it answers the Steam probe, and serves the two launcher files
/// (status and fallback marker) from memory. `launched` records nothing: the coordinator's
/// own `lastCommand` is what the tests assert on, because the real spawn goes over ishwl.
@MainActor
private final class GameModeTestHost: LinuxHost {
    let hostName = "test"
    let homeDirectory = "/root"

    /// What the Steam probe should answer: true means "Steam is installed".
    var steamInstalled = true
    var files: [String: Data] = [:]
    var commands: [String] = []

    func run(_ command: String, cwd: String?, stdin: Data?) async -> CommandResult {
        commands.append(command)
        if command.contains("linpad-steam-present") {
            return CommandResult(stdout: steamInstalled ? "linpad-steam-present\n" : "")
        }
        return CommandResult(stdout: "")
    }

    func stream(_ command: String, cwd: String?, onOutput: @escaping @MainActor (String) -> Void) async -> Int32 {
        0
    }

    func listDirectory(_ path: String) async throws -> [FileEntry] { [] }

    func readFile(_ path: String) async throws -> Data {
        guard let data = files[path] else { throw LinuxHostError.invalidPath(path) }
        return data
    }

    func writeFile(_ path: String, data: Data) async throws { files[path] = data }

    func makeTerminalViewController(command: String?, cwd: String?) -> UIViewController { UIViewController() }
}

/// Records what the coordinator asked the session to run.
@MainActor
private final class RecordingLauncher: GameCommandLauncher {
    var commands: [String] = []
    func launch(command: String) { commands.append(command) }
}

@MainActor
final class GameModeTests: XCTestCase {
    private var host: GameModeTestHost!
    private var launcher: RecordingLauncher!
    private var defaults: UserDefaults!
    private var suiteName: String!

    override func setUp() {
        super.setUp()
        host = GameModeTestHost()
        launcher = RecordingLauncher()
        suiteName = "GameModeTests.\(UUID().uuidString)"
        defaults = UserDefaults(suiteName: suiteName)
    }

    override func tearDown() {
        defaults.removePersistentDomain(forName: suiteName)
        super.tearDown()
    }

    private func makeCoordinator(geometry: CGSize = CGSize(width: 1366, height: 1024),
                                 timeout: Duration = .seconds(180),
                                 configuration: GameModeConfiguration? = nil) -> GameModeCoordinator {
        var config = configuration ?? GameModeConfiguration()
        config.startupTimeout = timeout
        return GameModeCoordinator(host: host, launcher: launcher, configuration: config,
                                   defaults: defaults, screenPointSize: { geometry })
    }

    private func surface(id: UInt32, appID: String) -> LinuxSurface {
        let surface = LinuxSurface(id: id, kind: .toplevel, parentID: 0)
        surface.appID = appID
        return surface
    }

    // MARK: - Variant

    func testVariantParsing() {
        XCTAssertEqual(LinPadVariant.parse("steam"), .steam)
        XCTAssertEqual(LinPadVariant.parse(" STEAM "), .steam)
        XCTAssertEqual(LinPadVariant.parse("omarchy"), .omarchy)
        XCTAssertEqual(LinPadVariant.parse(" OMARCHY "), .omarchy)
        XCTAssertEqual(LinPadVariant.parse("desktop"), .desktop)
        // Anything else is the shipping desktop app: a build without the key must not change.
        XCTAssertEqual(LinPadVariant.parse(nil), .desktop)
        XCTAssertEqual(LinPadVariant.parse(""), .desktop)
        XCTAssertEqual(LinPadVariant.parse("bogus"), .desktop)
        XCTAssertTrue(LinPadVariant.steam.isGameMode)
        XCTAssertFalse(LinPadVariant.desktop.isGameMode)
    }

    func testOmarchyVariantIsACanvasNotAGame() {
        XCTAssertEqual(LinPadVariant.parse("omarchy").rootName, "omarchy")
        XCTAssertFalse(LinPadVariant.parse("omarchy").isGameMode)
        XCTAssertTrue(LinPadVariant.parse("omarchy").isCanvasMode)
        XCTAssertFalse(LinPadVariant.steam.isCanvasMode)
        XCTAssertFalse(LinPadVariant.desktop.isCanvasMode)
    }

    /// The test bundle has no LinPadVariant key, which is exactly the fallback case.
    func testCurrentVariantDefaultsToDesktop() {
        XCTAssertEqual(LinPadVariant.current(bundle: Bundle(for: Self.self)), .desktop)
        XCTAssertEqual(LinPadVariant.current(bundle: .main), .desktop)
    }

    // MARK: - Starting Steam

    func testBeginWithoutSteamReportsIt() async {
        host.steamInstalled = false
        let coordinator = makeCoordinator()
        await coordinator.begin()
        XCTAssertEqual(coordinator.phase, .stopped(.steamMissing))
        XCTAssertNil(coordinator.lastCommand)
    }

    func testBeginStartsBigPictureWithTheVerifiedGeometry() async {
        let coordinator = makeCoordinator()
        await coordinator.begin()
        XCTAssertEqual(coordinator.lastCommand, "steam-bigpicture")
        XCTAssertEqual(launcher.commands, ["steam-bigpicture"])
        XCTAssertEqual(coordinator.phase, .startingSteam)
        XCTAssertNotNil(coordinator.startedAt)
        XCTAssertTrue(host.commands.contains { $0.contains("linpad-steam-present") })
        coordinator.stop()
    }

    func testWithoutABridgeTheGameCannotStart() async {
        let coordinator = GameModeCoordinator(host: host, launcher: nil, configuration: GameModeConfiguration(),
                                              defaults: defaults, screenPointSize: { CGSize(width: 1024, height: 768) })
        await coordinator.begin()
        XCTAssertEqual(coordinator.phase, .stopped(.sessionUnavailable("This build has no Linux GUI bridge.")))
    }

    func testClassicPreferenceAddsTheFlagAndPersists() async {
        let coordinator = makeCoordinator()
        coordinator.useClassicUI()
        XCTAssertEqual(coordinator.lastCommand, "steam-bigpicture --classic")
        XCTAssertEqual(launcher.commands, ["steam-bigpicture --classic"])
        XCTAssertTrue(defaults.bool(forKey: GameModeCoordinator.classicUIKey))

        // A second coordinator (a relaunch) starts classic without being told again, with its
        // own launcher: the first one's commands are not this half's business.
        let relauncher = RecordingLauncher()
        let relaunched = GameModeCoordinator(host: host, launcher: relauncher,
                                             configuration: GameModeConfiguration(),
                                             defaults: defaults,
                                             screenPointSize: { CGSize(width: 1366, height: 1024) })
        XCTAssertTrue(relaunched.prefersClassicUI)
        await relaunched.begin()
        XCTAssertEqual(relaunched.lastCommand, "steam-bigpicture --classic")
        XCTAssertEqual(relauncher.commands, ["steam-bigpicture --classic"])
        relaunched.stop()
    }

    func testBigPictureFlagOverridesTheStoredPreference() async {
        let coordinator = makeCoordinator()
        coordinator.useClassicUI()
        coordinator.useBigPicture()
        XCTAssertEqual(coordinator.lastCommand, "steam-bigpicture --bigpicture")
        XCTAssertFalse(defaults.bool(forKey: GameModeCoordinator.classicUIKey))
        coordinator.stop()
    }

    func testFillScreenPassesTheScreenSize() async {
        let coordinator = makeCoordinator(geometry: CGSize(width: 1365.6, height: 1023.4))
        coordinator.fillsScreen = true
        await coordinator.begin()
        // Rounded down to even numbers, as Xvnc and the viewer both need.
        XCTAssertEqual(coordinator.lastCommand, "steam-bigpicture --geometry 1364x1022")
        XCTAssertEqual(launcher.commands, ["steam-bigpicture --geometry 1364x1022"])
        XCTAssertEqual(coordinator.geometryDescription, "1364x1022")
        coordinator.stop()
        XCTAssertTrue(defaults.bool(forKey: GameModeCoordinator.fillScreenKey))
    }

    func testUnusableScreenSizeKeepsTheVerifiedDefault() {
        for size in [CGSize(width: 137, height: 100), CGSize(width: 5000, height: 5000)] {
            let coordinator = makeCoordinator(geometry: size)
            coordinator.fillsScreen = true
            XCTAssertNil(coordinator.launchGeometry())
            XCTAssertEqual(coordinator.geometryDescription, "1024x700 (verified)")
        }
    }

    func testConfigurationCanDefaultToTheClassicUI() async {
        var config = GameModeConfiguration()
        config.usesClassicUIByDefault = true
        let coordinator = makeCoordinator(configuration: config)
        await coordinator.begin()
        XCTAssertEqual(coordinator.lastCommand, "steam-bigpicture --classic")
        coordinator.stop()
    }

    // MARK: - The Omarchy canvas

    func testOmarchyConfigurationDescribesTheCanvasSession() {
        let config = GameModeConfiguration.omarchy
        XCTAssertEqual(config.subjectName, "Omarchy")
        XCTAssertEqual(config.command, "omarchy-pad-session")
        XCTAssertEqual(config.launcherPaths, ["/usr/local/bin/omarchy-pad-session"])
        XCTAssertEqual(config.claimedAppIDs, ["omarchy-pad"])
        XCTAssertEqual(config.startupTimeout, .seconds(300))
        XCTAssertEqual(config.statusPath, "/tmp/omarchy-pad/status")
        XCTAssertEqual(config.fallbackMarkerPath, "/tmp/omarchy-pad/error")
        XCTAssertNil(config.bigPictureFlag)
        XCTAssertNil(config.classicFlag)
        XCTAssertNil(config.geometryFlag)
        XCTAssertFalse(config.usesClassicUIByDefault)
        XCTAssertFalse(LinPadVariant.omarchy.isGameMode)
        XCTAssertTrue(LinPadVariant.omarchy.isCanvasMode)
    }

    func testOmarchyClaimsItsOwnSurface() {
        let coordinator = makeCoordinator(configuration: .omarchy)
        XCTAssertTrue(coordinator.claims(appID: "omarchy-pad"))
        XCTAssertTrue(coordinator.claims(appID: "Omarchy-Pad"))
        XCTAssertFalse(coordinator.claims(appID: "Steam"))
    }

    func testOmarchyLaunchesTheSessionWithNoSteamFlags() async {
        let coordinator = makeCoordinator(timeout: .seconds(300), configuration: .omarchy)
        // Even with the persisted screen-size preference on, the canvas has no --geometry.
        coordinator.fillsScreen = true
        await coordinator.begin()
        XCTAssertEqual(coordinator.lastCommand, "omarchy-pad-session")
        XCTAssertEqual(launcher.commands, ["omarchy-pad-session"])
        XCTAssertFalse(coordinator.lastCommand?.contains("--bigpicture") ?? true)
        XCTAssertFalse(coordinator.lastCommand?.contains("--classic") ?? true)
        XCTAssertFalse(coordinator.lastCommand?.contains("--geometry") ?? true)
        coordinator.stop()
    }

    func testFailureCopyUsesTheConfiguredSubject() {
        XCTAssertEqual(GameModeFailure.steamMissing.title(subject: "Omarchy"), "Omarchy is not installed")
        XCTAssertTrue(GameModeFailure.steamNotResponding.detail(subject: "Omarchy").contains("Omarchy"))
        XCTAssertTrue(GameModeFailure.steamStopped("").detail(subject: "Omarchy").contains("Omarchy"))
        // The Steam copy keeps the wording the game app has always shown.
        XCTAssertEqual(GameModeFailure.steamMissing.title, "Steam is not installed")
        XCTAssertEqual(GameModeFailure.steamStopped("").title, "Steam closed")
    }

    // MARK: - Claiming the game surface

    func testOnlyTheGameAppIDIsClaimed() {
        let coordinator = makeCoordinator()
        XCTAssertTrue(coordinator.claims(appID: "Steam"))
        XCTAssertTrue(coordinator.claims(appID: "steam"))
        XCTAssertFalse(coordinator.claims(appID: "Firefox"))
        XCTAssertFalse(coordinator.claims(appID: ""))
    }

    func testTheFirstGameSurfaceIsClaimedAndTheRestAreNot() {
        let coordinator = makeCoordinator()
        let steam = surface(id: 7, appID: "Steam")
        let other = surface(id: 8, appID: "Firefox")
        XCTAssertTrue(coordinator.surfaceMapped(steam))
        XCTAssertEqual(coordinator.phase, .presenting(surfaceID: 7))
        // A second Steam toplevel while one is on screen, and another app's window: ignored,
        // because the game shell has no window layer to put them in.
        XCTAssertFalse(coordinator.surfaceMapped(surface(id: 9, appID: "Steam")))
        XCTAssertFalse(coordinator.surfaceMapped(other))
    }

    func testUnmappingAnotherSurfaceIsIgnored() {
        let coordinator = makeCoordinator()
        _ = coordinator.surfaceMapped(surface(id: 7, appID: "Steam"))
        XCTAssertFalse(coordinator.surfaceUnmapped(surface(id: 8, appID: "Firefox")))
        XCTAssertEqual(coordinator.phase, .presenting(surfaceID: 7))
    }

    func testSteamClosingStopsWithTheLauncherStatus() async {
        let coordinator = makeCoordinator()
        host.files[GameModeConfiguration().statusPath] = Data("exited 0\n".utf8)
        let steam = surface(id: 7, appID: "Steam")
        XCTAssertTrue(coordinator.surfaceMapped(steam))
        coordinator.stop()
        XCTAssertTrue(coordinator.surfaceUnmapped(steam))
        // The status is refined from the file, so let that land.
        try? await Task.sleep(for: .milliseconds(80))
        XCTAssertEqual(coordinator.launcherStatus, "exited 0")
        XCTAssertEqual(coordinator.phase, .stopped(.steamStopped("exited 0")))
        XCTAssertTrue(GameModeFailure.steamStopped("exited 0").suggestsClassicUI)
    }

    func testSessionEndingReportsTheSession() async {
        let coordinator = makeCoordinator()
        await coordinator.begin()
        coordinator.sessionEnded()
        guard case .stopped(.sessionUnavailable(let detail)) = coordinator.phase else {
            return XCTFail("expected a session failure, got \(coordinator.phase)")
        }
        XCTAssertTrue(detail.contains("session stopped"))
    }

    /// A missing Steam is a fact about the system, not a failure of the session: it must not be
    /// overwritten by an unmap or a session end that follows.
    func testSteamMissingSurvivesTheSessionEnding() async {
        host.steamInstalled = false
        let coordinator = makeCoordinator()
        await coordinator.begin()
        coordinator.sessionEnded()
        XCTAssertEqual(coordinator.phase, .stopped(.steamMissing))
    }

    // MARK: - Launcher files

    func testFallbackMarkerIsSurfaced() async {
        let config = GameModeConfiguration()
        host.files[config.fallbackMarkerPath] = Data()
        host.files[config.statusPath] = Data("desktop-fallback\n".utf8)
        let coordinator = makeCoordinator()
        await coordinator.refreshLauncherFiles()
        XCTAssertTrue(coordinator.usedClassicUIFallback)
        XCTAssertEqual(coordinator.launcherStatus, "desktop-fallback")
    }

    func testEmptyStatusFileKeepsTheLastValue() async {
        let config = GameModeConfiguration()
        host.files[config.statusPath] = Data("desktop-fallback\n".utf8)
        let coordinator = makeCoordinator()
        await coordinator.refreshLauncherFiles()
        host.files[config.statusPath] = Data("\n".utf8)
        await coordinator.refreshLauncherFiles()
        XCTAssertEqual(coordinator.launcherStatus, "desktop-fallback")
    }

    // MARK: - Watchdog

    func testNoWindowInTimeIsReported() async {
        let coordinator = makeCoordinator(timeout: .milliseconds(60))
        await coordinator.begin()
        XCTAssertEqual(coordinator.phase, .startingSteam)
        try? await Task.sleep(for: .milliseconds(200))
        XCTAssertEqual(coordinator.phase, .stopped(.steamNotResponding))
    }

    /// The classic-UI retry gets one more window before the shell gives up.
    func testTheFallbackGetsOneMoreWindow() async {
        var config = GameModeConfiguration()
        config.startupTimeout = .milliseconds(120)
        host.files[config.fallbackMarkerPath] = Data()
        let coordinator = GameModeCoordinator(host: host, launcher: launcher, configuration: config,
                                              defaults: defaults, screenPointSize: { CGSize(width: 1024, height: 768) })
        coordinator.startSteam()
        XCTAssertEqual(coordinator.phase, .startingSteam)
        // First watchdog: the marker is there, so the shell waits again instead of failing.
        try? await Task.sleep(for: .milliseconds(180))
        XCTAssertEqual(coordinator.phase, .startingSteam)
        XCTAssertTrue(coordinator.usedClassicUIFallback)
        try? await Task.sleep(for: .milliseconds(180))
        XCTAssertEqual(coordinator.phase, .stopped(.steamNotResponding))
    }

    // MARK: - Mode and failures

    func testSessionModeRoundTrip() {
        SessionMode.store(.desktop, in: defaults)
        XCTAssertEqual(SessionMode.stored(in: defaults), .desktop)
        SessionMode.store(.game, in: defaults)
        XCTAssertEqual(SessionMode.stored(in: defaults), .game)
        defaults.removeObject(forKey: SessionMode.storageKey)
        XCTAssertNil(SessionMode.stored(in: defaults))
    }

    func testFailureMessagesAreUserFacing() {
        XCTAssertEqual(GameModeFailure.steamMissing.title, "Steam is not installed")
        XCTAssertFalse(GameModeFailure.steamMissing.detail.isEmpty)
        XCTAssertFalse(GameModeFailure.steamMissing.suggestsClassicUI)
        XCTAssertTrue(GameModeFailure.steamNotResponding.suggestsClassicUI)
        XCTAssertFalse(GameModeFailure.sessionUnavailable("x").suggestsClassicUI)
        XCTAssertTrue(GameModeFailure.steamStopped("").detail.contains("Start it again"))
    }

    func testDesktopRequestRunsTheAppCallback() {
        let coordinator = makeCoordinator()
        var asked = 0
        coordinator.onRequestDesktop = { asked += 1 }
        coordinator.openDesktop()
        XCTAssertEqual(asked, 1)
    }

    /// The most consequential thing this type decides: an app that boots the wrong root mounts a
    /// different Linux system than the one it shipped. The desktop's Alpine root is the default for
    /// anything unrecognised, and an unknown value must never invent a root name.
    func testEachVariantKnowsWhichRootItBoots() {
        XCTAssertEqual(LinPadVariant.parse("steam").rootName, "arch")
        XCTAssertEqual(LinPadVariant.parse("omarchy").rootName, "omarchy")
        XCTAssertEqual(LinPadVariant.parse("desktop").rootName, "default")
        XCTAssertEqual(LinPadVariant.parse(nil).rootName, "default")
        XCTAssertEqual(LinPadVariant.parse("").rootName, "default")
        XCTAssertEqual(LinPadVariant.parse(" ARCH ").rootName, "default", "unknown values must not invent a root")
        XCTAssertEqual(LinPadVariant.current(bundle: Bundle(for: type(of: self))).rootName, "default",
                       "a test bundle has no LinPadVariant key, so it is the desktop")
        XCTAssertEqual(LinPadVariant.steam.rootName, "arch")
        XCTAssertEqual(LinPadVariant.omarchy.rootName, "omarchy")
        XCTAssertNotEqual(LinPadVariant.desktop.rootName, LinPadVariant.steam.rootName,
                          "the two apps must never share a root name")
        XCTAssertNotEqual(LinPadVariant.omarchy.rootName, LinPadVariant.steam.rootName,
                          "the canvas and the game app must never share a root name")
        XCTAssertNotEqual(LinPadVariant.omarchy.rootName, LinPadVariant.desktop.rootName,
                          "the canvas must never mount the desktop's Alpine root")
    }
}
