package studio.tin.fitnessai

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.Service
import android.content.Intent
import android.hardware.Sensor
import android.hardware.SensorEvent
import android.hardware.SensorEventListener
import android.hardware.SensorManager
import android.os.IBinder
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import java.time.Instant

/**
 * Foreground service that keeps tracking while the screen is off — the part a PWA
 * can't do, which is why this is a native app.
 *
 *  - Listens to TYPE_STEP_COUNTER (cumulative since boot) and computes a delta.
 *  - Every UPLOAD_INTERVAL_MS, ships the step delta to the backend via Uploader.
 *
 * TODO (see mobile/android/README.md "TODO"):
 *  - Add FusedLocationProvider for GPS routes during an active workout.
 *  - Persist the last cumulative step baseline across reboots (TYPE_STEP_COUNTER
 *    resets on reboot) so reboots don't drop or double-count steps.
 *  - Batch multiple readings instead of one-per-interval to save battery/network.
 */
class TrackingService : Service(), SensorEventListener {

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private lateinit var sensorManager: SensorManager
    private var stepSensor: Sensor? = null
    private var baseline: Float = -1f          // first cumulative reading seen
    private var lastUploadedSteps = 0
    private var currentSteps = 0
    private var lastUploadAt = 0L

    override fun onCreate() {
        super.onCreate()
        startForeground(NOTIF_ID, buildNotification("Tracking your activity"))
        sensorManager = getSystemService(SENSOR_SERVICE) as SensorManager
        stepSensor = sensorManager.getDefaultSensor(Sensor.TYPE_STEP_COUNTER)
        stepSensor?.let { sensorManager.registerListener(this, it, SensorManager.SENSOR_DELAY_NORMAL) }
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int = START_STICKY

    override fun onSensorChanged(event: SensorEvent) {
        if (event.sensor.type != Sensor.TYPE_STEP_COUNTER) return
        val total = event.values[0]
        if (baseline < 0f) baseline = total
        currentSteps = (total - baseline).toInt()

        val now = System.currentTimeMillis()
        if (now - lastUploadAt >= UPLOAD_INTERVAL_MS && currentSteps > lastUploadedSteps) {
            lastUploadAt = now
            val delta = currentSteps - lastUploadedSteps
            lastUploadedSteps = currentSteps
            val payload = Uploader.buildPayload(delta, null, Instant.now().toString())
            scope.launch { Uploader.upload(applicationContext, payload) }
        }
    }

    override fun onAccuracyChanged(sensor: Sensor?, accuracy: Int) {}

    override fun onDestroy() {
        sensorManager.unregisterListener(this)
        scope.cancel()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    private fun buildNotification(text: String): Notification {
        val mgr = getSystemService(NotificationManager::class.java)
        if (mgr.getNotificationChannel(CHANNEL) == null) {
            mgr.createNotificationChannel(
                NotificationChannel(CHANNEL, "Activity tracking", NotificationManager.IMPORTANCE_LOW)
            )
        }
        return NotificationCompat.Builder(this, CHANNEL)
            .setContentTitle("FitnessAI")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.ic_menu_compass)
            .setOngoing(true)
            .build()
    }

    companion object {
        private const val CHANNEL = "tracking"
        private const val NOTIF_ID = 1
        private const val UPLOAD_INTERVAL_MS = 5 * 60 * 1000L  // upload every 5 min
    }
}
