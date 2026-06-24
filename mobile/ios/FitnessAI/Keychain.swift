import Foundation
import Security

/// Secure storage for the pairing token using the iOS Keychain.
/// Accessibility is `afterFirstUnlockThisDeviceOnly`: readable in the background
/// (so HealthKit delivery can upload) but never synced to iCloud and never
/// leaves this device. This is the iOS equivalent of the Android Keystore.
enum Keychain {
    private static let service = "studio.tin.fitnessai"
    private static let account = "pair_token"

    static func setToken(_ token: String) {
        let data = Data(token.utf8)
        // delete any existing item first
        SecItemDelete([
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: account,
        ] as CFDictionary)
        SecItemAdd([
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: account,
            kSecValueData: data,
            kSecAttrAccessible: kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly,
        ] as CFDictionary, nil)
    }

    static func token() -> String? {
        var out: AnyObject?
        let status = SecItemCopyMatching([
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: account,
            kSecReturnData: true,
            kSecMatchLimit: kSecMatchLimitOne,
        ] as CFDictionary, &out)
        guard status == errSecSuccess, let data = out as? Data else { return nil }
        return String(data: data, encoding: .utf8)
    }

    static var isPaired: Bool { token()?.isEmpty == false }

    static func clear() {
        SecItemDelete([
            kSecClass: kSecClassGenericPassword,
            kSecAttrService: service,
            kSecAttrAccount: account,
        ] as CFDictionary)
    }
}
