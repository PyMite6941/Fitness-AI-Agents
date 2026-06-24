import Foundation

/// Posts readings to the backend /ingest endpoint using the Keychain pairing token.
/// HTTPS is enforced by App Transport Security (no exceptions in Info.plist).
/// No analysis happens on-device — the phone only ships raw readings.
enum Uploader {

    struct Reading: Codable {
        let timestamp: String   // ISO-8601
        var steps: Int?
        var heart_rate: Double?
        var distance_meters: Double?
        var active_calories: Double?
    }

    /// Send a batch of readings. Returns true on success (HTTP 2xx).
    static func upload(readings: [Reading]) async -> Bool {
        guard let token = Keychain.token(), !readings.isEmpty else { return false }
        guard let url = URL(string: "\(Config.backendURL)/ingest/") else { return false }

        let body: [String: Any] = [
            "device": "fitness_phone_ios",
            "app_version": Config.appVersion,
            "readings": readings.map { r in
                var d: [String: Any] = ["timestamp": r.timestamp]
                if let v = r.steps { d["steps"] = v }
                if let v = r.heart_rate { d["heart_rate"] = v }
                if let v = r.distance_meters { d["distance_meters"] = v }
                if let v = r.active_calories { d["active_calories"] = v }
                return d
            },
            "workouts": [],
        ]
        guard let data = try? JSONSerialization.data(withJSONObject: body) else { return false }

        var req = URLRequest(url: url)
        req.httpMethod = "POST"
        req.setValue("application/json", forHTTPHeaderField: "Content-Type")
        req.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        req.httpBody = data
        req.timeoutInterval = 20

        do {
            let (_, resp) = try await URLSession.shared.data(for: req)
            return (resp as? HTTPURLResponse).map { (200..<300).contains($0.statusCode) } ?? false
        } catch {
            // TODO: persist failed batches to disk and retry (offline queue), like Android's Prefs.enqueue.
            return false
        }
    }
}
