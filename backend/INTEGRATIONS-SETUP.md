# OAuth integrations — what YOU need to register + add

The code is done and live. Each OAuth provider just needs (1) a developer app
registered on their side, and (2) its client id/secret added as env vars on the
**Vercel `backend` project**. After adding env vars, redeploy the backend.

Backend base URL: `https://backend-seven-topaz-23.vercel.app`

| Provider | Register an app at | Redirect / Callback URL to enter | Scopes | Env vars to set |
|---|---|---|---|---|
| **Oura** | https://cloud.ouraring.com/oauth/applications | `https://backend-seven-topaz-23.vercel.app/integrations/oura/callback` | daily, heartrate, workout, personal, session | `OURA_CLIENT_ID`, `OURA_CLIENT_SECRET` |
| **WHOOP** | https://developer.whoop.com (Dashboard → Create app) | `https://backend-seven-topaz-23.vercel.app/integrations/whoop/callback` | read:recovery, read:sleep, read:workout, read:cycles, offline | `WHOOP_CLIENT_ID`, `WHOOP_CLIENT_SECRET` |
| Strava (already wired) | https://www.strava.com/settings/api | `.../integrations/strava/callback` | activity:read_all | `STRAVA_CLIENT_ID`, `STRAVA_CLIENT_SECRET` |
| Fitbit/Google Health (already wired) | https://console.cloud.google.com | `.../integrations/fitbit/callback` | (Google Health) | `GOOGLE_HEALTH_CLIENT_ID`, `GOOGLE_HEALTH_CLIENT_SECRET` |

## Step by step (Oura example — WHOOP is identical)
1. Go to the **register** URL above, create an application.
2. Set the **redirect/callback URL** to the exact one in the table (must match exactly).
3. Copy the **Client ID** and **Client Secret** it gives you.
4. Add them to the Vercel backend project:
   - Vercel → project **`backend`** → **Settings → Environment Variables** →
     add `OURA_CLIENT_ID` and `OURA_CLIENT_SECRET` (Production).
   - or CLI from `backend/`: `vercel env add OURA_CLIENT_ID production` (paste value), repeat.
5. **Redeploy** so the vars load: from `backend/` run `vercel --prod`.
6. Test: web app → **Log → Connect Apps → Connect Oura Ring** → approve → **Sync Now**.
   Synced data lands in `watch_data` under your account (device = `oura`/`whoop`).

## Notes
- No client id/secret set → the Connect button just bounces back with an OAuth error
  (the provider rejects an empty client). Nothing breaks; it's inert until configured.
- WHOOP **requires the `offline` scope** to issue a refresh token (already in the code).
- Data mapping follows each API's current docs; if a field name changed, adjust the
  `*_sync` function in `routes/integrations.py`.
