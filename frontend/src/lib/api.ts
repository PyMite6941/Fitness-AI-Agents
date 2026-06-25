const API_URL = import.meta.env.VITE_API_URL || 'http://localhost:8000';

async function request(path: string, token: string, options: RequestInit = {}) {
	const res = await fetch(`${API_URL}${path}`, {
		...options,
		headers: {
			'Content-Type': 'application/json',
			Authorization: `Bearer ${token}`,
			...options.headers,
		},
	});
	if (!res.ok) {
		const err = await res.json().catch(() => ({ detail: 'Request failed' }));
		throw new Error(err.detail || 'Request failed');
	}
	return res.json();
}

export const api = {
	getMe: (token: string) =>
		request('/user/me', token),

	getSources: (token: string) =>
		request('/user/sources', token),

	getSummary: (token: string) =>
		request('/user/summary', token),

	getHistory: (token: string, limit = 20) =>
		request(`/user/history?limit=${limit}`, token),

	getData: (token: string, limit = 100) =>
		request(`/user/data?limit=${limit}`, token),

	analyze: (token: string, context: string, dateFrom?: string, dateTo?: string) =>
		request('/analyze/', token, {
			method: 'POST',
			body: JSON.stringify({ context, date_from: dateFrom, date_to: dateTo }),
		}),

	getCharts: (token: string) =>
		request('/charts/', token),

	syncWatch: (token: string, payload: object) =>
		request('/watch/sync', token, {
			method: 'POST',
			body: JSON.stringify(payload),
		}),

	saveRoute: (token: string, payload: object) =>
		request('/routes/', token, {
			method: 'POST',
			body: JSON.stringify(payload),
		}),

	getRoutes: (token: string, limit = 20) =>
		request(`/routes/?limit=${limit}`, token),

	getRoute: (token: string, id: string) =>
		request(`/routes/${id}`, token),

	logWorkout: (token: string, workout: object) =>
		request('/watch/sync', token, {
			method: 'POST',
			body: JSON.stringify({ workouts: [workout] }),
		}),

	getIntegrationStatus: (token: string) =>
		request('/integrations/status', token),

	stravaConnectUrl: (token: string) =>
		request('/integrations/strava/connect', token),
	stravaSync: (token: string) =>
		request('/integrations/strava/sync', token, { method: 'POST' }),

	fitbitConnectUrl: (token: string) =>
		request('/integrations/fitbit/connect', token),
	fitbitSync: (token: string) =>
		request('/integrations/fitbit/sync', token, { method: 'POST' }),

	nikeImport:    (token: string, file: File) => _fileUpload(token, '/integrations/nike/import',    file),
	garminImport:  (token: string, file: File) => _fileUpload(token, '/integrations/garmin/import',  file),
	appleImport:   (token: string, file: File) => _fileUpload(token, '/integrations/apple/import',   file),
	googleFitImport: (token: string, file: File) => _fileUpload(token, '/integrations/google/import', file),

	// Universal .fit/.tcx/.gpx import — `source` tags the data (coros, wahoo, polar, …).
	fileImport: (token: string, file: File, source: string) =>
		_fileUpload(token, '/integrations/file/import', file, { source }),

	// AI Coach
	getPlan: (token: string) => request('/coach/plan', token),
	createPlan: (token: string, goal: string, weeks: number) =>
		request('/coach/plan', token, { method: 'POST', body: JSON.stringify({ goal, weeks }) }),
	adaptPlan: (token: string) => request('/coach/adapt', token, { method: 'POST' }),
	endPlan: (token: string) => request('/coach/plan', token, { method: 'DELETE' }),

	// Insights (deterministic)
	getReadiness: (token: string) => request('/insights/readiness', token),
	getAlerts: (token: string) => request('/insights/alerts', token),

	// Chat with your data
	chat: (token: string, messages: object[]) =>
		request('/chat/', token, { method: 'POST', body: JSON.stringify({ messages }) }),

	deleteMyData: (token: string) =>
		request('/user/data', token, { method: 'DELETE' }),

	demoSeed: (token: string) =>
		request('/demo/seed', token, { method: 'POST' }),

	demoReset: (token: string) =>
		request('/demo/reset', token, { method: 'POST' }),
};

function _fileUpload(token: string, path: string, file: File, extra?: Record<string, string>) {
	const form = new FormData();
	form.append('file', file);
	if (extra) for (const [k, v] of Object.entries(extra)) form.append(k, v);
	return fetch(`${API_URL}${path}`, {
		method: 'POST',
		headers: { Authorization: `Bearer ${token}` },
		body: form,
	}).then(async res => {
		if (!res.ok) {
			const err = await res.json().catch(() => ({ detail: 'Request failed' }));
			throw new Error(err.detail || 'Request failed');
		}
		return res.json();
	});
}
