import Foundation

/// Build-wide constants. No secrets here — the app only ever holds a per-user
/// pairing token (in the Keychain), never any service key.
enum Config {
    static let backendURL = "https://pymite6941-data-analyst-ai-agent.hf.space"
    // Served by the public website (the GitHub repo is private).
    static let versionURL = "https://fitness-ai-agents.vercel.app/version.json"
    static let appVersion = "0.1.0"
    static let appVersionCode = 1
}
