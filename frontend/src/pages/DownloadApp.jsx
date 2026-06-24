import { useEffect, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import './DownloadApp.css';

const VERSION_URL =
	'https://raw.githubusercontent.com/PyMite6941/Fitness-AI-Agents/main/mobile/version.json';

function detectOS() {
	const ua = navigator.userAgent || '';
	if (/Android/i.test(ua)) return 'android';
	if (/iPhone|iPad|iPod/i.test(ua)) return 'ios';
	return 'desktop';
}

export default function DownloadApp() {
	const navigate = useNavigate();
	const [os] = useState(detectOS());
	const [meta, setMeta] = useState(null);
	const [err, setErr] = useState('');

	// Live version check straight from the GitHub repo (same source the app uses).
	useEffect(() => {
		fetch(VERSION_URL, { cache: 'no-store' })
			.then(r => r.json())
			.then(setMeta)
			.catch(() => setErr('Could not reach GitHub to check the latest version.'));
	}, []);

	const app = meta?.androidApp;
	const apkUrl = app?.apkUrl || '#';

	return (
		<div className='dl-page'>
			<button className='dl-back' onClick={() => navigate('/')}>← Back</button>

			<header className='dl-head'>
				<h1>Get the FitnessAI tracker</h1>
				<p>
					Track steps &amp; routes on your Android phone — even with no watch. Data syncs to
					your account and is analyzed on the web when you ask. No app store: you install the
					app directly, and it auto-checks GitHub for updates.
				</p>
				{app && (
					<div className='dl-version'>
						Latest: <strong>v{app.version}</strong> · build {app.versionCode} · ~{app.sizeMB} MB
						· Android {app.minAndroid}+
					</div>
				)}
				{err && <div className='dl-version dl-warn'>{err}</div>}
			</header>

			{/* ANDROID — the real path */}
			{os === 'android' && (
				<div className='dl-card dl-primary'>
					<span className='dl-tag'>ANDROID — INSTALL ON THIS PHONE</span>
					<h2>Install in 3 taps</h2>
					<a className='dl-btn' href={apkUrl} target='_blank' rel='noreferrer'>
						⬇ Download &amp; Install{app ? ` (v${app.version})` : ''}
					</a>
					<ol className='dl-steps dl-steps-tight'>
						<li>Tap <strong>Download &amp; Install</strong> above.</li>
						<li>Open the downloaded file. Android will ask to <strong>allow installs from your browser</strong> this once — tap <em>Settings → Allow</em>. That's normal for off-store apps.</li>
						<li>Tap <strong>Install</strong>, then <strong>Open</strong>.</li>
					</ol>
				</div>
			)}

			{/* iOS — not possible natively */}
			{os === 'ios' && (
				<div className='dl-card dl-note'>
					<span className='dl-tag'>IPHONE</span>
					<h2>No background tracker on iPhone</h2>
					<p>
						Apple blocks background sensor tracking for off-store apps, so there's no iPhone
						tracker. Instead, connect a source from your dashboard — <strong>Apple Health
						export, Strava, or Fitbit</strong> — and the AI analyzes that data the same way.
					</p>
					<button className='dl-btn dl-btn-ghost' onClick={() => navigate('/')}>Connect a source instead</button>
				</div>
			)}

			{/* DESKTOP — tell them to open on the phone */}
			{os === 'desktop' && (
				<div className='dl-card dl-primary'>
					<span className='dl-tag'>OPEN THIS ON YOUR ANDROID PHONE</span>
					<h2>The tracker installs on the phone</h2>
					<p className='dl-sub'>
						Scan this QR or open <code>{typeof window !== 'undefined' ? window.location.host : ''}/app</code> in
						your Android phone's browser, then tap Download &amp; Install.
					</p>
					{app && (
						<img
							className='dl-qr'
							alt='QR to this page'
							src={`https://api.qrserver.com/v1/create-qr-code/?size=180x180&bgcolor=20-20-20&color=255-60-0&data=${encodeURIComponent((typeof window !== 'undefined' ? window.location.origin : '') + '/app')}`}
						/>
					)}
					<a className='dl-btn dl-btn-ghost' href={apkUrl} target='_blank' rel='noreferrer'>
						Or download the APK directly
					</a>
				</div>
			)}

			{/* After installing — pairing */}
			<div className='dl-card'>
				<h3>Link it to your account</h3>
				<ol className='dl-steps'>
					<li>On the web app, go to <strong>Settings → Pair a device</strong> and copy the code.</li>
					<li>Paste the code into the app — it's stored securely in the Android Keystore (not a password).</li>
					<li>Allow <strong>Activity &amp; Location</strong> so it can track in the background.</li>
					<li>It tracks locally and uploads to your account when Wi-Fi or data returns.</li>
				</ol>
			</div>

			<div className='dl-card dl-security'>
				<h3>Safety &amp; updates</h3>
				<ul>
					<li>Open source — the APK is built from this GitHub repo.</li>
					<li>On launch the app reads GitHub's <code>version.json</code> and offers the update when a newer build exists.</li>
					<li>No analysis runs on your phone; it only collects and uploads your own data.</li>
					<li>The account link is a revocable pairing token, not your login.</li>
				</ul>
			</div>
		</div>
	);
}
