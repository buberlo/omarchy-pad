import Foundation
import UIKit

/// The one thing the game shell needs from the GUI bridge: hand a command line to the
/// session. LinuxGUIBridge is the real one (its `spawn` over ishwl's events FIFO); tests
/// substitute a recorder, which is why the coordinator's start path is testable at all.
@MainActor
protocol GameCommandLauncher: AnyObject {
    func launch(command: String)
}

extension LinuxGUIBridge: GameCommandLauncher {}

/// The game app's policy, in one testable place: after the guest has booted, start Steam's
/// Big Picture UI, show its toplevel full screen, and cover everything that can go wrong on
/// the way. The view (`GameModeRootView`) only draws what this reports.
///
/// The heavy lifting stays in the guest: `steam-bigpicture` (installed by the repair kit)
/// starts the verified Steam launcher and falls back to the classic UI once if this Steam
/// build refuses `-gamepadui`. This type reads what it did from the two files it writes in
/// the guest's /tmp, so the panel and the bar can say it too.
@Observable @MainActor
public final class GameModeCoordinator {
    public private(set) var phase: GameModePhase = .waitingForSystem {
        didSet { notePhase(String(describing: phase)) }
    }
    /// The last line /tmp/steam-bigpicture-status held (bigpicture, desktop-fallback, exited N…).
    public private(set) var launcherStatus: String?
    /// steam-bigpicture reported that the gamepad UI failed and it started the classic UI.
    public private(set) var usedClassicUIFallback = false

    /// Persisted: render at the iPad's size instead of the verified 1024x700. Xvnc renders
    /// Steam in software, so the larger screen costs speed.
    public var fillsScreen: Bool {
        didSet { defaults.set(fillsScreen, forKey: Self.fillScreenKey) }
    }
    /// Persisted: start Steam's normal desktop UI rather than Big Picture.
    public var prefersClassicUI: Bool {
        didSet { defaults.set(prefersClassicUI, forKey: Self.classicUIKey) }
    }

    /// The app switches the window to the ordinary desktop; the mode is stored by the app.
    public var onRequestDesktop: (() -> Void)?

    static let fillScreenKey = "steam.fillScreen"
    static let classicUIKey = "steam.classicUI"

    /// The guest launcher's command line, as sent to the session, for tests and diagnostics.
    public private(set) var lastCommand: String?
    /// When the current start began; the shell counts from here while Steam comes up.
    public private(set) var startedAt: Date?

    private let host: any LinuxHost
    private weak var launcher: (any GameCommandLauncher)?
    private let configuration: GameModeConfiguration
    private let defaults: UserDefaults
    private let screenPointSize: () -> CGSize

    @ObservationIgnored private var claimedSurfaceID: UInt32?
    @ObservationIgnored private var launchGeneration = 0
    @ObservationIgnored private var watchdogExtensions = 0
    @ObservationIgnored private var watchdog: Task<Void, Never>?
    @ObservationIgnored private var statusPoll: Task<Void, Never>?

    init(host: any LinuxHost,
         launcher: (any GameCommandLauncher)?,
         configuration: GameModeConfiguration = .steam,
         defaults: UserDefaults = .standard,
         screenPointSize: @escaping () -> CGSize = { UIScreen.main.bounds.size }) {
        self.host = host
        self.launcher = launcher
        self.configuration = configuration
        self.defaults = defaults
        self.screenPointSize = screenPointSize
        fillsScreen = defaults.bool(forKey: Self.fillScreenKey)
        prefersClassicUI = defaults.bool(forKey: Self.classicUIKey)
    }

    // MARK: - Start

    /// Called once the system is booted: look for Steam, then start it.
    public func begin() async {
        guard case .waitingForSystem = phase else { return }
        phase = .checkingSteam
        await refreshLauncherFiles()
        guard await steamIsInstalled() else {
            phase = .stopped(.steamMissing)
            return
        }
        launch(forceBigPicture: false)
    }

    /// The gamepad UI is what failed; the classic UI is the same client with another UI mode.
    public func useClassicUI() {
        prefersClassicUI = true
        launch(forceBigPicture: false)
    }

