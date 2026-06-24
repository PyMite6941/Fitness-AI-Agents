import { useEffect, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import './DownloadApp.css';

// Served by the website (public) — NOT GitHub raw, because the repo is private.
const VERSION_URL = '/version.json';

function detectOS() {
	const ua = navigator.userAgent || '';
	if (/Android/i.test(ua)) return 'android';
	if (/iPhone|iPad|iPod/i.test(ua) || (/Mac/i.test(ua) && navigator.maxTouchPoints > 1)) return 'ios';
	return 'desktop';
}

export default function DownloadApp() {
	const navigate = useNavigate();
	const [os] = useState(detectOS());
	const [meta, setMeta] = useState(null);
	const [err, setErr] = useState('');
	const origin = typeof window !== 'undefined' ? window.location.origin : '';

	useEffect(() => {
		fetch(VERSION_URL, { cache: 'no-store' })
			.then(r => r.json()).then(setMeta)
			.catch(() => setErr('Could not reach GitHub to check the latest version.'));
	}, []);

	const app = meta?.androidApp;
	const apkUrl = app?.apkUrl || '#';
	const cls = (id) => `dl-card dl-device${os === id ? ' dl-detected' : ''}`;

	return (
		<div className='dl-page'>
			<button className='dl-back' onClick={() => navigate('/')}>← Back</button>

			<header className='dl-head'>
				<h1>Install FitnessAI</h1>
				<p>
					Get FitnessAI on any device — no app store. Track on your phone (even with no
					watch), then analyze on the web. Pick your device below; the one you're on is
					highlighted.
				</p>
				{app && (
					<div className='dl-version'>
						Android app: <strong>v{app.version}</strong> · build {app.versionCode} ·
						~{app.sizeMB} MB · auto-updates from GitHub
					</div>
				)}
				{err && <div className='dl-version dl-warn'>{err}</div>}
			</header>

			{/* ─── ANDROID ─────────────────────────────────────────────── */}
			<div className={cls('android')}>
				<span className='dl-tag'>🤖 ANDROID{os === 'android' ? ' — YOUR DEVICE' : ''}</span>
				<h2>Native tracker app (.apk)</h2>
				<p className='dl-sub'>
					Full background tracking — steps &amp; GPS even with the screen off and no watch.
					Installs directly (sideload); checks GitHub for updates on launch.
				</p>
				<a className='dl-btn' href={apkUrl} target='_blank' rel='noreferrer'>
					⬇ Download &amp; Install{app ? ` (v${app.version})` : ''}
				</a>
				<ol className='dl-steps dl-steps-tight'>
					<li>Tap <strong>Download &amp; Install</strong> above. Your phone downloads <code>fitness-ai.apk</code> (~4 MB). If it warns <em>“this type of file can harm your device”</em>, that's the normal notice for any app outside the Play Store — tap <strong>Download anyway</strong>.</li>
					<li>Open the file: tap the download notification, or go to <strong>Files → Downloads</strong> and tap <code>fitness-ai.apk</code>.</li>
					<li>If Android says installing from this source isn't allowed, tap <strong>Settings</strong> on that popup, turn on <strong>Allow from this source</strong>, then tap back. You only do this once.</li>
					<li>Tap <strong>Install</strong>, wait a few seconds, then tap <strong>Open</strong>.</li>
					<li>In the app, paste your pairing code (next section) and tap <strong>Pair this phone</strong>. When asked, tap <strong>Allow</strong> for <em>Physical activity</em> and <em>Location</em> so it can count steps and record routes.</li>
				</ol>
			</div>

			{/* ─── IPHONE / IPAD ───────────────────────────────────────── */}
			<div className={cls('ios')}>
				<span className='dl-tag'>🍎 IPHONE &amp; IPAD{os === 'ios' ? ' — YOUR DEVICE' : ''}</span>
				<h2>Add to Home Screen (PWA)</h2>
				<p className='dl-sub'>
					Apple blocks background sensors for off-store apps, so there's no always-on tracker
					on iOS. Instead, install FitnessAI as a home-screen app and connect Apple Health —
					you get the full dashboard, AI analysis, and foreground GPS tracking.
				</p>
				<ol className='dl-steps dl-steps-tight'>
					<li>Open <strong>{origin.replace(/^https?:\/\//, '') || 'this site'}</strong> in <strong>Safari</strong> — it must be Safari; Chrome on iPhone can't add to the Home Screen.</li>
					<li>Tap the <strong>Share</strong> button — the square with an up-arrow <span className='dl-mono'>⬆</span> at the bottom of the screen.</li>
					<li>Scroll down the share sheet and tap <strong>Add to Home Screen</strong>, then tap <strong>Add</strong> (top-right).</li>
					<li>Open the new <strong>FitnessAI</strong> icon from your Home Screen and sign in.</li>
					<li>To bring in Apple Watch / iPhone data: open Apple's <strong>Health</strong> app → tap your <strong>profile photo</strong> (top-right) → <strong>Export All Health Data</strong> → save the <code>export.zip</code>, then upload it on the FitnessAI dashboard.</li>
				</ol>
				<button className='dl-btn dl-btn-ghost' onClick={() => navigate('/')}>Open the web app</button>
			</div>

			{/* ─── DESKTOP ─────────────────────────────────────────────── */}
			<div className={cls('desktop')}>
				<span className='dl-tag'>💻 WINDOWS · MAC · LINUX{os === 'desktop' ? ' — YOUR DEVICE' : ''}</span>
				<h2>Install the web app</h2>
				<p className='dl-sub'>
					Use FitnessAI as a desktop app: in Chrome or Edge, click the <strong>Install</strong>
					icon in the address bar (or menu → <em>Install FitnessAI</em>). To get the Android
					tracker, scan this on your phone:
				</p>
				{origin && (
					<img
						className='dl-qr'
						alt='Scan to open the install page on your phone'
						src={`https://api.qrserver.com/v1/create-qr-code/?size=170x170&bgcolor=20-20-20&color=255-60-0&data=${encodeURIComponent(origin + '/app')}`}
					/>
				)}
				<button className='dl-btn dl-btn-ghost' onClick={() => navigate('/')}>Open the web app</button>
			</div>

			{/* Pairing + safety */}
			<div className='dl-card'>
				<h3>Link a phone to your account</h3>
				<ol className='dl-steps'>
					<li>On a computer or in any browser, open the FitnessAI web app and sign in.</li>
					<li>Go to <strong>Settings → Pair a device</strong> and tap <strong>Generate code</strong>. A code starting with <code>fit_</code> appears (you can also scan it as a QR).</li>
					<li>In the phone app, paste that code into the <strong>Pairing code</strong> box and tap <strong>Pair this phone</strong>. The code is saved in your phone's secure storage (Android Keystore / iOS Keychain) — it's a revocable key, never your password.</li>
					<li>Done. The phone tracks locally and uploads to your account whenever Wi-Fi or mobile data is available. Analysis only runs on the web, when you ask for it — never on the phone.</li>
					<li>Lost your phone? In <strong>Settings → Pair a device</strong>, tap <strong>Revoke</strong> next to it to cut it off instantly.</li>
				</ol>
			</div>

			<div className='dl-card dl-security'>
				<h3>Safety &amp; updates</h3>
				<ul>
					<li>Open source — built from this GitHub repo; the APK auto-checks GitHub's <code>version.json</code>.</li>
					<li>No analysis runs on your device; it only collects and uploads your own data.</li>
					<li>The account link is a revocable pairing token, not your login.</li>
				</ul>
			</div>
		</div>
	);
}
