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
};
