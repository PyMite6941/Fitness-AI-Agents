import Foundation

/// Checks the website's version.json for a newer build. iOS can't auto-install an
/// update (no sideload), so this just informs the user to re-install via Xcode /
/// TestFlight. Reads `iosApp` if present, else falls back to `androidApp`.
enum UpdateChecker {
    struct Result { let updateAvailable: Bool; let latestVersion: String; let notes: String }

    static func check() async -> Result? {
        guard let url = URL(string: Config.versionURL) else { return nil }
        guard let (data, _) = try? await URLSession.shared.data(from: url),
              let json = try? JSONSerialization.jsonObject(with: data) as? [String: Any] else { return nil }
        let app = (json["iosApp"] as? [String: Any]) ?? (json["androidApp"] as? [String: Any])
        guard let app, let code = app["versionCode"] as? Int else { return nil }
        return Result(
            updateAvailable: code > Config.appVersionCode,
            latestVersion: app["version"] as? String ?? "?",
            notes: app["notes"] as? String ?? ""
        )
    }
}
