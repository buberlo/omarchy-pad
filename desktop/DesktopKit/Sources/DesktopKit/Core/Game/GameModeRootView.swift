import SwiftUI
import UIKit

/// The whole screen of the dedicated game app: Linux boots behind a splash, then Steam's
/// toplevel fills the screen with nothing around it. A hairline strip at the top edge
/// reveals the few controls that must exist (restart, classic UI, screen size, desktop).
///
/// It reuses the desktop's `DesktopController`, so the guest session, input, clipboard,
/// audio and diagnostics behave exactly as they do on the desktop; only the presentation
/// differs (`DesktopController.gameMode`).
public struct GameModeRootView: View {
    @State private var controller: DesktopController
    @State private var coordinator: GameModeCoordinator
    @State private var isBarRevealed = false
    @State private var now = Date()

    private let configuration: GameModeConfiguration

    public init(host: any LinuxHost,
                apps: [DesktopAppDescriptor],
                systemControls: any DesktopSystemControls,
                configuration: GameModeConfiguration = .steam,
                onRequestDesktop: (() -> Void)? = nil) {
        self.configuration = configuration
        let controller = DesktopController(host: host, apps: apps, systemControls: systemControls)
        let coordinator = GameModeCoordinator(host: host, launcher: controller.linux, configuration: configuration)
        coordinator.onRequestDesktop = onRequestDesktop
        // From here on every toplevel goes to the coordinator instead of a desktop window.
        controller.gameMode = coordinator
        _controller = State(initialValue: controller)
        _coordinator = State(initialValue: coordinator)
    }

    public var body: some View {
        ZStack {
            Color.black.ignoresSafeArea()
            content
            if let linux = controller.linux {
                LinuxPopupLayerHost(bridge: linux)
                    .ignoresSafeArea()
            }
            revealStrip
            if isBarRevealed {
                GameModeBar(controller: controller, coordinator: coordinator,
                            isRevealed: $isBarRevealed, configuration: configuration)
                    .transition(.move(edge: .top).combined(with: .opacity))
                    .zIndex(10)
            }
        }
        .ignoresSafeArea()
        .animation(.snappy(duration: 0.2), value: isBarRevealed)
        .task { await start() }
        .onAppear { UIApplication.shared.isIdleTimerDisabled = true }
        .onDisappear {
            UIApplication.shared.isIdleTimerDisabled = false
            coordinator.stop()
        }
        .sheet(isPresented: Binding(get: { controller.isFastModeHelpPresented },
                                    set: { controller.isFastModeHelpPresented = $0 })) {
            FastModeHelpSheet()
        }
        .onReceive(Timer.publish(every: 1, on: .main, in: .common).autoconnect()) { now = $0 }
        .environment(\.desktopController, controller)
    }

    // MARK: - Boot

    private func start() async {
        await controller.boot.run(host: controller.host, linux: controller.linux)
        // The other LinPad app holds the shared Linux system: no probe, no Steam, just say so.
        if let reason = (controller.host as? any LinuxSystemBusyReporting)?.sessionBusyReason {
            coordinator.blockForBusySystem(reason)
            return
        }
        controller.offerFastModeRetryIfFailed()
        await coordinator.begin()
    }

    // MARK: - Content

    @ViewBuilder
    private var content: some View {
        if !controller.boot.isFinished {
            BootSplash(controller: controller)
        } else {
            switch coordinator.phase {
            case .waitingForSystem, .checkingSteam:
                GameModeProgressPanel(title: "Starting the game system…",
                                      detail: "Linux is up; looking for Steam.",
                                      showsSpinner: true)
            case .startingSteam:
                GameModeProgressPanel(title: "Starting Steam…",
                                      detail: startingDetail,
                                      showsSpinner: true)
            case .presenting(let surfaceID):
                gameSurface(surfaceID)
            case .stopped(let failure):
                GameModeFailurePanel(failure: failure,
                                     coordinator: coordinator,
                                     controller: controller)
            }
        }
    }

    private var startingDetail: String {
        var lines = ["Big Picture (`-gamepadui`); the first start after an update takes longer."]
        if let started = coordinator.startedAt {
            lines.append("\(Int(now.timeIntervalSince(started))) s so far. The screen is \(coordinator.geometryDescription).")
        }
        if coordinator.usedClassicUIFallback {
            lines.append("Big Picture did not start, so Steam's classic UI is starting instead.")
        }
        return lines.joined(separator: " ")
    }

    @ViewBuilder
    private func gameSurface(_ surfaceID: UInt32) -> some View {
        if let view = controller.linux?.surface(withID: surfaceID)?.view {
            LinuxWindowContent(view: view)
                .ignoresSafeArea()
        } else {
            GameModeProgressPanel(title: "Steam is starting…",
                                  detail: "Waiting for its window.",
                                  showsSpinner: true)
        }
    }

