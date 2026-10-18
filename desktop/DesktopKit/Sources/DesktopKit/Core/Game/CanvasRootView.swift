import SwiftUI

/// The Omarchy canvas: one guest Wayland surface (the Omarchy desktop) edge to edge, with no
/// desktop around it. It is the game shell's machinery with the Omarchy configuration -- the
/// same guest boot, the same claim of one toplevel by app id, and the same reveal strip on
/// the top edge -- but no Steam modes and no Steam copy.
///
/// "Desktop" on the reveal strip switches to the ordinary LinPad desktop without restarting
/// Linux; the app stores that choice, exactly as the game app does.
public struct CanvasRootView: View {
    private let host: any LinuxHost
    private let apps: [DesktopAppDescriptor]
    private let systemControls: any DesktopSystemControls
    private let configuration: GameModeConfiguration
    private let onRequestDesktop: (() -> Void)?

    public init(host: any LinuxHost,
                apps: [DesktopAppDescriptor],
                systemControls: any DesktopSystemControls,
                configuration: GameModeConfiguration = .omarchy,
                onRequestDesktop: (() -> Void)? = nil) {
        self.host = host
        self.apps = apps
        self.systemControls = systemControls
        self.configuration = configuration
        self.onRequestDesktop = onRequestDesktop
    }

    public var body: some View {
        GameModeRootView(host: host,
                         apps: apps,
                         systemControls: systemControls,
                         configuration: configuration,
                         onRequestDesktop: onRequestDesktop)
    }
}
