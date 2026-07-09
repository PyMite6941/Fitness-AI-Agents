import { useEffect } from 'react';
import { useAuth, UserButton } from '@clerk/react';
import { captureEvent } from '../lib/analytics';
import './EvaluationPages.css';

const PAGE_ROUTE = '/personal-trainer-client-question-assistant';

const pageDescription = 'Create a free FitnessAI assistant for repeated personal-trainer client questions about workouts, meal timing, macros, swaps, and check-ins.';

const repeatedQuestions = [
	{
		title: 'Workout swaps',
		copy: 'Clients ask what to do when a machine is taken, a movement hurts, or they miss a session.',
	},
	{
		title: 'Meal timing and macros',
		copy: 'Quick nutrition questions pile up when clients are choosing meals around workouts, work, and travel.',
	},
	{
		title: 'Check-ins and soreness',
		copy: 'Every week brings progress updates, recovery questions, PR notes, and what-do-I-do-today messages.',
	},
];

const setupSteps = [
	{
		title: 'Pick one question stream',
		copy: 'Start with the repeated topic that costs you the most time, such as workout substitutions or meal timing.',
	},
	{
		title: 'Add your playbook',
		copy: 'Use routines, nutrition guidelines, onboarding notes, policy boundaries, and tone rules as the assistant source material.',
	},
	{
		title: 'Test with starter credits',
		copy: 'Check the first answers with the free assistant path before you use the assistant with a wider client group.',
	},
];

const uploadItems = [
	'Workout routines and substitution rules',
	'Nutrition guidelines, macro ranges, and meal timing notes',
	'Onboarding notes, client policies, and scheduling boundaries',
	'Your coaching tone, escalation rules, and answers you never want automated',
];

const faqItems = [
	{
		question: 'Does this replace my coaching?',
		answer: 'No. The page is framed around repeated questions from your own playbook, with edge cases routed back to you.',
	},
	{
		question: 'Can I edit the answers?',
		answer: 'You control the source material and persona. The first free test is meant to show whether the answers match how you coach.',
	},
	{
		question: 'Can it handle nutrition questions?',
		answer: 'Use your own nutrition guidelines and boundaries as source material. Medical, unsafe, or unclear questions should come back to you.',
	},
	{
		question: 'Which client channels should I plan around?',
		answer: 'FitnessAI names WhatsApp, Telegram, and Messenger as the first client-message paths to plan around.',
	},
];

function trackPageCta(source, label) {
	captureEvent('cta_clicked', {
		cta_id: source,
		label,
		route: PAGE_ROUTE,
		page_source: PAGE_ROUTE,
	});
}

