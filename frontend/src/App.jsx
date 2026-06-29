import { SignInButton, SignUpButton, UserButton, useAuth } from '@clerk/react';
import { useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import './App.css';
import { captureEvent } from './lib/analytics';
import { SIGN_UP_COMPLETE_REDIRECT } from './lib/authRedirects';

const features = [
	{
		label: 'Channel coverage',
		title: 'Meet clients where they ask',
		desc: 'Shape assistants for WhatsApp, Telegram, and Messenger so client questions can move through the channels trainers already use.',
	},
	{
		label: 'Knowledge base',
		title: 'Train it on your playbook',
		desc: 'Use workout routines, nutrition rules, onboarding notes, and gym policies as the source for more useful assistant replies.',
	},
	{
		label: 'Persona control',
		title: 'Keep the coach voice consistent',
		desc: 'Set a persona for the assistant so replies sound like the business instead of a generic chatbot.',
	},
	{
		label: 'Upgrade path',
		title: 'Start free, then scale',
		desc: 'Test with starter credits, then move to the $20/month Professional plan when client question volume justifies it.',
	},
];

const steps = [
	{ number: '01', title: 'Pick a question stream', desc: 'Start with the repeated questions clients already send about workouts, nutrition, timing, PRs, and setup.' },
	{ number: '02', title: 'Add the source material', desc: 'Give the assistant your routines, nutrition notes, gym policies, and preferred coaching persona so replies have a clear base.' },
	{ number: '03', title: 'Review and expand', desc: 'Test replies with starter credits, use them with a small client group, then expand the assistant once the answers are working.' },
];

const proofStats = [
	{ value: '3', label: 'Messaging channels', desc: 'WhatsApp, Telegram, and Messenger are the first client-response paths named on the site.' },
	{ value: '2', label: 'Public plan paths', desc: 'Free starter credits and the $20/month Professional plan are now visible before signup.' },
	{ value: '4', label: 'Question types', desc: 'Workouts, nutrition, timing, and PRs are the first repeated client questions called out.' },
];

const faqItems = [
	{
		question: 'How fast can I test the assistant?',
		answer: 'Start with a free account and starter credits. Use one common client question first, then add more routines or policies after the first replies look right.',
	},
	{
		question: 'Which messaging channels does this fit?',
		answer: 'The first customer workflows are built around WhatsApp, Telegram, and Messenger because those are where trainers and gym owners already answer quick client questions.',
	},
	{
		question: 'What does the $20/month plan change?',
		answer: 'The free tier is for evaluation. The Professional plan gives a higher monthly credit allowance for ongoing client reply volume.',
	},
	{
		question: 'Can the assistant match my coaching style?',
		answer: 'Yes. The assistant is framed around your knowledge base and a defined persona so the answer can follow your routines, tone, and boundaries.',
	},
];

export default function FitnessAI() {
	const { isSignedIn, isLoaded } = useAuth();
	const navigate = useNavigate();

	useEffect(() => {
		if (isLoaded && isSignedIn) navigate('/dashboard', { replace: true });
	}, [isLoaded, isSignedIn]);

	function trackCtaClick(ctaId, label) {
		captureEvent('cta_clicked', {
			cta_id: ctaId,
			label,
			route: '/',
		});
	}

	function trackSignupStart(source, label) {
		trackCtaClick(source, label);
		captureEvent('signup_started', {
			source,
			route: '/',
		});
	}

	return (
		<div className='page'>

			<nav className='nav' aria-label='Primary'>
				<span className='nav-logo'>FitnessAI</span>
				<div className='nav-links'>
					<a href='#how-it-works'>How It Works</a>
					<a href='#features'>Features</a>
					<a href='/pricing'>Pricing</a>
					<a href='/free'>Free</a>
					<a href='/use-cases'>Use Cases</a>
					<a href='#demo'>Live Demo</a>
					<a href='/app'>Get the App</a>
				</div>
				<div className='nav-auth'>
					{!isSignedIn ? (
						<>
							<SignInButton mode="modal"><button className='nav-btn'>Log In</button></SignInButton>
							<SignUpButton mode="modal" forceRedirectUrl={SIGN_UP_COMPLETE_REDIRECT}><button className='nav-btn nav-btn-primary' onClick={() => trackSignupStart('nav_signup', 'Sign Up')}>Sign Up</button></SignUpButton>
						</>
					) : (
						<UserButton />
					)}
				</div>
			</nav>

			<main>
			{/* HERO */}
			<section className='hero'>
				<div className='hero-overlay' />
				<div className='hero-content'>
					<h1>ANSWER CLIENT QUESTIONS BEFORE THEY PILE UP.</h1>
					<p className='hero-sub'>
						FitnessAI helps personal trainers and gym owners turn workout, nutrition, and scheduling questions into fast AI replies across WhatsApp, Telegram, and Messenger.
					</p>
					<div className='hero-actions'>
						{!isSignedIn ? (
							<SignUpButton mode="modal" forceRedirectUrl={SIGN_UP_COMPLETE_REDIRECT}>
								<button className='hero-btn' onClick={() => trackSignupStart('hero_create_free_assistant', 'Create free assistant')}>CREATE FREE ASSISTANT</button>
							</SignUpButton>
						) : (
							<a href='/dashboard' className='hero-btn' onClick={() => trackCtaClick('hero_dashboard', 'Open dashboard')}>OPEN DASHBOARD</a>
						)}
						<a href='/pricing' className='hero-btn-ghost' onClick={() => trackCtaClick('hero_pricing', 'See pricing')}>SEE PRICING</a>
						<a href='#demo' className='hero-btn-ghost' onClick={() => trackCtaClick('hero_live_demo', 'Try the live AI')}>TRY THE LIVE AI →</a>
					</div>
				</div>
				<div className='hero-scroll' aria-hidden='true'>
					<span>SCROLL</span>
					<div className='hero-scroll-line' />
				</div>
			</section>

			{/* STATS BAR */}
			<section className='proof-row' aria-label='FitnessAI proof points'>
				{proofStats.map((proof) => (
					<div className='proof-stat' key={proof.label}>
						<strong>{proof.value}</strong>
						<span>{proof.label}</span>
						<p>{proof.desc}</p>
					</div>
				))}
			</section>

			{/* HOW IT WORKS */}
			<section className='section' id='how-it-works'>
				<div className='section-inner'>
					<p className='section-tag'>THE PROCESS</p>
					<h2 className='section-heading'>Three steps to a client-answering assistant</h2>
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
					<h2 className='section-heading'>Built around the questions clients repeat</h2>
					<div className='features'>
						{features.map((f) => (
							<div className='feature-card' key={f.title}>
								<span className='feature-label'>{f.label}</span>
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
					<h2 className='section-heading'>Try the AI before you build your assistant</h2>
					<p className='demo-sub'>
						Use the live demo to check how the AI handles fitness questions and source material before you create an account.
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
						rel='noopener noreferrer'
						onClick={() => trackCtaClick('demo_external', 'Open demo in new tab')}
					>
						OPEN DEMO IN NEW TAB ↗
					</a>
				</div>
			</section>

			{/* FAQ */}
			<section className='section section-dark' id='faq'>
				<div className='section-inner faq-layout'>
					<div>
						<p className='section-tag'>FAQ</p>
						<h2 className='section-heading'>The setup questions buyers ask first</h2>
					</div>
					<div className='faq-list'>
						{faqItems.map((item) => (
							<article className='faq-item' key={item.question}>
								<h3>{item.question}</h3>
								<p>{item.answer}</p>
							</article>
						))}
					</div>
				</div>
			</section>

			{/* CTA */}
			<section className='cta-section'>
				<div className='cta-inner'>
					<p className='section-tag'>GET STARTED</p>
					<h2>Your clients already have questions.<br />Give them the first answer.</h2>
					{!isSignedIn ? (
						<SignUpButton mode="modal" forceRedirectUrl={SIGN_UP_COMPLETE_REDIRECT}>
							<button className='hero-btn' onClick={() => trackSignupStart('footer_create_account', 'Create free account')}>CREATE FREE ACCOUNT</button>
						</SignUpButton>
					) : (
						<a href='/dashboard' className='hero-btn' onClick={() => trackCtaClick('footer_dashboard', 'Go to dashboard')}>GO TO DASHBOARD</a>
					)}
				</div>
			</section>
			</main>

			{/* FOOTER */}
			<footer className='footer'>
				<div className='footer-col'>
					<span className='nav-logo'>FitnessAI</span>
					<p>© 2026 FitnessAI. All rights reserved.</p>
					<a href='/privacy' className='footer-privacy'>Privacy &amp; your data</a>
				</div>
				<div className='footer-links'>
					<div className='footer-group'>
						<h3>Explore</h3>
						<a href='/pricing'>Pricing</a>
						<a href='/free'>Free tier</a>
						<a href='/use-cases'>Use cases</a>
						<a href='#features'>Features</a>
						<a href='#faq'>FAQ</a>
						<a href='/demo'>Sample Dashboard</a>
						<a href='#demo'>Live AI Demo</a>
					</div>
					<div className='footer-group'>
						<h3>Contact</h3>
						<a href='https://github.com/PyMite6941/Fitness-AI-Agents' target='_blank' rel='noopener noreferrer'>GitHub repo</a>
						<a href='#faq'>Setup questions</a>
					</div>
					<div className='footer-group'>
						<h3>Apps</h3>
						<a href='/app'>Android tracker</a>
						<a href='/app'>iPhone (Add to Home Screen)</a>
						<a href='/app'>Desktop app</a>
					</div>
				</div>
			</footer>

		</div>
	);
}
