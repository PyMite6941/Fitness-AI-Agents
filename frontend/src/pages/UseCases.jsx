import { SignUpButton, useAuth, UserButton } from '@clerk/react';
import { captureEvent } from '../lib/analytics';
import './EvaluationPages.css';

const workflows = [
	{
		audience: 'Personal trainer',
		title: 'Answer WhatsApp workout questions between sessions',
		copy: 'Clients ask what to swap, how hard to push, or whether soreness changes the plan. FitnessAI can use your routines and coaching persona to draft fast replies from the playbook you already trust.',
		points: [
			'Workout substitutions',
			'Form and recovery reminders',
			'Nutrition timing questions',
		],
	},
	{
		audience: 'Gym owner',
		title: 'Handle Messenger setup and class questions',
		copy: 'Members ask about onboarding, class timing, gym policies, and what to do next. A trained assistant can answer common questions while keeping the gym voice consistent.',
		points: [
			'Member onboarding',
			'Class and schedule guidance',
			'Gym policy answers',
		],
	},
	{
		audience: 'Coach community',
		title: 'Support Telegram groups without repeating yourself',
		copy: 'Group chats move quickly. FitnessAI can help respond to repeated challenge rules, PR questions, nutrition reminders, and recovery guidance without rebuilding the same answer each time.',
		points: [
			'Group challenge rules',
			'PR and progress questions',
			'Standard coaching boundaries',
		],
	},
];

const setupSteps = [
	{
		title: 'Choose the channel',
		copy: 'Start with the place your clients already ask questions: WhatsApp, Telegram, or Messenger.',
	},
	{
		title: 'Add your source material',
		copy: 'Use routines, nutrition notes, onboarding guidance, and gym policies as the assistant knowledge base.',
	},
	{
		title: 'Set the persona',
		copy: 'Shape the assistant around the voice, boundaries, and coaching style clients expect from you.',
	},
];

export default function UseCases() {
	const { isSignedIn } = useAuth();

	function trackUseCaseCta(source, label) {
		captureEvent('cta_clicked', {
			cta_id: source,
			label,
			route: '/use-cases',
		});

		if (!isSignedIn && source.includes('signup')) {
			captureEvent('signup_started', {
				source,
				route: '/use-cases',
			});
		}
	}

	return (
		<div className='eval-page'>
			<nav className='eval-nav'>
				<a href='/' className='eval-logo'>FitnessAI</a>
				<div className='eval-nav-links'>
					<a href='/'>Home</a>
					<a href='/free'>Free</a>
					<a href='/pricing'>Pricing</a>
					<a href='/#demo'>Demo</a>
					<a href='/app'>Apps</a>
				</div>
				{isSignedIn ? (
					<UserButton />
				) : (
					<SignUpButton mode='modal'>
						<button className='eval-nav-cta' onClick={() => trackUseCaseCta('use_cases_nav_signup', 'Sign up')}>Sign up</button>
					</SignUpButton>
				)}
			</nav>

			<main>
				<section className='eval-hero'>
					<div>
						<p className='eval-kicker'>Use cases</p>
						<h1>Use FitnessAI when client questions repeat across messaging channels.</h1>
						<p className='eval-hero-copy'>
							Personal trainers and gym owners can turn repeated workout, nutrition, setup, and timing questions into assistant replies shaped by their knowledge base and coaching persona.
						</p>
						<div className='eval-actions'>
							{isSignedIn ? (
								<a className='eval-primary' href='/dashboard' onClick={() => trackUseCaseCta('use_cases_dashboard', 'Open dashboard')}>Open dashboard</a>
							) : (
								<SignUpButton mode='modal'>
									<button className='eval-primary' onClick={() => trackUseCaseCta('use_cases_signup_primary', 'Create free assistant')}>Create free assistant</button>
								</SignUpButton>
							)}
							<a className='eval-secondary' href='/free' onClick={() => trackUseCaseCta('use_cases_free', 'See free tier')}>See free tier</a>
							<a className='eval-secondary' href='/pricing' onClick={() => trackUseCaseCta('use_cases_pricing', 'Compare pricing')}>Compare pricing</a>
						</div>
					</div>
					<aside className='eval-hero-card'>
						<span>Best first test</span>
						<strong>One repeated question stream</strong>
						<p>
							Start where replies already cost time: WhatsApp clients, Messenger members, or Telegram groups.
						</p>
					</aside>
				</section>

				<section className='eval-band'>
					<div className='eval-inner'>
						<div className='eval-section-head'>
							<p className='eval-kicker'>Workflows</p>
							<h2>Three ways trainers and gyms can use the assistant</h2>
							<p>
								Each workflow starts with a real channel, source material, and a persona so the assistant can answer with context instead of generic fitness copy.
							</p>
						</div>
						<div className='eval-grid'>
							{workflows.map((workflow) => (
								<article className='eval-card' key={workflow.title}>
									<span>{workflow.audience}</span>
									<h3>{workflow.title}</h3>
									<p>{workflow.copy}</p>
									<ul>
										{workflow.points.map((point) => (
											<li key={point}>{point}</li>
										))}
									</ul>
								</article>
							))}
						</div>
					</div>
				</section>

				<section className='eval-band alt'>
					<div className='eval-inner'>
						<div className='eval-section-head'>
							<p className='eval-kicker'>Setup path</p>
							<h2>Turn a messy inbox into a focused first assistant</h2>
							<p>
								The strongest first use case is narrow enough to judge quickly and important enough that better replies would save real time.
							</p>
						</div>
						<div className='eval-grid'>
							{setupSteps.map((step) => (
								<article className='eval-card' key={step.title}>
									<span>Setup</span>
									<h3>{step.title}</h3>
									<p>{step.copy}</p>
								</article>
							))}
						</div>
						<div className='eval-proof'>
							<div>
								<strong>3</strong>
								<span>Channels</span>
							</div>
							<div>
								<h3>WhatsApp, Telegram, and Messenger are the first channels to plan around.</h3>
								<p>
									Use the page to pick which channel carries the highest-value repeated questions before building a wider client automation path.
								</p>
							</div>
						</div>
					</div>
				</section>

				<section className='eval-final'>
					<h2>Pick the use case that already shows up in your messages.</h2>
					<p>
						Then create a free assistant, test the first replies with starter credits, and compare the $20/month Professional plan when volume grows.
					</p>
					<div className='eval-actions'>
						{isSignedIn ? (
							<a className='eval-primary' href='/dashboard' onClick={() => trackUseCaseCta('use_cases_final_dashboard', 'Open dashboard')}>Open dashboard</a>
						) : (
							<SignUpButton mode='modal'>
								<button className='eval-primary' onClick={() => trackUseCaseCta('use_cases_final_signup', 'Create free assistant')}>Create free assistant</button>
							</SignUpButton>
						)}
						<a className='eval-secondary' href='/pricing' onClick={() => trackUseCaseCta('use_cases_final_pricing', 'See pricing')}>See pricing</a>
					</div>
				</section>
			</main>
		</div>
	);
}
