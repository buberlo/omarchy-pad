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

    /// Which Linux root this build boots. The desktop app keeps `default` (its Alpine system, with the
    /// user's files); the game app boots `arch` (the Arch Linux ARM image, `roots/arch`). One container
    /// holds both, and neither may mount the other's.
    ///
    /// This is the rule. `VariantRootName()` in app/AppDelegate.m mirrors it because the boot path runs
    /// in Objective-C before any of this is reachable -- change one, change both. Tested in
    /// `GameModeTests.testEachVariantKnowsWhichRootItBoots`.
    public var rootName: String { isGameMode ? "arch" : "default" }
}
