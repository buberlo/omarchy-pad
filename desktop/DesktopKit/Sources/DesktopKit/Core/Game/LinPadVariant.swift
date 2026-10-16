import Foundation

/// Which product this build is. Both apps are built from the one `iSH-ARM64` target, so the
/// choice is baked into the bundle's Info.plist by the build (`LINPAD_VARIANT`, see
/// app/AppARM64.xcconfig and installation/steam-app):
///
/// - `desktop`: LinPad's Linux desktop, as before.
/// - `steam`:   the dedicated game app, which boots straight into Steam Big Picture.
///
/// A missing or unknown key means `desktop`, so test hosts, previews and the shipping
/// desktop build behave exactly as they did before this type existed.
public enum LinPadVariant: String, Sendable {
    case desktop
    case steam

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
}
