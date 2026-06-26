import { SignUpButton, useAuth, UserButton } from '@clerk/react';
import { useNavigate } from 'react-router-dom';
import { captureEvent } from '../lib/analytics';
import './Pricing.css';

const tiers = [
	{
		name: 'Free credits',
		price: '$0',
		cadence: 'starter tier',
		limit: 'Included starter credits for testing one assistant workflow before you rely on it with clients.',
		description: 'Best for seeing whether FitnessAI fits the questions your clients already send.',
		features: [
			'Create a free account',
			'Try assistant replies with starter credits',
			'Use the live demo and sample dashboard',
			'Upgrade when client question volume grows',
		],
		cta: 'Create free assistant',
		source: 'pricing_free',
		highlight: false,
	},
	{
		name: 'Professional',
		price: '$20',
		cadence: 'per month',
		limit: 'Higher monthly credit allowance for active client replies across your core messaging channels.',
		description: 'Best for trainers and gym owners who want the assistant working through repeat client questions.',
		features: [
			'Use AI replies for ongoing client support',
			'Cover workout, nutrition, timing, and setup questions',
			'Plan around WhatsApp, Telegram, and Messenger workflows',
			'Keep pricing predictable while message volume grows',
		],
		cta: 'Start professional',
		source: 'pricing_professional',
		highlight: true,
	},
];

export default function Pricing() {
	const { isSignedIn } = useAuth();
	const navigate = useNavigate();

	function trackPricingCta(source, label) {
		captureEvent('cta_clicked', {
			cta_id: source,
			label,
			route: '/pricing',
		});

		if (!isSignedIn) {
			captureEvent('signup_started', {
				source,
				route: '/pricing',
			});
		}
	}

	return (
		<div className='pricing-page'>
			<nav className='pricing-nav'>
				<a href='/' className='pricing-logo'>FitnessAI</a>
				<div className='pricing-nav-links'>
					<a href='/'>Home</a>
					<a href='/#demo'>Demo</a>
					<a href='/app'>Apps</a>
				</div>
				{isSignedIn ? (
					<UserButton />
				) : (
					<SignUpButton mode='modal'>
						<button className='pricing-nav-cta' onClick={() => trackPricingCta('pricing_nav_signup', 'Sign up')}>Sign up</button>
					</SignUpButton>
				)}
			</nav>

			<main>
				<section className='pricing-hero'>
					<p className='pricing-kicker'>Pricing for trainer and gym assistants</p>
					<h1>Start with free credits, then move to $20/month when the assistant is part of your client workflow.</h1>
					<p>
						FitnessAI gives personal trainers and gym owners a clear way to test client-response automation before paying for ongoing message volume.
					</p>
				</section>

				<section className='pricing-tiers' aria-label='FitnessAI pricing tiers'>
					{tiers.map((tier) => (
						<article className={`pricing-card ${tier.highlight ? 'featured' : ''}`} key={tier.name}>
							{tier.highlight && <span className='pricing-badge'>For active client support</span>}
							<div className='pricing-card-head'>
								<h2>{tier.name}</h2>
								<p>{tier.description}</p>
							</div>
							<div className='pricing-price-row'>
								<span className='pricing-price'>{tier.price}</span>
								<span className='pricing-cadence'>{tier.cadence}</span>
							</div>
							<div className='pricing-limit'>
								<span>Limit</span>
								<p>{tier.limit}</p>
							</div>
							<ul>
								{tier.features.map((feature) => (
									<li key={feature}>{feature}</li>
								))}
							</ul>
							{isSignedIn ? (
								<button className='pricing-card-cta' onClick={() => navigate('/dashboard')}>Open dashboard</button>
							) : (
								<SignUpButton mode='modal'>
									<button className='pricing-card-cta' onClick={() => trackPricingCta(tier.source, tier.cta)}>{tier.cta}</button>
								</SignUpButton>
							)}
						</article>
					))}
				</section>

				<section className='pricing-note'>
					<div>
						<h2>What counts as usage?</h2>
						<p>
							Credits cover assistant work such as drafting replies, answering client questions, and running AI analysis. The free tier is for evaluation. The professional plan is for ongoing client support volume.
						</p>
					</div>
					<a href='/#demo' onClick={() => captureEvent('cta_clicked', { cta_id: 'pricing_demo', label: 'Try the demo', route: '/pricing' })}>Try the demo</a>
				</section>
			</main>
		</div>
	);
}
