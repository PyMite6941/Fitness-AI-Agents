package studio.tin.fitnessai

import android.content.Context
import org.json.JSONArray
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

/**
 * Posts tracked data to the backend `/ingest` endpoint using the pairing token.
 * Offline-first: if the network is down (or the POST fails), the payload is queued
 * in encrypted prefs and retried on the next successful upload.
 *
 * No analysis happens here — the phone only ships raw readings/workouts; the server
 * stores them in Supabase under the account and analyzes on request.
 */
object Uploader {

    /** Build the /ingest body from collected readings. See models/watch.py for the schema. */
    fun buildPayload(steps: Int, route: List<DoubleArray>?, isoTimestamp: String): String {
        val reading = JSONObject()
            .put("timestamp", isoTimestamp)
            .put("steps", steps)
        val readings = JSONArray().put(reading)
        val body = JSONObject()
            .put("device", "fitness_phone_android")
            .put("app_version", BuildConfig.VERSION_NAME)
            .put("readings", readings)
            .put("workouts", JSONArray())
        // TODO: when a workout/route is active, also append a workout object + post the
        //       route to /routes (lat/lng array) so distance/pace/calories are computed.
        return body.toString()
    }

    /** Try to flush any queued payloads, then send [payloadJson]. Returns true if [payloadJson] sent. */
    fun upload(ctx: Context, payloadJson: String): Boolean {
        val token = Prefs.token(ctx) ?: return false.also { Prefs.enqueue(ctx, payloadJson) }

        // flush backlog first (best-effort)
        for (queued in Prefs.drainQueue(ctx)) {
            if (!post(token, queued)) { Prefs.enqueue(ctx, queued); Prefs.enqueue(ctx, payloadJson); return false }
        }
        val ok = post(token, payloadJson)
        if (!ok) Prefs.enqueue(ctx, payloadJson)
        return ok
    }

    private fun post(token: String, body: String): Boolean {
        return try {
            val conn = (URL("${BuildConfig.BACKEND_URL}/ingest/").openConnection() as HttpURLConnection).apply {
                requestMethod = "POST"
                connectTimeout = 15000
                readTimeout = 20000
                doOutput = true
                setRequestProperty("Content-Type", "application/json")
                setRequestProperty("Authorization", "Bearer $token")
            }
            conn.outputStream.use { it.write(body.toByteArray()) }
            val code = conn.responseCode
            conn.disconnect()
            code in 200..299
        } catch (e: Exception) {
            false
        }
    }
}
