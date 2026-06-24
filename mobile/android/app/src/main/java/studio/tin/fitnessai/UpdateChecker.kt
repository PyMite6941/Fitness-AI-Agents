package studio.tin.fitnessai

import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL

/**
 * Self-update check against the GitHub repo's mobile/version.json.
 * Compares the published `versionCode` with this build's BuildConfig.VERSION_CODE
 * (BuildConfig auto-exposes it). If GitHub is newer, returns where to get the APK.
 */
object UpdateChecker {

    data class Result(val updateAvailable: Boolean, val latestVersion: String, val apkUrl: String, val notes: String)

    /** Network call — run off the main thread. Returns null if the check couldn't complete. */
    fun check(): Result? {
        return try {
            val conn = (URL(BuildConfig.VERSION_JSON_URL).openConnection() as HttpURLConnection).apply {
                connectTimeout = 10000; readTimeout = 10000
            }
            val text = conn.inputStream.bufferedReader().use { it.readText() }
            conn.disconnect()
            val app = JSONObject(text).getJSONObject("androidApp")
            val latestCode = app.getInt("versionCode")
            Result(
                updateAvailable = latestCode > BuildConfig.VERSION_CODE,
                latestVersion = app.getString("version"),
                apkUrl = app.getString("apkUrl"),
                notes = app.optString("notes", ""),
            )
        } catch (e: Exception) {
            null
        }
    }
}