    /// A 24 pt strip along the top edge reveals the bar; it never blocks the game otherwise.
    private var revealStrip: some View {
        VStack {
            Color.clear
                .frame(height: 24)
                .frame(maxWidth: .infinity)
                .contentShape(Rectangle())
                .onTapGesture { isBarRevealed.toggle() }
                .accessibilityLabel("Show game controls")
            Spacer()
        }
        .ignoresSafeArea()
    }
}

/// "Starting Steam…" and the two other waiting states.
private struct GameModeProgressPanel: View {
    let title: String
    let detail: String
    var showsSpinner = false

    var body: some View {
        VStack(spacing: 18) {
            if showsSpinner { ProgressView().controlSize(.large) }
            Text(title).font(.title2.weight(.semibold))
            Text(detail)
                .font(.callout)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 520)
        }
        .padding(32)
        .foregroundStyle(.white)
    }
}

/// Everything the game shell can do, shown only when the top strip is tapped.
private struct GameModeBar: View {
    let controller: DesktopController
    let coordinator: GameModeCoordinator
    @Binding var isRevealed: Bool
    let configuration: GameModeConfiguration

    var body: some View {
        VStack {
            HStack(spacing: 10) {
                label
                Spacer(minLength: 8)
                buttons
            }
            .padding(.horizontal, 14)
            .padding(.vertical, 10)
            .background(.ultraThinMaterial)
            Spacer()
        }
        .ignoresSafeArea(edges: .top)
        .task {
            // Hides itself again, so it cannot sit on top of a game forever.
            try? await Task.sleep(for: .seconds(8))
            withAnimation { isRevealed = false }
        }
    }

    private var label: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(title).font(.subheadline.weight(.semibold))
            Text(subtitle).font(.caption2).foregroundStyle(.secondary)
        }
        .foregroundStyle(.primary)
    }

    private var title: String {
        switch coordinator.phase {
        case .presenting: "Steam Big Picture"
        case .startingSteam: "Starting Steam…"
        case .stopped(let failure): failure.title
        default: "LinPad Steam"
        }
    }

    private var subtitle: String {
        var parts: [String] = []
        if let status = coordinator.launcherStatus { parts.append("launcher: \(status)") }
        parts.append("screen: \(coordinator.geometryDescription)")
        return parts.joined(separator: " · ")
    }

    @ViewBuilder
    private var buttons: some View {
        if coordinator.prefersClassicUI {
            barButton("Big Picture", symbol: "gamecontroller") { coordinator.useBigPicture() }
        } else {
            barButton("Classic UI", symbol: "macwindow") { coordinator.useClassicUI() }
        }
        barButton(coordinator.fillsScreen ? "Verified screen" : "Fill screen",
                  symbol: coordinator.fillsScreen ? "arrow.down.right.and.arrow.up.left"
                                                  : "arrow.up.left.and.arrow.down.right") {
            coordinator.fillsScreen.toggle()
            coordinator.startSteam(forceBigPicture: !coordinator.prefersClassicUI)
        }
        barButton("Restart Steam", symbol: "arrow.clockwise") {
            coordinator.startSteam(forceBigPicture: !coordinator.prefersClassicUI)
        }
        barButton("Restart Linux", symbol: "power") {
            Task { await controller.linux?.restartSession() }
        }
        barButton("Desktop", symbol: "display") { coordinator.openDesktop() }
    }

    private func barButton(_ title: String, symbol: String, action: @escaping () -> Void) -> some View {
        Button(action: action) {
            Label(title, systemImage: symbol)
                .labelStyle(.titleAndIcon)
                .font(.caption)
        }
        .buttonStyle(.bordered)
        .controlSize(.small)
    }
}

/// A failure the game app can act on, with the actions that make sense for it.
private struct GameModeFailurePanel: View {
    let failure: GameModeFailure
    let coordinator: GameModeCoordinator
    let controller: DesktopController

    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: "exclamationmark.triangle")
                .font(.system(size: 34))
                .foregroundStyle(.yellow)
            Text(failure.title).font(.title2.weight(.semibold))
            Text(failure.detail)
                .font(.callout)
                .foregroundStyle(.secondary)
                .multilineTextAlignment(.center)
                .frame(maxWidth: 560)
            HStack(spacing: 10) {
                Button("Start Steam again") { coordinator.startSteam() }
                    .buttonStyle(.borderedProminent)
                if failure.suggestsClassicUI && !coordinator.prefersClassicUI {
                    Button("Use the classic UI") { coordinator.useClassicUI() }
                        .buttonStyle(.bordered)
                }
                Button("Open desktop") { coordinator.openDesktop() }
                    .buttonStyle(.bordered)
            }
            .padding(.top, 4)
        }
        .padding(32)
        .foregroundStyle(.white)
    }
}
