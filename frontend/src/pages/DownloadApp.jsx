import { useEffect, useState } from 'react';
import { useNavigate } from 'react-router-dom';
import './DownloadApp.css';

const VERSION_URL =
	'https://raw.githubusercontent.com/PyMite6941/Fitness-AI-Agents/main/mobile/version.json';

function detectOS() {
	const ua = navigator.userAgent || '';
	if (/Android/i.test(ua)) return 'android';
	if (/iPhone|iPad|iPod/i.test(ua)) return 'ios';
	if (/Windows/i.test(ua)) return 'windows';
	if (/Mac/i.test(ua)) return 'mac';
	if (/Linux/i.test(ua)) return 'linux';
	return 'other';
}

export default function DownloadApp() {
	const navigate = useNavigate();
	const [os] = useState(detectOS());
	const [meta, setMeta] = useState(null);
	const [err, setErr] = useState('');
	const [copied, setCopied] = useState('');

	const origin = typeof window !== 'undefined' ? window.location.origin : '';

	// Live version check straight from the GitHub repo.
	useEffect(() => {
		fetch(VERSION_URL, { cache: 'no-store' })
			.then(r => r.json())
			.then(setMeta)
			.catch(() => setErr('Could not reach GitHub to check the latest version.'));
	}, []);

	const app = meta?.androidApp;
	const winCmd = `irm ${origin}/install.ps1 | iex`;
	const nixCmd = `curl -fsSL ${origin}/install.sh | bash`;
	const desktopCmd = os === 'windows' ? winCmd : nixCmd;
	const isDesktop = ['windows', 'mac', 'linux'].includes(os);

	function copy(text, which) {
		navigator.clipboard?.writeText(text).then(() => {
			setCopied(which);
			setTimeout(() => setCopied(''), 1600);
		});
	}

	return (
		<div className='dl-page'>
			<button className='dl-back' onClick={() => navigate('/')}>← Back</button>

			<header className='dl-head'>
				<h1>Get the FitnessAI tracker</h1>
				<p>
					Track steps &amp; routes on your phone — even with no watch. Data syncs to your
					account and is analyzed on the web when you ask. No app store; the installer pulls
					straight from GitHub and auto-checks for updates.
				</p>
				{app && (
					<div className='dl-version'>
						Latest: <strong>v{app.version}</strong> · build {app.versionCode} · ~{app.sizeMB} MB
						· Android {app.minAndroid}+
					</div>
				)}
				{err && <div className='dl-version dl-warn'>{err}</div>}
			</header>

			{os === 'ios' && (
				<div className='dl-card dl-note'>
					<strong>iPhone:</strong> a background tracker isn't possible in a sideloaded iOS app
					(Apple restricts it). On iPhone, connect a source instead — Apple Health export,
					Strava, or Fitbit — from your dashboard. The Android tracker below is for Android phones.
				</div>
			)}

			{/* Recommended path for the visitor's OS */}
			<div className='dl-card dl-primary'>
				<span className='dl-tag'>{isDesktop ? `DETECTED: ${os.toUpperCase()}` : os === 'android' ? 'DETECTED: ANDROID' : 'INSTALL'}</span>

				{isDesktop && (
					<>
						<h2>One-line install (phone connected by USB)</h2>
						<p className='dl-sub'>
							Enable USB debugging on your phone, connect it, then run this. It checks your
							installed version against GitHub and only updates when there's something newer.
						</p>
						<div className='dl-cmd'>
							<code>{desktopCmd}</code>
							<button onClick={() => copy(desktopCmd, 'cmd')}>{copied === 'cmd' ? '✓' : 'Copy'}</button>
						</div>
						<p className='dl-or'>…or download the APK and put it on the phone yourself:</p>
					</>
				)}

				{os === 'android' && (
					<>
						<h2>Install on this phone</h2>
						<p className='dl-sub'>
							Tap download, then open the file. You'll be asked to allow installing from your
							browser this once (Settings → Install unknown apps) — that's normal for
							off-store apps.
						</p>
					</>
				)}

				<a className='dl-btn' href={app?.apkUrl || '#'} target='_blank' rel='noreferrer'>
					⬇ Download APK{app ? ` (v${app.version})` : ''}
				</a>
			</div>

			{/* The other OS option */}
			{isDesktop && (
				<details className='dl-card'>
					<summary>On macOS / Linux instead</summary>
					<div className='dl-cmd'>
						<code>{nixCmd}</code>
						<button onClick={() => copy(nixCmd, 'nix')}>{copied === 'nix' ? '✓' : 'Copy'}</button>
					</div>
				</details>
			)}

			{/* Sideload + pairing steps */}
			<div className='dl-card'>
				<h3>After installing</h3>
				<ol className='dl-steps'>
					<li>Open <strong>FitnessAI</strong> on your phone.</li>
					<li>On the web app, go to <strong>Settings → Pair a device</strong> and copy the code.</li>
					<li>Paste the code into the app to link it to your account (stored securely on-device).</li>
					<li>Allow Activity &amp; Location permissions so it can track in the background.</li>
					<li>That's it — it tracks locally and syncs when you have Wi-Fi or data.</li>
				</ol>
			</div>

			<div className='dl-card dl-security'>
				<h3>Safety &amp; updates</h3>
				<ul>
					<li>Open source — the installer and APK are built from this GitHub repo.</li>
					<li>The app checks GitHub's <code>version.json</code> on launch and tells you when an update is out.</li>
					<li>No live analysis runs on your phone; it only collects and uploads your own data.</li>
					<li>Your account link is a pairing token kept in the Android Keystore, not a password.</li>
				</ul>
			</div>
		</div>
	);
}
