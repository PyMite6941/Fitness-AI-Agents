package studio.tin.fitnessai

import android.content.Context
import android.content.SharedPreferences
import androidx.security.crypto.EncryptedSharedPreferences
import androidx.security.crypto.MasterKey

/**
 * Encrypted on-device storage for the pairing token + a small offline upload queue.
 * Backed by the Android Keystore, so the token is never stored in plaintext.
 */
object Prefs {
    private const val FILE = "fitnessai_secure"
    private const val KEY_TOKEN = "pair_token"
    private const val KEY_QUEUE = "pending_queue"   // newline-separated JSON payloads

    private fun sp(ctx: Context): SharedPreferences {
        val master = MasterKey.Builder(ctx)
            .setKeyScheme(MasterKey.KeyScheme.AES256_GCM)
            .build()
        return EncryptedSharedPreferences.create(
            ctx, FILE, master,
            EncryptedSharedPreferences.PrefKeyEncryptionScheme.AES256_SIV,
            EncryptedSharedPreferences.PrefValueEncryptionScheme.AES256_GCM,
        )
    }

    fun token(ctx: Context): String? = sp(ctx).getString(KEY_TOKEN, null)
    fun isPaired(ctx: Context): Boolean = !token(ctx).isNullOrBlank()
    fun setToken(ctx: Context, token: String) = sp(ctx).edit().putString(KEY_TOKEN, token).apply()
    fun clear(ctx: Context) = sp(ctx).edit().clear().apply()

    // --- tiny offline queue: payloads that failed to upload, retried later ---
    fun enqueue(ctx: Context, payloadJson: String) {
        val cur = sp(ctx).getString(KEY_QUEUE, "") ?: ""
        sp(ctx).edit().putString(KEY_QUEUE, if (cur.isBlank()) payloadJson else "$cur\n$payloadJson").apply()
    }
    fun drainQueue(ctx: Context): List<String> {
        val cur = sp(ctx).getString(KEY_QUEUE, "") ?: ""
        if (cur.isBlank()) return emptyList()
        sp(ctx).edit().putString(KEY_QUEUE, "").apply()
        return cur.split("\n").filter { it.isNotBlank() }
    }
}
