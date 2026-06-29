import { useEffect } from 'react';
import { SignUpButton, useAuth, UserButton } from '@clerk/react';
import { captureEvent } from '../lib/analytics';
import './EvaluationPages.css';

const freePageDescription = 'Create a free FitnessAI assistant with starter credits, then follow the signup path to test one client-question workflow before paying.';

const includedItems = [
	{
		title: 'A free account',
		copy: 'Create the assistant workspace and test the first client-question flow before choosing a paid plan.',
	},
	{
		title: 'Starter credits',
		copy: 'Use the starter credit allowance to test assistant replies, tone, and source material with a narrow workflow.',
	},
	{
		title: 'Evaluation tools',
		copy: 'Use the live demo, sample dashboard, pricing page, and setup guidance to judge fit before paying.',
	},
];

const freeLimits = [
	'Built for evaluation, not full ongoing client support',
	'Best for one common client question stream at a time',
	'Upgrade to Professional when reply volume needs a higher monthly allowance',
];

export default function FreeTier() {
	const { isSignedIn } = useAuth();

	useEffect(() => {
		const previousTitle = document.title;
		const existingDescription = document.querySelector('meta[name="description"]');
		const descriptionMeta = existingDescription || document.createElement('meta');
		const previousDescription = descriptionMeta.getAttribute('content');

		if (!existingDescription) {
			descriptionMeta.setAttribute('name', 'description');
			document.head.append(descriptionMeta);
		}

		document.title = 'Free FitnessAI assistant with starter credits';
		descriptionMeta.setAttribute('content', freePageDescription);

		return () => {
			document.title = previousTitle;

			if (existingDescription) {
				if (previousDescription === null) {
					descriptionMeta.removeAttribute('content');
				} else {
					descriptionMeta.setAttribute('content', previousDescription);
				}
			} else {
				descriptionMeta.remove();
			}
		};
	}, []);

	function trackFreeCta(source, label) {
		captureEvent('cta_clicked', {
			cta_id: source,
			label,
			route: '/free',
		});

		if (!isSignedIn && source.includes('signup')) {
			captureEvent('signup_started', {
				source,
				route: '/free',
			});
		}
	}

	return (
		<div className='eval-page'>
			<nav className='eval-nav'>
				<a href='/' className='eval-logo'>FitnessAI</a>
				<div className='eval-nav-links'>
					<a href='/'>Home</a>
					<a href='/pricing'>Pricing</a>
					<a href='/use-cases'>Use Cases</a>
					<a href='/#demo'>Demo</a>
					<a href='/app'>Apps</a>
				</div>
				{isSignedIn ? (
					<UserButton />
				) : (
					<SignUpButton mode='modal'>
						<button className='eval-nav-cta' onClick={() => trackFreeCta('free_nav_signup', 'Sign up')}>Sign up</button>
					</SignUpButton>
				)}
			</nav>

			<main>
				<section className='eval-hero'>
					<div>
						<p className='eval-kicker'>Free tier</p>
						<h1>Use starter credits to test one assistant workflow before you pay.</h1>
						<p className='eval-hero-copy'>
							FitnessAI is free to start for personal trainers and gym owners who want to see whether an assistant can answer repeated client questions across messaging channels.
						</p>
						<div className='eval-actions'>
							{isSignedIn ? (
								<a className='eval-primary' href='/dashboard' onClick={() => trackFreeCta('free_dashboard', 'Open dashboard')}>Open dashboard</a>
							) : (
								<SignUpButton mode='modal'>
									<button className='eval-primary' onClick={() => trackFreeCta('free_signup_primary', 'Create free assistant')}>Create free assistant</button>
								</SignUpButton>
							)}
							<a className='eval-secondary' href='/pricing' onClick={() => trackFreeCta('free_pricing', 'Compare pricing')}>Compare pricing</a>
							<a className='eval-secondary' href='/use-cases' onClick={() => trackFreeCta('free_use_cases', 'See use cases')}>See use cases</a>
						</div>
					</div>
					<aside className='eval-hero-card'>
						<span>What free means</span>
						<strong>$0 to start</strong>
						<p>
							The free tier gives you starter credits for evaluation. The $20/month Professional plan is the path for higher ongoing client reply volume.
						</p>
					</aside>
				</section>

				<section className='eval-band'>
					<div className='eval-inner'>
						<div className='eval-section-head'>
							<p className='eval-kicker'>Included</p>
							<h2>What you can test without paying</h2>
							<p>
								Start with a narrow question stream, connect the assistant to your playbook, and judge whether the replies match how you coach.
							</p>
						</div>
						<div className='eval-grid'>
							{includedItems.map((item) => (
								<article className='eval-card' key={item.title}>
									<span>Included</span>
									<h3>{item.title}</h3>
									<p>{item.copy}</p>
								</article>
							))}
						</div>
					</div>
				</section>

				<section className='eval-band alt'>
					<div className='eval-inner'>
						<div className='eval-grid two'>
							<article className='eval-checklist'>
								<span>Limits</span>
								<h3>Where the free tier should stop</h3>
								<p>
									Use the free tier to prove fit before relying on the assistant with a wider client group.
								</p>
								<ul>
									{freeLimits.map((limit) => (
										<li key={limit}>{limit}</li>
									))}
								</ul>
							</article>
							<article className='eval-checklist'>
								<span>Upgrade signal</span>
								<h3>When Professional makes sense</h3>
								<p>
									Move to the $20/month Professional plan once repeated questions are taking enough time that a higher monthly credit allowance matters.
								</p>
								<ul>
									<li>Clients ask the same workout, nutrition, timing, or setup questions every week</li>
									<li>You want the assistant to support WhatsApp, Telegram, or Messenger workflows</li>
									<li>Your knowledge base and persona are ready for more active client use</li>
								</ul>
							</article>
						</div>
						<div className='eval-proof'>
							<div>
								<strong>1</strong>
								<span>Workflow first</span>
							</div>
							<div>
								<h3>Start with the repeated question that costs you the most time.</h3>
								<p>
									A focused free test is easier to judge than a broad chatbot trial. Pick one question stream, check the reply quality, then expand after it earns trust.
								</p>
							</div>
						</div>
					</div>
				</section>

				<section className='eval-final'>
					<h2>Use the free tier to decide if the assistant belongs in your client workflow.</h2>
					<p>
						If the starter replies are useful, the pricing page shows the $20/month Professional plan for higher message volume.
					</p>
					<div className='eval-actions'>
						{isSignedIn ? (
							<a className='eval-primary' href='/dashboard' onClick={() => trackFreeCta('free_final_dashboard', 'Open dashboard')}>Open dashboard</a>
						) : (
							<SignUpButton mode='modal'>
								<button className='eval-primary' onClick={() => trackFreeCta('free_final_signup', 'Create free assistant')}>Create free assistant</button>
							</SignUpButton>
						)}
						<a className='eval-secondary' href='/pricing' onClick={() => trackFreeCta('free_final_pricing', 'See pricing')}>See pricing</a>
					</div>
				</section>
			</main>
		</div>
	);
}
