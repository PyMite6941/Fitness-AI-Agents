import { SignInButton, SignUpButton, UserButton, useAuth } from '@clerk/react';
import { useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import './App.css';

const features = [
	{
		icon: '⌚',
		title: 'Sync From Any Device',
		desc: 'Apple Watch, Garmin, Fitbit — connect your wearable and your data flows in automatically.',
	},
	{
		icon: '🤖',
		title: 'Multi-Agent AI Analysis',
		desc: 'A pipeline of specialized AI agents cleans, interprets, and turns your raw data into clear insights.',
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
	{ number: '01', title: 'Connect Your Watch', desc: 'Link your wearable or upload a data export. We support all major formats.' },
	{ number: '02', title: 'Ask a Question', desc: 'Tell the AI what you want to know — in plain English, no technical knowledge needed.' },
	{ number: '03', title: 'Get Your Analysis', desc: 'Receive a full breakdown with findings, anomalies, and recommendations in seconds.' },
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
					<p className='hero-tag'>AI-POWERED PERFORMANCE</p>
					<h1>TRAIN SMARTER.<br />RECOVER FASTER.</h1>
					<p className='hero-sub'>
						Connect your wearable. Ask a question. Get AI-powered insights built from your actual fitness data.
					</p>
					<div className='hero-actions'>
						{!isSignedIn ? (
							<SignUpButton mode="modal">
								<button className='hero-btn'>START FOR FREE</button>
							</SignUpButton>
						) : (
							<a href='/dashboard' className='hero-btn'>GO TO DASHBOARD</a>
						)}
						<a href='#how-it-works' className='hero-btn-ghost'>SEE HOW IT WORKS</a>
					</div>
				</div>
				<div className='hero-scroll'>
					<span>SCROLL</span>
					<div className='hero-scroll-line' />
				</div>
			</section>

			{/* STATS BAR */}
			<div className='stats-bar'>
				<div className='stat'><strong>12+</strong><span>Data Sources</span></div>
				<div className='stat-divider' />
				<div className='stat'><strong>4</strong><span>AI Agents</span></div>
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

			{/* CTA */}
			<section className='cta-section'>
				<div className='cta-inner'>
					<p className='section-tag'>GET STARTED</p>
					<h2>Your data has answers.<br />Start asking.</h2>
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
				<span className='nav-logo'>FitnessAI</span>
				<p>© 2026 FitnessAI. All rights reserved.</p>
			</footer>

		</div>
	);
}
