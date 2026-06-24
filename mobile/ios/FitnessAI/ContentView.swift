import SwiftUI

struct ContentView: View {
    @State private var code = ""
    @State private var paired = Keychain.isPaired
    @State private var message = ""

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            Text("FitnessAI").font(.largeTitle).bold()
            Text(paired ? "Status: paired ✓" : "Status: not paired")
                .foregroundStyle(paired ? .green : .secondary)

            if !paired {
                Text("Paste the pairing code from the web app (Settings → Pair a device).")
                    .font(.footnote).foregroundStyle(.secondary)
                TextField("fit_…", text: $code)
                    .textFieldStyle(.roundedBorder)
                    .autocorrectionDisabled()
                    .textInputAutocapitalization(.never)
                Button("Pair this phone") { pair() }
                    .buttonStyle(.borderedProminent)
            } else {
                Button("Sync Apple Health now") { Task { await syncNow() } }
                    .buttonStyle(.borderedProminent)
                Button("Unpair", role: .destructive) { Keychain.clear(); paired = false }
            }

            Button("Check for updates") { Task { await checkUpdate() } }
            if !message.isEmpty { Text(message).font(.footnote).foregroundStyle(.secondary) }
            Spacer()
        }
        .padding(24)
    }

    private func pair() {
        let c = code.trimmingCharacters(in: .whitespacesAndNewlines)
        guard c.hasPrefix("fit_") else { message = "That doesn't look like a pairing code."; return }
        Keychain.setToken(c)
        paired = true
        message = "Paired. Granting Health access…"
        Task {
            let ok = await HealthSync.shared.requestAuthorization()
            if ok { HealthSync.shared.startBackgroundSync(); await HealthSync.shared.uploadTodaySteps() }
            message = ok ? "Connected to Apple Health. Syncing in the background." : "Health access denied — enable it in Settings → Health."
        }
    }

    private func syncNow() async {
        message = "Syncing…"
        await HealthSync.shared.uploadTodaySteps()
        message = "Synced today's activity."
    }

    private func checkUpdate() async {
        if let r = await UpdateChecker.check() {
            message = r.updateAvailable ? "Update available: v\(r.latestVersion)" : "You're on the latest version."
        } else { message = "Couldn't check for updates." }
    }
}
