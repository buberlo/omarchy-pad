import Foundation

/// Which product this build is. Both apps are built from the one `iSH-ARM64` target, so the
/// choice is baked into the bundle's Info.plist by the build (`LINPAD_VARIANT`, see
/// app/AppARM64.xcconfig and installation/steam-app):
///
/// - `desktop`: LinPad's Linux desktop, as before.
/// - `steam`:   the dedicated game app, which boots straight into Steam Big Picture.
/// - `omarchy`: the Omarchy canvas app, which boots the Omarchy root and shows its one
///              guest Wayland surface edge to edge (the desktop is one reveal strip away).
///
/// A missing or unknown key means `desktop`, so test hosts, previews and the shipping
/// desktop build behave exactly as they did before this type existed.
public enum LinPadVariant: String, Sendable {
    case desktop
    case steam
    case omarchy

    public static let infoPlistKey = "LinPadVariant"

    public static func current(bundle: Bundle = .main) -> LinPadVariant {
        parse(bundle.object(forInfoDictionaryKey: infoPlistKey) as? String)
    }

    /// The Info.plist value, as the build leaves it. Anything unrecognised is the desktop.
    public static func parse(_ raw: String?) -> LinPadVariant {
        guard let raw else { return .desktop }
        let value = raw.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
        return LinPadVariant(rawValue: value) ?? .desktop
    }

    /// The game app boots into Steam instead of the desktop.
    public var isGameMode: Bool { self == .steam }

    /// The Omarchy app boots the guest session and shows its single Wayland surface full
    /// screen. It shares the game mode machinery but is not a game: there is no Steam UI to
    /// start, no classic fallback and no screen-size flag.
    public var isCanvasMode: Bool { self == .omarchy }

    /// Which Linux root this build boots. The desktop app keeps `default` (its Alpine system, with the
    /// user's files); the game app boots `arch` (the Arch Linux ARM image, `roots/arch`); the canvas
    /// app boots `omarchy` (the Arch Linux ARM + Omarchy image, `roots/omarchy`). One container holds
    /// all of them, and none may mount another's.
    ///
    /// This is the rule. `VariantRootName()` in app/AppDelegate.m mirrors it because the boot path runs
    /// in Objective-C before any of this is reachable -- change one, change both. Tested in
    /// `GameModeTests.testEachVariantKnowsWhichRootItBoots`.
    public var rootName: String {
        switch self {
        case .desktop: "default"
        case .steam: "arch"
        case .omarchy: "omarchy"
        }
    }
}
