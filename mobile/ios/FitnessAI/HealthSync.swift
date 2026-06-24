import Foundation
import HealthKit

/// Reads activity from HealthKit and uploads it. HealthKit already aggregates
/// steps / distance / heart rate from the iPhone AND a paired Apple Watch in the
/// background, so we don't run our own sensors — we read what iOS already collected.
/// This is why iOS "tracking" is HealthKit-based rather than a foreground service.
final class HealthSync {
    static let shared = HealthSync()
    private let store = HKHealthStore()

    private var readTypes: Set<HKObjectType> {
        [
            HKQuantityType(.stepCount),
            HKQuantityType(.distanceWalkingRunning),
            HKQuantityType(.heartRate),
            HKQuantityType(.activeEnergyBurned),
        ]
    }

    var isAvailable: Bool { HKHealthStore.isHealthDataAvailable() }

    /// Ask the user to share the read-only types. Call from the pairing screen.
    func requestAuthorization() async -> Bool {
        guard isAvailable else { return false }
        return await withCheckedContinuation { cont in
            store.requestAuthorization(toShare: [], read: readTypes) { ok, _ in cont.resume(returning: ok) }
        }
    }

    /// Register background delivery so iOS wakes us when new step data lands, then
    /// uploads today's totals. Call once after pairing + authorization.
    func startBackgroundSync() {
        let steps = HKQuantityType(.stepCount)
        store.enableBackgroundDelivery(for: steps, frequency: .hourly) { _, _ in }
        let q = HKObserverQuery(sampleType: steps, predicate: nil) { [weak self] _, completion, _ in
            Task { await self?.uploadTodaySteps(); completion() }
        }
        store.execute(q)
    }

    /// Sum today's steps and POST them. (Distance/HR/energy follow the same pattern — TODO.)
    func uploadTodaySteps() async {
        let type = HKQuantityType(.stepCount)
        let start = Calendar.current.startOfDay(for: Date())
        let pred = HKQuery.predicateForSamples(withStart: start, end: Date())
        let total: Double? = await withCheckedContinuation { cont in
            let q = HKStatisticsQuery(quantityType: type, quantitySamplePredicate: pred, options: .cumulativeSum) { _, stats, _ in
                cont.resume(returning: stats?.sumQuantity()?.doubleValue(for: .count()))
            }
            store.execute(q)
        }
        guard let steps = total else { return }
        let reading = Uploader.Reading(
            timestamp: ISO8601DateFormatter().string(from: Date()),
            steps: Int(steps), heart_rate: nil, distance_meters: nil, active_calories: nil
        )
        _ = await Uploader.upload(readings: [reading])
    }
}
