import CoreGraphics
import Foundation

/// What the game app does instead of showing the Linux desktop: boot the guest, start
/// Steam's Big Picture UI (the Deck UI, `steam -gamepadui`) and present its window full
/// screen with no desktop around it.
///
/// Everything the guest side needs is installed by the repair kit: `steam-bigpicture`
/// (release/guest/steam-bigpicture) starts the verified Steam launcher, adds `--geometry`
/// for the screen size and falls back to the classic UI once if Big Picture is refused.
public struct GameModeConfiguration: Equatable, Sendable {
    /// The guest launcher, by name; it is on the session's PATH from /usr/local/bin.
    public var steamCommand = "steam-bigpicture"
    /// Either path means Steam is installed. The first is the verified entry point, the
    /// second is the client itself, so a user who started Steam by hand still passes.
    /// Absolute paths, not $HOME: the probe goes through shellQuoted, which does not expand.
    public var launcherPaths = ["/usr/local/bin/steam", "/root/.local/share/Steam/steamrtarm64/steam"]
    /// Wayland app ids whose first toplevel becomes the game surface. steam-local names the
    /// viewer window after `ISHWL_APP=Steam`, in both Big Picture and classic mode.
    public var claimedAppIDs: Set<String> = ["steam"]
    /// No toplevel within this window is reported as a failure with actions, not a spinner.
    public var startupTimeout: Duration = .seconds(180)
    /// Where steam-bigpicture says what it started (guest /tmp).
    public var statusPath = "/tmp/steam-bigpicture-status"
    /// Written by steam-bigpicture when it had to fall back to the classic UI.
    public var fallbackMarkerPath = "/tmp/steam-bigpicture-fallback"
    /// Settings the desktop would normally offer live here instead.
    public var usesClassicUIByDefault = false

    public static let steam = GameModeConfiguration()

    public init() {}
}

/// The game app can show the ordinary Linux desktop instead, e.g. to sign in to Steam or
/// install a game. The choice survives a relaunch; the desktop build never reads it.
public enum SessionMode: String, Sendable {
    case game
    case desktop

    public static let storageKey = "linpad.sessionMode"

    public static func stored(in defaults: UserDefaults = .standard) -> SessionMode? {
        defaults.string(forKey: storageKey).flatMap(SessionMode.init(rawValue:))
    }

    public static func store(_ mode: SessionMode, in defaults: UserDefaults = .standard) {
        defaults.set(mode.rawValue, forKey: storageKey)
    }
}

/// Why Steam is not on screen. Each case carries what the panel needs to explain it and
/// which actions make sense.
public enum GameModeFailure: Equatable, Sendable {
    /// No Steam client in this Linux system (a fresh container, or Steam was removed).
    case steamMissing
    /// The Linux GUI bridge itself is not usable.
    case sessionUnavailable(String)
    /// Another LinPad app (LinPad itself) holds the shared Linux system.
    case linuxSystemBusy(String)
    /// The launcher ran but no window appeared in time.
    case steamNotResponding
    /// The window went away; the status line is what steam-bigpicture last wrote.
    case steamStopped(String)

    public var title: String {
        switch self {
        case .steamMissing: "Steam is not installed"
        case .sessionUnavailable: "Linux is not available"
        case .linuxSystemBusy: "Linux is in use"
        case .steamNotResponding: "Steam did not appear"
        case .steamStopped: "Steam closed"
        }
    }

    public var detail: String {
        switch self {
        case .steamMissing:
            """
            This Linux system has no Steam client. Open the desktop and install Steam there \
            (installation/linpad-steam in the LinPad repository describes the transfer), then \
            come back and start Steam again.
            """
        case .sessionUnavailable(let reason):
            reason
        case .linuxSystemBusy(let reason):
            """
            \(reason) Both apps use the same Linux system, so only one can run it at a time.
            """
        case .steamNotResponding:
            """
            steam-bigpicture started, but no Steam window reached the app within three minutes. \
            The first start after an update can take longer than usual. /tmp/steam-main.log and \
            /tmp/steam-xvnc.log in the Linux system have the details.
            """
        case .steamStopped(let status):
            status.isEmpty
                ? "Steam's window closed. Start it again, or open the desktop to work in Linux."
                : "Steam's window closed (launcher status: \(status))."
        }
    }

    /// The gamepad UI is what failed most often; offering the classic UI is then the useful
    /// retry, because it is the same client with a different UI mode.
    public var suggestsClassicUI: Bool {
        switch self {
        case .steamNotResponding, .steamStopped: true
        case .steamMissing, .sessionUnavailable, .linuxSystemBusy: false
        }
    }
}

/// Where the game shell is in its (short) life. `presenting` carries the Wayland surface
/// the shell shows full screen.
public enum GameModePhase: Equatable {
    case waitingForSystem
    case checkingSteam
    case startingSteam
    case presenting(surfaceID: UInt32)
    case stopped(GameModeFailure)

    public var gameSurfaceID: UInt32? {
        if case .presenting(let id) = self { return id }
        return nil
    }

    public var failure: GameModeFailure? {
        if case .stopped(let failure) = self { return failure }
        return nil
    }
}
