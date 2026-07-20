# Daily Insights Push — setup

The retention loop: once a day the backend computes each opted‑in user's Readiness
score + Watchdog alerts and delivers a browser **push notification**. Free
scheduler = a GitHub Actions cron that calls the backend.

## Pieces
- **Backend:** `routes/push.py` (subscribe/unsubscribe/status/test), `routes/cron.py`
  (`POST /cron/daily-insights`), `push_utils.py` (VAPID web push), and the reusable
  `readiness_for` / `alerts_for` / `build_daily_notification` in `routes/insights.py`.
- **DB:** `push_subscriptions` table (see `schema.sql`, RLS on — service role only).
- **Frontend:** `sw.js` push/notificationclick handlers, `lib/push.js`, and the
  `NotifyToggle` control in the Coach page header.
- **Scheduler:** `.github/workflows/daily-insights.yml` (13:00 UTC daily).

## One‑time setup

### 1. DB migration
Run the new `push_subscriptions` block from `backend/schema.sql` against Supabase
(the `CREATE TABLE IF NOT EXISTS` + `ALTER TABLE … ENABLE ROW LEVEL SECURITY`).

### 2. Generate VAPID keys
```bash
python - <<'PY'
from cryptography.hazmat.primitives.asymmetric import ec
from cryptography.hazmat.primitives import serialization
import base64
b64=lambda b: base64.urlsafe_b64encode(b).rstrip(b'=').decode()
k=ec.generate_private_key(ec.SECP256R1())
print("VAPID_PUBLIC_KEY="+b64(k.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)))
print("VAPID_PRIVATE_KEY="+b64(k.private_numbers().private_value.to_bytes(32,'big')))
PY
```

### 3. Env vars on the Vercel **backend** project
| Var | Value |
|---|---|
| `VAPID_PUBLIC_KEY` | from step 2 |
| `VAPID_PRIVATE_KEY` | from step 2 (secret) |
| `VAPID_SUBJECT` | `mailto:you@example.com` |
| `CRON_SECRET` | any long random string |

Then redeploy the backend (`cd backend && vercel --prod`). The light
`requirements.txt` now includes `pywebpush`.

### 4. GitHub repo secrets (for the scheduler)
| Secret | Value |
|---|---|
| `CRON_SECRET` | **same** value as on Vercel |
| `BACKEND_URL` | optional; defaults to the prod backend URL in the workflow |

## Verify
1. Open the app → **Coach** page → click **🔕 Daily alerts** → allow notifications
   → you should get a test push ("notifications on").
2. Manually fire the job: Actions tab → **Daily Insights Push** → *Run workflow*.
   It should return `{"ok": true, "notifications_sent": N, ...}`.

## Notes
- Push degrades gracefully: with no VAPID keys the endpoints return `configured:false`
  and the `NotifyToggle` hides itself, so nothing breaks pre‑config.
- iOS requires the PWA be **installed to the Home Screen** before web push works
  (Safari limitation); Android/desktop Chrome work in‑browser.
- Alternative scheduler: Vercel Cron (`vercel.json` `crons`) hitting the same
  endpoint — the GitHub Actions route avoids touching the zero‑config Vercel setup.