export default function SoloTrainerSearch() {
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

		document.title = 'AI client-question assistant for personal trainers';
		descriptionMeta.setAttribute('content', pageDescription);

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

	return (
		<div className='eval-page'>
			<nav className='eval-nav'>
				<a href='/' className='eval-logo'>FitnessAI</a>
				<div className='eval-nav-links'>
					<a href='/'>Home</a>
					<a href='/free'>Free</a>
					<a href='/pricing'>Pricing</a>
					<a href='/use-cases'>Use Cases</a>
					<a href='/#demo'>Demo</a>
				</div>
				{isSignedIn ? (
					<UserButton />
				) : (
					<a className='eval-nav-cta' href='/free' onClick={() => trackPageCta('solo_trainer_nav_free', 'Create free assistant')}>Create free assistant</a>
				)}
			</nav>

			<main>
				<section className='eval-hero'>
					<div>
						<p className='eval-kicker'>Personal trainers</p>
						<h1>Stop retyping the same answers after every session.</h1>
						<p className='eval-hero-copy'>
							FitnessAI helps solo personal trainers turn repeated client questions about workouts, meal timing, macros, swaps, and check-ins into a free assistant test built from their own coaching notes.
						</p>
						<div className='eval-actions'>
							{isSignedIn ? (
								<a className='eval-primary' href='/dashboard' onClick={() => trackPageCta('solo_trainer_dashboard', 'Open dashboard')}>Open dashboard</a>
							) : (
								<a className='eval-primary' href='/free' onClick={() => trackPageCta('solo_trainer_free_primary', 'Create free assistant')}>Create free assistant</a>
							)}
							<a className='eval-secondary' href='/pricing' onClick={() => trackPageCta('solo_trainer_pricing', 'See the 20 dollar plan')}>See the $20 plan</a>
						</div>
					</div>
					<aside className='eval-hero-card'>
						<span>Best first workflow</span>
						<strong>One repeated client question stream</strong>
						<p>
							Start with a narrow question set, test replies with starter credits, then expand only after the answers fit your coaching boundaries.
						</p>
					</aside>
				</section>

				<section className='eval-band'>
					<div className='eval-inner'>
						<div className='eval-section-head'>
							<p className='eval-kicker'>Daily question load</p>
							<h2>The messages that keep showing up between sessions</h2>
							<p>
								The strongest first use case is not a broad chatbot. It is the handful of client questions you answer by hand every week.
							</p>
						</div>
						<div className='eval-grid'>
							{repeatedQuestions.map((question) => (
								<article className='eval-card' key={question.title}>
									<span>Repeated question</span>
									<h3>{question.title}</h3>
									<p>{question.copy}</p>
								</article>
							))}
						</div>
					</div>
				</section>

				<section className='eval-band alt'>
					<div className='eval-inner'>
						<div className='eval-section-head'>
							<p className='eval-kicker'>Setup path</p>
							<h2>Build the first assistant from the playbook you already use</h2>
							<p>
								Keep the first version small enough to review. The assistant should answer from your routines, rules, and tone, not from generic fitness advice.
							</p>
						</div>
						<div className='eval-grid'>
							{setupSteps.map((step) => (
								<article className='eval-card' key={step.title}>
									<span>Step</span>
									<h3>{step.title}</h3>
									<p>{step.copy}</p>
								</article>
							))}
						</div>
						<div className='eval-proof'>
							<div>
								<strong>4</strong>
								<span>Good uploads</span>
							</div>
							<div>
								<h3>Give the assistant source material before you judge the answers.</h3>
								<ul className='eval-inline-list'>
									{uploadItems.map((item) => (
										<li key={item}>{item}</li>
									))}
								</ul>
							</div>
						</div>
					</div>
				</section>

				<section className='eval-band'>
					<div className='eval-inner'>
						<div className='eval-section-head'>
							<p className='eval-kicker'>Boundaries</p>
							<h2>Keep the assistant inside your coaching lane</h2>
							<p>
								The assistant should work from your playbook and return uncertain, unsafe, or personal edge cases to you. That keeps the free test focused on repeated answers, not replacing judgment.
							</p>
						</div>
						<div className='eval-grid two'>
							<article className='eval-checklist'>
								<span>Good fit</span>
								<h3>Questions with a standard answer</h3>
								<p>
									Use it for repeatable guidance like exercise swaps, check-in prompts, meal timing reminders, and policy questions.
								</p>
							</article>
							<article className='eval-checklist'>
								<span>Hand back</span>
								<h3>Questions that need you</h3>
								<p>
									Route injuries, medical questions, sensitive nutrition concerns, and anything outside your written rules back to the trainer.
								</p>
							</article>
						</div>
					</div>
				</section>

				<section className='eval-band alt'>
					<div className='eval-inner'>
						<div className='eval-section-head'>
							<p className='eval-kicker'>FAQ</p>
							<h2>What trainers usually need to know before testing</h2>
						</div>
						<div className='eval-grid two'>
							{faqItems.map((item) => (
								<article className='eval-card' key={item.question}>
									<span>Question</span>
									<h3>{item.question}</h3>
									<p>{item.answer}</p>
								</article>
							))}
						</div>
					</div>
				</section>

				<section className='eval-final'>
					<h2>Start with the client question you are tired of retyping.</h2>
					<p>
						Create a free assistant, test one workflow with starter credits, and compare the $20/month Professional plan only when message volume justifies it.
					</p>
					<div className='eval-actions'>
						{isSignedIn ? (
							<a className='eval-primary' href='/dashboard' onClick={() => trackPageCta('solo_trainer_final_dashboard', 'Open dashboard')}>Open dashboard</a>
						) : (
							<a className='eval-primary' href='/free' onClick={() => trackPageCta('solo_trainer_final_free', 'Create free assistant')}>Create free assistant</a>
						)}
						<a className='eval-secondary' href='/pricing' onClick={() => trackPageCta('solo_trainer_final_pricing', 'Compare free and Professional')}>Compare free and Professional</a>
					</div>
				</section>
			</main>
		</div>
	);
}
