import { useState, useEffect } from 'react';
import { useAuth } from '@clerk/react';
import { api } from '../lib/api';
import './Dashboard.css';

export default function Dashboard() {
	const { getToken } = useAuth();
	const [stats, setStats] = useState(null);
	const [history, setHistory] = useState([]);
	const [context, setContext] = useState('');
	const [loading, setLoading] = useState(false);
	const [analyzing, setAnalyzing] = useState(false);
	const [error, setError] = useState('');
	const [selected, setSelected] = useState(null);

	useEffect(() => {
		loadData();
	}, []);

	async function loadData() {
		setLoading(true);
		try {
			const token = await getToken();
			const [me, hist] = await Promise.all([
				api.getMe(token),
				api.getHistory(token),
			]);
			setStats(me);
			setHistory(hist.analyses);
		} catch (e) {
			setError(e.message);
		} finally {
			setLoading(false);
		}
	}

	async function runAnalysis() {
		if (!context.trim()) return;
		setAnalyzing(true);
		setError('');
		try {
			const token = await getToken();
			const result = await api.analyze(token, context);
			setHistory(prev => [result, ...prev]);
			setSelected(result);
			setContext('');
		} catch (e) {
			setError(e.message);
		} finally {
			setAnalyzing(false);
		}
	}

	return (
		<div className='dash'>

			<aside className='sidebar'>
				<a href='/' className='sidebar-logo'>FitnessAI</a>
				<nav className='sidebar-nav'>
					<span className='sidebar-nav-label'>MENU</span>
					<a href='/dashboard' className='sidebar-link active'>Dashboard</a>
					<a href='/data' className='sidebar-link'>My Data</a>
				</nav>
				{stats && (
					<div className='sidebar-stats'>
						<span className='sidebar-nav-label'>YOUR STATS</span>
						<div className='sidebar-stat'><strong>{stats.workouts}</strong> Workouts</div>
						<div className='sidebar-stat'><strong>{stats.readings}</strong> Readings</div>
						<div className='sidebar-stat'><strong>{stats.analyses}</strong> Analyses</div>
					</div>
				)}
			</aside>

			<main className='dash-main'>
				<header className='dash-header'>
					<div>
						<h1>Dashboard</h1>
						<p>Ask a question about your fitness data</p>
					</div>
				</header>

				{error && <div className='dash-error'>{error}</div>}

				<div className='analyze-box'>
					<textarea
						className='analyze-input'
						placeholder='What do you want to know? e.g. "Show me my best performing workout days and identify recovery patterns."'
						value={context}
						onChange={e => setContext(e.target.value)}
						rows={3}
					/>
					<button
						className='analyze-btn'
						onClick={runAnalysis}
						disabled={!context.trim() || analyzing}
					>
						{analyzing ? 'ANALYZING...' : 'ANALYZE'}
					</button>
				</div>

				<div className='dash-body'>
					<div className='history-panel'>
						<h2>Past Analyses</h2>
						{loading && <p className='dim'>Loading...</p>}
						{!loading && history.length === 0 && (
							<p className='dim'>No analyses yet. Ask your first question above.</p>
						)}
						{history.map((a, i) => (
							<button
								key={i}
								className={`history-item ${selected === a ? 'active' : ''}`}
								onClick={() => setSelected(a)}
							>
								<span className='history-context'>{a.context}</span>
								<span className='history-date'>{new Date(a.created_at).toLocaleDateString()}</span>
							</button>
						))}
					</div>

					<div className='result-panel'>
						{selected ? (
							<>
								<p className='result-question'>"{selected.context}"</p>
								<div className='result-summary'>{selected.summary}</div>
								{selected.key_findings?.length > 0 && (
									<div className='result-section'>
										<h3>KEY FINDINGS</h3>
										<ul>{selected.key_findings.map((f, i) => <li key={i}>{f}</li>)}</ul>
									</div>
								)}
								{selected.recommendations?.length > 0 && (
									<div className='result-section'>
										<h3>RECOMMENDATIONS</h3>
										<ul>{selected.recommendations.map((r, i) => <li key={i}>{r}</li>)}</ul>
									</div>
								)}
							</>
						) : (
							<div className='result-empty'>
								<span>Select an analysis or run a new one</span>
							</div>
						)}
					</div>
				</div>
			</main>
		</div>
	);
}
