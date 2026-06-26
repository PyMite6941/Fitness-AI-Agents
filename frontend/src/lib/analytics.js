import posthog from 'posthog-js';

const POSTHOG_KEY = import.meta.env.VITE_POSTHOG_KEY || 'phc_D3wnwPRFrNCAYzopzCHAbgrhph7XqhiaMbEid2uwK6t4';
const POSTHOG_HOST = import.meta.env.VITE_POSTHOG_HOST || 'https://us.i.posthog.com';
const ACTIVATION_STORAGE_KEY = 'fitnessai:first_activation_captured';

let initialized = false;

export function initAnalytics() {
	if (initialized || typeof window === 'undefined' || !POSTHOG_KEY) return;

	posthog.init(POSTHOG_KEY, {
		api_host: POSTHOG_HOST,
		autocapture: false,
		capture_pageview: false,
		person_profiles: 'identified_only',
	});

	initialized = true;
}

export function identifyUser(userId) {
	if (!userId) return;
	initAnalytics();
	posthog.identify(userId);
}

export function captureEvent(eventName, properties = {}) {
	if (!eventName) return;
	initAnalytics();

	posthog.capture(eventName, {
		app: 'fitness_ai_agents',
		...properties,
	});
}

export function captureActivationOnce(properties = {}) {
	if (typeof window === 'undefined') return;

	try {
		if (window.localStorage.getItem(ACTIVATION_STORAGE_KEY)) return;
		window.localStorage.setItem(ACTIVATION_STORAGE_KEY, new Date().toISOString());
	} catch {
		// Local storage can be unavailable in private browser contexts.
	}

	captureEvent('activation_completed', {
		first_activation: true,
		...properties,
	});
}
