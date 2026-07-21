import { useEffect, useState } from 'react';
import { useAuth } from '@clerk/react';
import { api } from '../lib/api';
import { pushSupported, isSubscribed, enablePush, disablePush } from '../lib/push';

// Compact toggle for the daily Readiness/Watchdog push notifications.
// Renders nothing when the browser can't do push or the server has no VAPID keys.
export default function NotifyToggle() {
	const { getToken } = useAuth();
	const [available, setAvailable] = useState(false); // browser + server both ready
	const [on, setOn] = useState(false);
	const [busy, setBusy] = useState(false);
	const [msg, setMsg] = useState('');

	useEffect(() => {
		let active = true;
		(async () => {
			if (!pushSupported()) return;
			try {
				const status = await api.getPushStatus(await getToken());
				if (!active) return;
				if (!status?.configured) return;         // server has no VAPID keys yet
				setAvailable(true);
				setOn(status.subscribed || (await isSubscribed()));
			} catch { /* stay hidden */ }
		})();
		return () => { active = false; };
	}, [getToken]);

	if (!available) return null;

	async function toggle() {
		setBusy(true); setMsg('');
		try {
			if (on) {
				await disablePush(getToken);
				setOn(false); setMsg('Daily alerts off');
			} else {
				await enablePush(getToken);
				setOn(true);
				try { await api.testPush(await getToken()); } catch { /* non-fatal */ }
				setMsg('Daily alerts on — sent you a test');
			}
		} catch (e) {
			setMsg(e.message || 'Could not change notifications');
		} finally {
			setBusy(false);
		}
	}

	return (
		<button
			className='coach-link'
			onClick={toggle}
			disabled={busy}
			title={on ? 'Turn off daily readiness & health alerts' : 'Get a daily readiness score + health alerts as a notification'}
		>
			{busy ? '…' : on ? '🔔 Alerts on' : '🔕 Daily alerts'}
			{msg && <span style={{ marginLeft: 6, opacity: 0.7, fontSize: '0.85em' }}>{msg}</span>}
		</button>
	);
}