    /// "--bigpicture" also forgets an earlier fallback in the guest.
    public func useBigPicture() {
        prefersClassicUI = false
        launch(forceBigPicture: true)
    }

    public func startSteam(forceBigPicture: Bool = false) {
        launch(forceBigPicture: forceBigPicture)
    }

    public func openDesktop() {
        onRequestDesktop?()
    }

    /// The host refused to boot the Linux system because the other LinPad app is running it.
    /// Nothing here can fix that, so the shell says so instead of blaming Steam.
    public func blockForBusySystem(_ reason: String) {
        watchdog?.cancel()
        statusPoll?.cancel()
        phase = .stopped(.linuxSystemBusy(reason))
    }

    /// The shell is going away (mode switch or app teardown).
    public func stop() {
        watchdog?.cancel()
        statusPoll?.cancel()
    }

    // MARK: - Surfaces

    /// In game mode every toplevel is handed here: the Steam one is shown full screen and
    /// the rest are left alone, because the game shell has no window layer to show them in.
    public func claims(appID: String) -> Bool {
        let lowered = appID.lowercased()
        return configuration.claimedAppIDs.contains { $0.lowercased() == lowered }
    }

    /// Returns true when the surface belongs to the game and must not become a desktop window.
    /// Internal: LinuxSurface is this module's own type, and only DesktopController calls these.
    func surfaceMapped(_ surface: LinuxSurface) -> Bool {
        guard claims(appID: surface.appID), claimedSurfaceID == nil else { return false }
        claimedSurfaceID = surface.id
        watchdog?.cancel()
        phase = .presenting(surfaceID: surface.id)
        Task { [weak self] in await self?.refreshLauncherFiles() }
        return true
    }

    func surfaceUnmapped(_ surface: LinuxSurface) -> Bool {
        guard surface.id == claimedSurfaceID else { return false }
        claimedSurfaceID = nil
        // The session ending tells a better story than its windows going away; announce the
        // stop right away (the panel is on screen now) and refine the wording from the file.
        if case .stopped(.sessionUnavailable) = phase { return true }
        phase = .stopped(.steamStopped(launcherStatus ?? ""))
        Task { [weak self] in
            guard let self else { return }
            await self.refreshLauncherFiles()
            if case .stopped(.steamStopped) = self.phase {
                self.phase = .stopped(.steamStopped(self.launcherStatus ?? ""))
            }
        }
        return true
    }

    /// ishwl died; every surface is gone and a new start restarts the session.
    public func sessionEnded() {
        claimedSurfaceID = nil
        if case .stopped(.steamMissing) = phase { return }
        // A busy system never booted in the first place: that message is still the right one.
        if case .stopped(.linuxSystemBusy) = phase { return }
        phase = .stopped(.sessionUnavailable("""
            The Linux GUI session stopped. Start Steam again to bring it back, or open the \
            desktop and look at /tmp/ishwl.log.
            """))
    }

    // MARK: - Launching

    private func launch(forceBigPicture: Bool) {
        launchGeneration += 1
        watchdogExtensions = 0
        claimedSurfaceID = nil
        usedClassicUIFallback = false
        var command = configuration.steamCommand
        if let geometry = launchGeometry() { command += " --geometry \(geometry)" }
        if forceBigPicture {
            command += " --bigpicture"
        } else if prefersClassicUI || configuration.usesClassicUIByDefault {
            command += " --classic"
        }
        lastCommand = command
        guard let launcher else {
            phase = .stopped(.sessionUnavailable("This build has no Linux GUI bridge."))
            return
        }
        launcher.launch(command: command)
        phase = .startingSteam
        startedAt = Date()
        startWatchdog(generation: launchGeneration)
        startStatusPolling()
    }

    private func startWatchdog(generation: Int) {
        watchdog?.cancel()
        watchdog = Task { [weak self] in
            guard let self else { return }
            try? await Task.sleep(for: self.configuration.startupTimeout)
            guard !Task.isCancelled else { return }
            await self.watchdogFired(generation: generation)
        }
    }

