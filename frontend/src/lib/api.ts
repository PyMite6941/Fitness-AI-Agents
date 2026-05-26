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
};
