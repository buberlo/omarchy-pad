import DesktopKit
import SwiftUI
import UIKit

/// Entry point the Objective-C scene delegate looks up at runtime (by class name,
/// since SceneDelegate.m is also built into targets without Swift).
///
/// The build's variant (Info.plist `LinPadVariant`, see LinPadVariant in DesktopKit) decides
/// what the app shows: the ordinary Linux desktop, or — in the dedicated game app — Steam's
/// Big Picture UI full screen with no desktop around it. The same code is in both apps; only
/// signing and one build setting differ (installation/steam-app).
@objc(DesktopBridge)
final class DesktopBridge: NSObject {
    /// Set to NO (Settings app, or `defaults write`) to get the classic single-terminal UI back.
    static let enabledDefaultsKey = "desktop.enabled"

    @objc static var isEnabled: Bool {
        UserDefaults.standard.object(forKey: enabledDefaultsKey) as? Bool ?? true
    }

    /// linpad:// links (SceneDelegate); the desktop asks before acting on them.
    @MainActor
    @objc static func openURL(_ url: URL) -> Bool {
        LinPadLinkInbox.shared.receive(url)
    }

    // The host is created once: switching between game mode and the desktop must not restart
    // the Linux system — the guest session keeps running and the new bridge reconnects to it.
    @MainActor private static var host: ISHLinuxHost?
    @MainActor private static var hostingController: DesktopHostingController<AnyView>?

    @MainActor
    @objc static func makeRootViewController() -> UIViewController {
        // White terminals clash with the dark desktop; only overrides the untouched default.
        let preferences = UserPreferences.shared()
        if preferences.colorScheme == .ColorSchemeMatchSystem {
            preferences.colorScheme = .ColorSchemeAlwaysDark
        }
        let host = linuxHost()
        let controller = DesktopHostingController(rootView: makeRoot(for: host))
        controller.view.backgroundColor = .black
        hostingController = controller
        return controller
    }

    /// The one `ISHLinuxHost` this app run uses, with the bridges that hang off it.
    @MainActor
    private static func linuxHost() -> ISHLinuxHost {
        if let host { return host }
        let host = ISHLinuxHost()
        self.host = host
        // Before anything else can crash: how the last session ended, MetricKit, the marker.
        DiagnosticsCenter.shared.beginSession(host: host)
        // Waits for the guest's PulseAudio FIFO, which the Linux session (ishwl-session)
        // creates; until sound plays it holds no audio session.
        ISHAudioBridge.shared.start(guestRoot: host.guestRootURL)
        // Microphone for Linux apps: idle (no prompt, no indicator) until one records.
        ISHMicBridge.shared.start(guestRoot: host.guestRootURL)
        return host
    }

    /// The variant's default screen, unless the user asked for the desktop in the game app.
    @MainActor
    private static func makeRoot(for host: ISHLinuxHost) -> AnyView {
        let wantsGameMode = LinPadVariant.current().isGameMode
            && (SessionMode.stored() ?? .game) == .game
        if wantsGameMode {
            return AnyView(GameModeRootView(host: host, apps: BuiltinApps.all(),
                                            systemControls: ISHSystemControls(),
                                            configuration: .steam) {
                switchToDesktop()
            })
        }
        return AnyView(DesktopRootView(host: host, apps: BuiltinApps.all(),
                                       systemControls: ISHSystemControls()))
    }

    /// "Desktop" in the game bar. The Linux system keeps running; only the UI is replaced, and
    /// the next start shows the desktop until the user turns "Boot into Game Mode" back on.
    @MainActor
    @objc static func switchToDesktop() {
        SessionMode.store(.desktop)
        guard let hostingController, let host else { return }
        hostingController.rootView = makeRoot(for: host)
    }
}

/// The desktop's own panel replaces the iOS status bar.
private final class DesktopHostingController<Content: View>: UIHostingController<Content> {
    override var prefersStatusBarHidden: Bool { true }
    // UIHostingController otherwise lets an embedded TerminalViewController decide.
    override var childForStatusBarHidden: UIViewController? { nil }
    override var childForStatusBarStyle: UIViewController? { nil }
    override var prefersHomeIndicatorAutoHidden: Bool { true }
}

/// Quick-settings hooks into the iSH app: the Linux audio bridge's volume. The kernel runs
/// inside this process, so the guest cannot be rebooted on its own.
@MainActor
final class ISHSystemControls: DesktopSystemControls {
    var volume: Float? {
        get { ISHAudioBridge.shared.volume }
        set { ISHAudioBridge.shared.volume = newValue ?? 1 }
    }

    var isMuted: Bool {
        get { ISHAudioBridge.shared.isMuted }
        set { ISHAudioBridge.shared.isMuted = newValue }
    }

    let canRebootLinux = false

    func rebootLinux() async {}
}
