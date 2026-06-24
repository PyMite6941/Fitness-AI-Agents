package studio.tin.fitnessai

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.os.Build
import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

/**
 * Single-screen app:
 *  - If not paired: shows a field to paste the pairing token from the web app.
 *  - If paired: shows status, lets you start/stop background tracking, and checks
 *    GitHub for a newer APK.
 *
 * NOTE: the layout below is built in code to keep the scaffold to one file. Replace
 * with res/layout/activity_main.xml + ViewBinding for a real UI (viewBinding is on).
 */
class MainActivity : AppCompatActivity() {

    private val scope = CoroutineScope(Dispatchers.Main)
    private lateinit var status: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // TODO: replace this programmatic UI with res/layout/activity_main.xml.
        val tokenInput = EditText(this).apply { hint = "Paste pairing code (fit_…)" }
        val pairBtn = Button(this).apply { text = "Pair this phone" }
        val trackBtn = Button(this).apply { text = "Start tracking" }
        val updateBtn = Button(this).apply { text = "Check for updates" }
        status = TextView(this)

        val root = android.widget.LinearLayout(this).apply {
            orientation = android.widget.LinearLayout.VERTICAL
            setPadding(48, 96, 48, 48)
            addView(TextView(this@MainActivity).apply { text = "FitnessAI tracker"; textSize = 22f })
            addView(status)
            addView(tokenInput); addView(pairBtn)
            addView(trackBtn); addView(updateBtn)
        }
        setContentView(root)

        refreshStatus()

        pairBtn.setOnClickListener {
            val code = tokenInput.text.toString().trim()
            if (!code.startsWith("fit_")) { toast("That doesn't look like a pairing code."); return@setOnClickListener }
            Prefs.setToken(this, code)
            toast("Paired. You can start tracking.")
            refreshStatus()
        }

        trackBtn.setOnClickListener {
            if (!Prefs.isPaired(this)) { toast("Pair this phone first."); return@setOnClickListener }
            ensurePermissions()
            startForegroundService(Intent(this, TrackingService::class.java))
            toast("Tracking started.")
        }

        updateBtn.setOnClickListener {
            scope.launch {
                val res = withContext(Dispatchers.IO) { UpdateChecker.check() }
                when {
                    res == null -> toast("Couldn't reach GitHub.")
                    res.updateAvailable -> {
                        toast("Update available: v${res.latestVersion}")
                        startActivity(Intent(Intent.ACTION_VIEW, android.net.Uri.parse(res.apkUrl)))
                    }
                    else -> toast("You're on the latest version.")
                }
            }
        }
    }

    private fun refreshStatus() {
        status.text = if (Prefs.isPaired(this)) "Status: paired ✓" else "Status: not paired"
    }

    private fun ensurePermissions() {
        val needed = mutableListOf<String>()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q &&
            ContextCompat.checkSelfPermission(this, Manifest.permission.ACTIVITY_RECOGNITION) != PackageManager.PERMISSION_GRANTED)
            needed += Manifest.permission.ACTIVITY_RECOGNITION
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.ACCESS_FINE_LOCATION) != PackageManager.PERMISSION_GRANTED)
            needed += Manifest.permission.ACCESS_FINE_LOCATION
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU &&
            ContextCompat.checkSelfPermission(this, Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED)
            needed += Manifest.permission.POST_NOTIFICATIONS
        if (needed.isNotEmpty()) ActivityCompat.requestPermissions(this, needed.toTypedArray(), 100)
    }

    private fun toast(s: String) = Toast.makeText(this, s, Toast.LENGTH_SHORT).show()
}
