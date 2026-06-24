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
					<li>Tap <strong>Download &amp; Install</strong>, then open the file.</li>
					<li>Allow installs from your browser this once (<em>Settings → Allow</em>) — normal for off-store apps.</li>
					<li>Open <strong>FitnessAI</strong>, paste your pairing code, allow Activity &amp; Location.</li>
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
					<li>Open <strong>{origin.replace(/^https?:\/\//, '') || 'this site'}</strong> in <strong>Safari</strong>.</li>
					<li>Tap <strong>Share</strong> <span className='dl-mono'>⬆</span> → <strong>Add to Home Screen</strong> → <strong>Add</strong>.</li>
					<li>Open the FitnessAI icon, sign in, then <strong>Connect Apple Health</strong>: on iPhone, Health app → Profile → Export All Health Data, and upload the <code>export.zip</code> on the dashboard.</li>
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
					<li>On the web app: <strong>Settings → Pair a device</strong>, copy the code.</li>
					<li>Paste it into the Android app — stored in the Android Keystore, not a password.</li>
					<li>It tracks locally and uploads when Wi-Fi or data returns; analysis runs server-side on request.</li>
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
