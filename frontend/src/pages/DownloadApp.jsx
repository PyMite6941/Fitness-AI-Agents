import { useEffect, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import './DownloadApp.css';

// Served by the public website (the GitHub repo is private).
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
	const [installPrompt, setInstallPrompt] = useState(null);
	const [installed, setInstalled] = useState(false);
	const origin = typeof window !== 'undefined' ? window.location.origin : '';

	useEffect(() => {
		fetch(VERSION_URL, { cache: 'no-store' }).then(r => r.json()).then(setMeta).catch(() => {});
		// Capture the browser's install prompt (Android Chrome + desktop Chrome/Edge).
		const onPrompt = (e) => { e.preventDefault(); setInstallPrompt(e); };
		const onInstalled = () => { setInstalled(true); setInstallPrompt(null); };
		window.addEventListener('beforeinstallprompt', onPrompt);
		window.addEventListener('appinstalled', onInstalled);
		return () => {
			window.removeEventListener('beforeinstallprompt', onPrompt);
			window.removeEventListener('appinstalled', onInstalled);
		};
	}, []);

	const app = meta?.androidApp;
	const apkReady = app?.available === true;            // the native .apk binary exists
	const ios = meta?.iosApp;
	const ipaReady = ios?.available === true;            // the unsigned .ipa exists
	const cls = (id) => `dl-card dl-device${os === id ? ' dl-detected' : ''}`;

	async function installPWA() {
		if (!installPrompt) return;
		installPrompt.prompt();
		try { await installPrompt.userChoice; } catch { /* dismissed */ }
		setInstallPrompt(null);
	}

	const PwaButton = () => (
		installed
			? <span className='dl-installed'>✓ Installed — open it from your home screen</span>
			: installPrompt
				? <button className='dl-btn' onClick={installPWA}>📲 Install the app</button>
				: null
	);

	return (
		<div className='dl-page'>
			<button className='dl-back' onClick={() => navigate('/')}>← Back</button>

			<header className='dl-head'>
				<h1>Install FitnessAI</h1>
				<p>
					Install FitnessAI straight from your browser — no app store. You get the dashboard,
					AI analysis, source imports, and GPS tracking. Pick your device below; the one you're
					on is highlighted.
				</p>
			</header>

			{/* ─── ANDROID ─────────────────────────────────────────────── */}
			<div className={cls('android')}>
				<span className='dl-tag'>🤖 ANDROID{os === 'android' ? ' — YOUR DEVICE' : ''}</span>
				<h2>Install the app</h2>
				<p className='dl-sub'>
					Install FitnessAI from your browser as a real app on your home screen — works right now,
					no Play Store, no sideloading.
				</p>
				<PwaButton />
				{!installPrompt && !installed && (
					<ol className='dl-steps dl-steps-tight'>
						<li>Open this page in <strong>Chrome</strong> on your phone.</li>
						<li>Tap the <strong>⋮ menu</strong> (top-right) → <strong>Install app</strong> (or <em>Add to Home screen</em>).</li>
						<li>Tap <strong>Install</strong> — FitnessAI lands on your home screen.</li>
					</ol>
				)}

				{apkReady ? (
					<a className='dl-btn dl-btn-ghost' href={app.apkUrl} target='_blank' rel='noreferrer'>
						⬇ Native tracker (.apk) v{app.version}
					</a>
				) : (
					<p className='dl-soon'>
						🔧 <strong>Native background tracker (.apk) — coming soon.</strong> Counting steps &amp;
						GPS with the screen off needs the native build (in progress). The installable app
						above already does everything else.
					</p>
				)}
			</div>

			{/* ─── IPHONE / IPAD ───────────────────────────────────────── */}
			<div className={cls('ios')}>
				<span className='dl-tag'>🍎 IPHONE &amp; IPAD{os === 'ios' ? ' — YOUR DEVICE' : ''}</span>
				<h2>Add to Home Screen</h2>
				<p className='dl-sub'>
					iOS has no app-store-free install prompt, so you add it via Safari's Share menu. You get
					the full dashboard, AI analysis, and foreground GPS — plus Apple Health import.
				</p>
				<ol className='dl-steps dl-steps-tight'>
					<li>Open <strong>{origin.replace(/^https?:\/\//, '') || 'this site'}</strong> in <strong>Safari</strong> — it must be Safari; Chrome on iPhone can't add to the Home Screen.</li>
					<li>Tap the <strong>Share</strong> button — the square with an up-arrow <span className='dl-mono'>⬆</span> at the bottom of the screen.</li>
					<li>Scroll down and tap <strong>Add to Home Screen</strong>, then tap <strong>Add</strong> (top-right).</li>
					<li>Open the new <strong>FitnessAI</strong> icon and sign in.</li>
					<li>For Apple Watch / iPhone data: Apple's <strong>Health</strong> app → your <strong>profile photo</strong> → <strong>Export All Health Data</strong> → upload the <code>export.zip</code> on the dashboard.</li>
				</ol>

				{ipaReady ? (
					<div className='dl-ipa'>
						<p className='dl-ipa-title'>Or install the native app (.ipa) — free sideload</p>
						<a className='dl-btn' href={ios.ipaUrl} target='_blank' rel='noreferrer'>⬇ Download .ipa{ios.version ? ` (v${ios.version})` : ''}</a>
						<ol className='dl-steps dl-steps-tight'>
							<li>Install a free sideloader once: <strong>AltStore</strong> / <strong>SideStore</strong> (altstore.io) or <strong>Sideloadly</strong>.</li>
							<li>Open the downloaded <code>fitness-ai.ipa</code> in it and sign in with your <strong>own free Apple ID</strong> — it re-signs the app onto your phone (renew every 7 days).</li>
						</ol>
						<p className='dl-soon'>Note: full <strong>Apple Health</strong> access needs a paid Apple Developer account; on a free Apple ID the app installs and pairs, but Health import may be limited. The PWA above has no such limit.</p>
					</div>
				) : (
					<p className='dl-soon'>🔧 A sideloadable <strong>.ipa</strong> is on the way (built free in CI). For now the Home-Screen PWA above gives you the full app.</p>
				)}
			</div>

			{/* ─── DESKTOP ─────────────────────────────────────────────── */}
			<div className={cls('desktop')}>
				<span className='dl-tag'>💻 WINDOWS · MAC · LINUX{os === 'desktop' ? ' — YOUR DEVICE' : ''}</span>
				<h2>Install the app</h2>
				<p className='dl-sub'>
					Install FitnessAI as a desktop app from Chrome or Edge. Or scan the code to open this
					page on your phone.
				</p>
				<PwaButton />
				{!installPrompt && !installed && (
					<p className='dl-sub'>In Chrome/Edge, click the <strong>Install</strong> icon in the address bar (or menu → <em>Install FitnessAI</em>).</p>
				)}
				{origin && (
					<img
						className='dl-qr'
						alt='Scan to open this page on your phone'
						src={`https://api.qrserver.com/v1/create-qr-code/?size=170x170&bgcolor=20-20-20&color=255-60-0&data=${encodeURIComponent(origin + '/app')}`}
					/>
				)}
			</div>

			{/* Pairing — only relevant once the native tracker ships */}
			<div className='dl-card'>
				<h3>Pair a phone to your account <span className='dl-soon-badge'>native tracker · soon</span></h3>
				<p className='dl-sub'>
					When the native background tracker ships, you'll link it to your account with a one-time
					code (no password on the device):
				</p>
				<ol className='dl-steps'>
					<li>Web app → <strong>Devices</strong> (sidebar) → <strong>Generate pairing code</strong> (a <code>fit_</code> code, also shown as a QR).</li>
					<li>Paste it into the phone app — stored in the device's secure storage (Android Keystore / iOS Keychain), and revocable anytime.</li>
					<li>The phone tracks locally and uploads to your account when Wi-Fi or data returns. Analysis only runs on the web, when you ask — never on the phone.</li>
				</ol>
			</div>

			<div className='dl-card dl-security'>
				<h3>Safety &amp; privacy</h3>
				<ul>
					<li>Installed straight from the website — no app store, no extra account.</li>
					<li>No analysis runs on your device; it only collects and uploads your own data.</li>
					<li>A paired device uses a revocable token, never your login.</li>
				</ul>
			</div>
		</div>
	);
}