    private func watchdogFired(generation: Int) async {
        guard generation == launchGeneration, case .startingSteam = phase else { return }
        await refreshLauncherFiles()
        // The classic UI is on its way; give it one more window before giving up.
        if usedClassicUIFallback, watchdogExtensions == 0 {
            watchdogExtensions += 1
            startWatchdog(generation: generation)
            return
        }
        phase = .stopped(.steamNotResponding)
        exportDiagnosticsForFailure()
    }

    /// Steam never appeared, so this run is over -- leave its evidence behind. The game app has no
    /// Settings UI, so without this the kernel log, the emulator log (which is where unsupported
    /// system calls show up), the session log and any crash report are unreachable on a device: this
    /// export lands in the app's Documents, and a container copy can read Documents even though it
    /// cannot read `roots/`, where the guest keeps its own logs.
    private func exportDiagnosticsForFailure() {
        Task { [host] in
            let model = DiagnosticsExportModel(host: host)
            await model.collect()
            model.export()
        }
    }

    private func startStatusPolling() {
        statusPoll?.cancel()
        statusPoll = Task { [weak self] in
            while !Task.isCancelled {
                guard let self else { return }
                await self.refreshLauncherFiles()
                if case .stopped = self.phase { return }
                try? await Task.sleep(for: .seconds(2))
            }
        }
    }

    private func steamIsInstalled() async -> Bool {
        let tests = configuration.launcherPaths.map { "[ -x \($0.shellQuoted) ]" }.joined(separator: " || ")
        let result = await host.run("(\(tests)) && echo linpad-steam-present")
        return result.stdout.contains("linpad-steam-present")
    }

    /// Reads what the launcher wrote in the guest's /tmp. Cheap: the files are host files of
    /// the shared guest filesystem, so this never runs a command in the emulator.
    public func refreshLauncherFiles() async {
        if let text = await guestText(at: configuration.statusPath) {
            let line = text.trimmingCharacters(in: .whitespacesAndNewlines)
            if !line.isEmpty { launcherStatus = line }
        }
        if await guestText(at: configuration.fallbackMarkerPath) != nil {
            usedClassicUIFallback = true
        }
    }

    private func guestText(at path: String) async -> String? {
        guard let data = try? await host.readFile(path) else { return nil }
        return String(data: data, encoding: .utf8)
    }

    // MARK: - Geometry

    /// nil keeps the launcher's verified 1024x700. "Fill screen" passes the iPad's points,
    /// clamped and rounded down to even numbers, because Xvnc and the viewer must agree.
    public func launchGeometry() -> String? {
        guard fillsScreen else { return nil }
        let size = screenPointSize()
        let width = Int(size.width.rounded(.down)) & ~1
        let height = Int(size.height.rounded(.down)) & ~1
        guard width >= 320, height >= 240, width <= 4096, height <= 4096 else { return nil }
        return "\(width)x\(height)"
    }

    /// What the bar shows for the current choice.
    public var geometryDescription: String {
        launchGeometry() ?? "1024x700 (verified)"
    }

    /// Game mode is invisible from outside the iPad: no Settings UI, the guest's logs live in `roots/`
    /// (outside what a container copy may read), and the screen cannot be captured without a developer
    /// disk image. So every phase change also appends a line to `game-mode-status.txt` in the app's
    /// Documents -- the one place a container copy *can* read. It turns "did Steam appear?" from an
    /// inference drawn from a missing export into a fact that can be read off the device.
    private func notePhase(_ text: String) {
        let documents = FileManager.default.urls(for: .documentDirectory, in: .userDomainMask)[0]
        let url = documents.appendingPathComponent("game-mode-status.txt")
        let line = "\(ISO8601DateFormatter().string(from: Date())) \(text)\n"
        if let handle = try? FileHandle(forWritingTo: url) {
            handle.seekToEndOfFile()
            handle.write(Data(line.utf8))
            try? handle.close()
        } else {
            try? line.write(to: url, atomically: true, encoding: .utf8)
        }
    }
}
