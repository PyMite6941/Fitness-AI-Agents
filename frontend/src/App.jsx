import { SignInButton, SignUpButton, UserButton, useAuth } from '@clerk/react';
import { useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import './App.css';

const features = [
	{
		icon: '📱',
		title: 'Import From Any Source',
		desc: 'Strava, Fitbit, Garmin, Apple Health, Nike Run Club, Google Fit, COROS, Suunto, Wahoo, Polar, Zwift, Peloton — or any .fit/.tcx/.gpx file.',
	},
	{
		icon: '🔄',
		title: 'Merge Everything',
		desc: 'Data from your Apple Watch, Garmin, Strava, and manual logs all gets merged into one unified view. No duplicates, no gaps.',
	},
	{
		icon: '🤖',
		title: 'Multi-Agent AI Analysis',
		desc: 'A pipeline of specialized AI agents cleans, interprets, and turns your raw data into clear insights — with source-level awareness.',
	},
	{
		icon: '📈',
		title: 'Trend Detection',
		desc: 'Spot patterns across weeks and months — recovery dips, performance peaks, sleep impact on output.',
	},
	{
		icon: '🎯',
		title: 'Actionable Recommendations',
		desc: 'Not just charts. Every analysis ends with specific, data-backed steps you can take today.',
	},
];

const steps = [
	{ number: '01', title: 'Connect Your Sources', desc: 'Sync Strava, Fitbit, or Garmin via OAuth. Import from Apple Health, Nike Run Club, or Google Fit. Log manually. We merge everything by source.' },
	{ number: '02', title: 'Ask a Question', desc: 'Tell the AI what you want to know — in plain English, no technical knowledge needed.' },
	{ number: '03', title: 'Get Your Analysis', desc: 'Receive a full breakdown with findings, anomalies, and recommendations — aware of which source each insight came from.' },
];

export default function FitnessAI() {
	const { isSignedIn, isLoaded } = useAuth();
	const navigate = useNavigate();

	useEffect(() => {
		if (isLoaded && isSignedIn) navigate('/dashboard', { replace: true });
	}, [isLoaded, isSignedIn]);

	return (
		<div className='page'>

			<nav className='nav'>
				<span className='nav-logo'>FitnessAI</span>
				<div className='nav-links'>
					<a href='#how-it-works'>How It Works</a>
					<a href='#features'>Features</a>
					<a href='#demo'>Live Demo</a>
					<a href='/demo'>Sample Dashboard</a>
					<a href='/app'>Get the App</a>
				</div>
				<div className='nav-auth'>
					{!isSignedIn ? (
						<>
							<SignInButton mode="modal"><button className='nav-btn'>Log In</button></SignInButton>
							<SignUpButton mode="modal"><button className='nav-btn nav-btn-primary'>Sign Up</button></SignUpButton>
						</>
					) : (
						<UserButton />
					)}
				</div>
			</nav>

			{/* HERO */}
			<section className='hero'>
				<div className='hero-overlay' />
				<div className='hero-content'>
					<p className='hero-tag'>ALL YOUR DATA. ONE ANALYSIS.</p>
					<h1>TRAIN SMARTER.<br />RECOVER FASTER.</h1>
					<p className='hero-sub'>
						Connect Strava, Apple Watch, Garmin, Fitbit — or import from Nike, Google Fit, Apple Health. Every source makes your AI analysis stronger.
					</p>
					<div className='hero-actions'>
						{!isSignedIn ? (
							<SignUpButton mode="modal">
								<button className='hero-btn'>START FOR FREE</button>
							</SignUpButton>
						) : (
							<a href='/dashboard' className='hero-btn'>GO TO DASHBOARD</a>
						)}
						<a href='#demo' className='hero-btn-ghost'>TRY THE LIVE AI →</a>
					</div>
				</div>
				<div className='hero-scroll'>
					<span>SCROLL</span>
					<div className='hero-scroll-line' />
				</div>
			</section>

			{/* STATS BAR */}
			<div className='stats-bar'>
				<div className='stat'><strong>13+</strong><span>Platform Integrations</span></div>
				<div className='stat-divider' />
				<div className='stat'><strong>.fit/.tcx/.gpx</strong><span>Universal Import</span></div>
				<div className='stat-divider' />
				<div className='stat'><strong>&lt;30s</strong><span>Analysis Time</span></div>
				<div className='stat-divider' />
				<div className='stat'><strong>100%</strong><span>Your Data</span></div>
			</div>

			{/* HOW IT WORKS */}
			<section className='section' id='how-it-works'>
				<div className='section-inner'>
					<p className='section-tag'>THE PROCESS</p>
					<h2 className='section-heading'>Three steps to better training</h2>
					<div className='steps'>
						{steps.map((s) => (
							<div className='step' key={s.number}>
								<span className='step-number'>{s.number}</span>
								<h3>{s.title}</h3>
								<p>{s.desc}</p>
							</div>
						))}
					</div>
				</div>
			</section>

			{/* FEATURES */}
			<section className='section section-dark' id='features'>
				<div className='section-inner'>
					<p className='section-tag'>CAPABILITIES</p>
					<h2 className='section-heading'>Everything your coach wishes they had</h2>
					<div className='features'>
						{features.map((f) => (
							<div className='feature-card' key={f.title}>
								<span className='feature-icon'>{f.icon}</span>
								<h3>{f.title}</h3>
								<p>{f.desc}</p>
							</div>
						))}
					</div>
				</div>
			</section>

			{/* LIVE DEMO */}
			<section className='section' id='demo'>
				<div className='section-inner'>
					<p className='section-tag'>TRY IT NOW</p>
					<h2 className='section-heading'>Run the AI — no signup required</h2>
					<p className='demo-sub'>
						This is the real multi-agent pipeline. Hit <strong>Run Analysis</strong> with the
						built-in sample data for an instant breakdown, or upload your own CSV to run the
						full 8-agent crew live.
					</p>
					<div className='demo-frame-wrap'>
						<iframe
							title='Fitness AI Agents — live demo'
							src='https://pymite6941-fitness-ai-agents-demo.hf.space'
							className='demo-frame'
							loading='lazy'
							allow='clipboard-write'
						/>
					</div>
					<a
						className='hero-btn-ghost'
						href='https://pymite6941-fitness-ai-agents-demo.hf.space'
						target='_blank'
						rel='noreferrer'
					>
						OPEN DEMO IN NEW TAB ↗
					</a>
				</div>
			</section>

			{/* CTA */}
			<section className='cta-section'>
				<div className='cta-inner'>
					<p className='section-tag'>GET STARTED</p>
					<h2>Every workout tells a story.<br />Start reading yours.</h2>
					{!isSignedIn ? (
						<SignUpButton mode="modal">
							<button className='hero-btn'>CREATE FREE ACCOUNT</button>
						</SignUpButton>
					) : (
						<a href='/dashboard' className='hero-btn'>GO TO DASHBOARD</a>
					)}
				</div>
			</section>

			{/* FOOTER */}
			<footer className='footer'>
				<div className='footer-col'>
					<span className='nav-logo'>FitnessAI</span>
					<p>© 2026 FitnessAI. All rights reserved.</p>
					<a href='/privacy' className='footer-privacy'>Privacy &amp; your data</a>
				</div>
				<div className='footer-links'>
					<div className='footer-group'>
						<h4>Try it</h4>
						<a href='/demo'>Sample Dashboard</a>
						<a href='#demo'>Live AI Demo</a>
						<a href='https://pymite6941-fitness-ai-agents-demo.hf.space' target='_blank' rel='noreferrer'>Agent Demo (HuggingFace) ↗</a>
					</div>
					<div className='footer-group'>
						<h4>Apps</h4>
						<a href='/app'>Android tracker</a>
						<a href='/app'>iPhone (Add to Home Screen)</a>
						<a href='/app'>Desktop app</a>
					</div>
				</div>
			</footer>

		</div>
	);
}
