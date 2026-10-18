import CoreGraphics
import Foundation

/// What a full-screen variant does instead of showing the Linux desktop: boot the guest,
/// start the subject's UI (Steam's Big Picture, or the Omarchy session) and present its
/// window full screen with no desktop around it.
///
/// Everything the guest side needs is installed by the variant's own tooling: for Steam,
/// `steam-bigpicture` (release/guest/steam-bigpicture) starts the verified Steam launcher,
/// adds `--geometry` for the screen size and falls back to the classic UI once if Big Picture
/// is refused. The Omarchy canvas uses `omarchy-pad-session` instead and appends no flags.
public struct GameModeConfiguration: Equatable, Sendable {
    /// How the shell names what it is starting, in copy and in failures ("Steam", "Omarchy").
    public var subjectName: String
    /// The guest launcher, by name; it is on the session's PATH from /usr/local/bin.
    public var command: String
    /// Either path means the subject is installed. For Steam the first is the verified entry
    /// point and the second is the client itself, so a user who started Steam by hand still
    /// passes. Absolute paths, not $HOME: the probe goes through shellQuoted, which does not
    /// expand.
    public var launcherPaths: [String]
    /// Wayland app ids whose first toplevel becomes the full-screen surface. steam-local
    /// names the viewer window after `ISHWL_APP=Steam`, in both Big Picture and classic mode;
    /// omarchy-pad-bridge names the Omarchy surface `omarchy-pad`.
    public var claimedAppIDs: Set<String>
    /// No toplevel within this window is reported as a failure with actions, not a spinner.
    public var startupTimeout: Duration
    /// Where the launcher says what it started (guest /tmp).
    public var statusPath: String
    /// Written by the launcher when it had to fall back to another UI or failed.
    public var fallbackMarkerPath: String
    /// Flags appended to the launcher command. nil means this session has no such mode, so
    /// nothing is appended: the Omarchy canvas has no Big Picture, classic UI or geometry.
    public var bigPictureFlag: String?
    public var classicFlag: String?
    public var geometryFlag: String?
    /// Settings the desktop would normally offer live here instead.
    public var usesClassicUIByDefault: Bool

    /// The former name of `command`, kept so the Steam path and older callers keep compiling.
    public var steamCommand: String {
        get { command }
        set { command = newValue }
    }

    public static let steam = GameModeConfiguration()

    /// The Omarchy canvas: one guest Wayland surface (the Omarchy desktop) edge to edge.
    /// `omarchy-pad-session` starts Hyprland and the bridge, which names its surface
    /// `omarchy-pad`; the shell claims exactly that toplevel and appends no Steam flags.
    public static let omarchy = GameModeConfiguration(
        subjectName: "Omarchy",
        command: "omarchy-pad-session",
        launcherPaths: ["/usr/local/bin/omarchy-pad-session"],
        claimedAppIDs: ["omarchy-pad"],
        startupTimeout: .seconds(300),
        statusPath: "/tmp/omarchy-pad/status",
        fallbackMarkerPath: "/tmp/omarchy-pad/error",
        bigPictureFlag: nil,
        classicFlag: nil,
        geometryFlag: nil,
        usesClassicUIByDefault: false)

    public init(subjectName: String = "Steam",
                command: String = "steam-bigpicture",
                launcherPaths: [String] = ["/usr/local/bin/steam",
                                           "/root/.local/share/Steam/steamrtarm64/steam"],
                claimedAppIDs: Set<String> = ["steam"],
                startupTimeout: Duration = .seconds(180),
                statusPath: String = "/tmp/steam-bigpicture-status",
                fallbackMarkerPath: String = "/tmp/steam-bigpicture-fallback",
                bigPictureFlag: String? = "--bigpicture",
                classicFlag: String? = "--classic",
                geometryFlag: String? = "--geometry",
                usesClassicUIByDefault: Bool = false) {
        self.subjectName = subjectName
        self.command = command
        self.launcherPaths = launcherPaths
        self.claimedAppIDs = claimedAppIDs
        self.startupTimeout = startupTimeout
        self.statusPath = statusPath
        self.fallbackMarkerPath = fallbackMarkerPath
        self.bigPictureFlag = bigPictureFlag
        self.classicFlag = classicFlag
        self.geometryFlag = geometryFlag
        self.usesClassicUIByDefault = usesClassicUIByDefault
    }
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

/// Why the subject is not on screen. Each case carries what the panel needs to explain it
/// and which actions make sense. The copy uses the configuration's subject (`Steam`,
/// `Omarchy`) instead of naming one of them.
public enum GameModeFailure: Equatable, Sendable {
    /// No launcher in this Linux system (a fresh container, or the subject was removed).
    case steamMissing
    /// The Linux GUI bridge itself is not usable.
    case sessionUnavailable(String)
    /// Another LinPad app (LinPad itself) holds the shared Linux system.
    case linuxSystemBusy(String)
    /// The launcher ran but no window appeared in time.
    case steamNotResponding
    /// The window went away; the status line is what the launcher last wrote.
    case steamStopped(String)

    /// The Steam wording, for the game shell and the existing callers.
    public var title: String { title(subject: "Steam") }

    public func title(subject: String) -> String {
        switch self {
        case .steamMissing: "\(subject) is not installed"
        case .sessionUnavailable: "Linux is not available"
        case .linuxSystemBusy: "Linux is in use"
        case .steamNotResponding: "\(subject) did not appear"
        case .steamStopped: "\(subject) closed"
        }
    }

    /// The Steam wording, for the game shell and the existing callers.
    public var detail: String { detail(subject: "Steam") }

    public func detail(subject: String) -> String {
        switch self {
        case .steamMissing:
            """
            This Linux system has no \(subject) launcher. Open the desktop and install it \
            there, then come back and start \(subject) again.
            """
        case .sessionUnavailable(let reason):
            reason
        case .linuxSystemBusy(let reason):
            """
            \(reason) Both apps use the same Linux system, so only one can run it at a time.
            """
        case .steamNotResponding:
            """
            \(subject) started, but no window reached the app in time. The first start after \
            a boot or an update can take longer than usual. The Linux system's /tmp has the \
            details.
            """
        case .steamStopped(let status):
            status.isEmpty
                ? "\(subject)'s window closed. Start it again, or open the desktop to work in Linux."
                : "\(subject)'s window closed (launcher status: \(status))."
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
