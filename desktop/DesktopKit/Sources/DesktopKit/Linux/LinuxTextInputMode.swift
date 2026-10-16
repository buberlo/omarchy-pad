import SwiftUI

/// Whether a Linux app's hardware keys go to the iPad's text system or straight to the app.
///
/// With a text field focused, the iPad is the input method: printable keys become
/// committed text (accents, dictation, emoji, CJK), which is what a terminal, an editor
/// or a browser needs. A game is the opposite: its SDL/Unity text input is not a text
/// field, it reads key codes and ignores committed text, so routing its keys through the
/// iPad text system loses them (Enter survives only because a non-multiline field turns
/// it back into a Return key press).
enum LinuxTextInputMode: String, CaseIterable, Identifiable {
    case automatic
    case keys
    case compose

    static let storageKey = "desktop.linux.textInput"
    /// Comma-separated Wayland app ids that get key codes in `automatic`.
    static let keysAppsKey = "desktop.linux.textInputKeysApps"
    /// Hollow Knight's Unity player name, as it arrives in the toplevel's app id.
    static let defaultKeysApps = "hollow_knight.x86_64"

    var id: String { rawValue }

    var title: String {
        switch self {
        case .automatic: "Keys in Listed Apps"
        case .keys: "Always Send Keys"
        case .compose: "Always Compose Text"
        }
    }

    static var current: LinuxTextInputMode {
        UserDefaults.standard.string(forKey: storageKey).flatMap(LinuxTextInputMode.init) ?? .automatic
    }

    static var keysApps: Set<String> {
        let list = UserDefaults.standard.string(forKey: keysAppsKey) ?? defaultKeysApps
        return Set(LinuxOptionKeyMode.parse(list))
    }

    /// Whether this app's hardware keys go to it as key codes instead of to the iPad text
    /// system. The app id is the Wayland one ishwl reports with the toplevel's title.
    static func sendsKeysDirectly(appID: String) -> Bool {
        switch current {
        case .keys: true
        case .compose: false
        case .automatic: keysApps.contains(appID.lowercased())
        }
    }
}

/// Settings → Keyboard: hardware keys in Linux apps. Self-contained, so the Settings app
/// can place it anywhere.
struct LinuxTextInputSettingsView: View {
    @Environment(\.desktopTheme) private var theme
    @AppStorage(LinuxTextInputMode.storageKey) private var mode = LinuxTextInputMode.automatic
    @AppStorage(LinuxTextInputMode.keysAppsKey) private var keysApps = LinuxTextInputMode.defaultKeysApps

    var body: some View {
        SettingsRow(title: "Keys in Linux apps") {
            Picker("Keys in Linux apps", selection: $mode) {
                ForEach(LinuxTextInputMode.allCases) { Text($0.title).tag($0) }
            }
            .labelsHidden()
            .accessibilityIdentifier("settings.linuxTextInput")
        }
        if mode == .automatic {
            SettingsRow(title: "Apps that get keys") {
                TextField("App ids", text: $keysApps)
                    .textInputAutocapitalization(.never)
                    .autocorrectionDisabled()
                    .multilineTextAlignment(.trailing)
                    .font(.callout.monospaced())
                    .accessibilityIdentifier("settings.linuxTextInputKeysApps")
            }
        }
        Text("A Linux text field composes through the iPad keyboard: Option-E then E is é, and dictation, emoji and CJK work. A game has no text field and reads key codes instead, so committed text is lost to it; the apps listed here get their keys as keys. App ids are Wayland app ids, separated by commas.")
            .font(.caption)
            .foregroundStyle(theme.secondaryText)
            .fixedSize(horizontal: false, vertical: true)
    }
}
