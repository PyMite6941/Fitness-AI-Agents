import { useState, useEffect } from 'react';
import { useAuth, UserButton } from '@clerk/react';
import {
	Chart as ChartJS,
	CategoryScale, LinearScale, PointElement, LineElement,
	BarElement, ArcElement, Tooltip, Legend, Filler,
} from 'chart.js';
import { Line, Bar, Doughnut } from 'react-chartjs-2';
import { api } from '../lib/api';
import './Dashboard.css';

ChartJS.register(
	CategoryScale, LinearScale, PointElement, LineElement,
	BarElement, ArcElement, Tooltip, Legend, Filler,
);

const ORANGE = '#ff3c00';
const ORANGE_DIM = 'rgba(255,60,0,0.15)';
const WHITE_DIM = 'rgba(255,255,255,0.08)';

const lineOpts = {
	responsive: true,
	maintainAspectRatio: false,
	plugins: { legend: { display: false }, tooltip: { backgroundColor: '#1a1a1a', titleColor: '#fff', bodyColor: 'rgba(255,255,255,0.6)', borderColor: '#2a2a2a', borderWidth: 1 } },
	scales: {
		x: { grid: { color: WHITE_DIM }, ticks: { color: 'rgba(255,255,255,0.3)', font: { size: 11 } } },
		y: { grid: { color: WHITE_DIM }, ticks: { color: 'rgba(255,255,255,0.3)', font: { size: 11 } } },
	},
};

const doughnutOpts = {
	responsive: true,
	maintainAspectRatio: false,
	plugins: {
		legend: { position: 'right', labels: { color: 'rgba(255,255,255,0.5)', font: { size: 12 }, padding: 16 } },
		tooltip: { backgroundColor: '#1a1a1a', titleColor: '#fff', bodyColor: 'rgba(255,255,255,0.6)' },
	},
};

const TYPE_COLORS = ['#ff3c00','#ff6b3d','#ff9a7a','#ffcab7','#cc3000','#992400','#661800','#ff5722'];

const NAV_TABS = ['Overview', 'Heart Rate', 'Calories', 'Steps', 'Sleep', 'AI Analysis'];

export default function Dashboard() {
	const { getToken } = useAuth();
	const [tab, setTab] = useState('Overview');
	const [stats, setStats] = useState(null);
	const [charts, setCharts] = useState(null);
	const [history, setHistory] = useState([]);
	const [context, setContext] = useState('');
	const [analyzing, setAnalyzing] = useState(false);
	const [selected, setSelected] = useState(null);
	const [error, setError] = useState('');
	const [loading, setLoading] = useState(true);

	useEffect(() => { load(); }, []);

	async function load() {
		setLoading(true);
		try {
			const token = await getToken();
			const [me, chartData, hist] = await Promise.all([
				api.getMe(token),
				api.getCharts(token),
				api.getHistory(token),
			]);
			setStats(me);
			setCharts(chartData);
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

	function lineDataset(label, values, color = ORANGE) {
		return {
			label,
			data: values,
			borderColor: color,
			backgroundColor: ORANGE_DIM,
			borderWidth: 2,
			pointRadius: 3,
			pointBackgroundColor: color,
			tension: 0.4,
			fill: true,
		};
	}

	const noData = !charts || charts.calories.labels.length === 0;

	return (
		<div className='dash'>
			<aside className='sidebar'>
				<a href='/' className='sidebar-logo'>FitnessAI</a>
				<nav className='sidebar-nav'>
					{NAV_TABS.map(t => (
						<button key={t} className={`sidebar-link ${tab === t ? 'active' : ''}`} onClick={() => setTab(t)}>
							{t}
						</button>
					))}
				</nav>
				{stats && (
					<div className='sidebar-stats'>
						<span className='sidebar-stat-label'>YOUR STATS</span>
						<div className='sidebar-stat'><strong>{stats.workouts}</strong><span>Workouts</span></div>
						<div className='sidebar-stat'><strong>{stats.readings}</strong><span>Readings</span></div>
						<div className='sidebar-stat'><strong>{stats.analyses}</strong><span>Analyses</span></div>
					</div>
				)}
				<div className='sidebar-user'><UserButton /></div>
			</aside>

			<main className='dash-main'>
				<header className='dash-header'>
					<h1>{tab}</h1>
					{error && <div className='dash-error'>{error}</div>}
				</header>

				{loading && <div className='dash-loading'>Loading your data...</div>}

				{!loading && (
					<div className='dash-content'>

						{/* OVERVIEW */}
						{tab === 'Overview' && (
							<>
								<div className='stat-cards'>
									<StatCard label="Total Workouts"   value={stats?.workouts ?? '—'} />
									<StatCard label="Total Readings"   value={stats?.readings ?? '—'} />
									<StatCard label="Analyses Run"     value={stats?.analyses ?? '—'} />
									<StatCard label="Data Points"      value={noData ? '—' : charts.calories.labels.length} />
								</div>

								<div className='chart-grid'>
									<div className='chart-card wide'>
										<p className='chart-title'>CALORIES BURNED</p>
										{noData ? <Empty /> : (
											<div className='chart-wrap'>
												<Line
													data={{ labels: charts.calories.labels, datasets: [lineDataset('Calories', charts.calories.values)] }}
													options={lineOpts}
												/>
											</div>
										)}
									</div>

									<div className='chart-card wide'>
										<p className='chart-title'>AVG HEART RATE</p>
										{noData ? <Empty /> : (
											<div className='chart-wrap'>
												<Line
													data={{ labels: charts.heart_rate.labels, datasets: [lineDataset('BPM', charts.heart_rate.values, '#e05555')] }}
													options={lineOpts}
												/>
											</div>
										)}
									</div>

									<div className='chart-card'>
										<p className='chart-title'>WORKOUT TYPES</p>
										{!charts?.workout_types?.labels?.length ? <Empty /> : (
											<div className='chart-wrap'>
												<Doughnut
													data={{
														labels: charts.workout_types.labels,
														datasets: [{ data: charts.workout_types.values, backgroundColor: TYPE_COLORS, borderWidth: 0 }],
													}}
													options={doughnutOpts}
												/>
											</div>
										)}
									</div>

									<div className='chart-card'>
										<p className='chart-title'>RECENT WORKOUTS</p>
										<div className='recent-list'>
											{!charts?.recent_workouts?.length ? <Empty /> : charts.recent_workouts.map((w, i) => (
												<div className='recent-item' key={i}>
													<div className='recent-left'>
														<span className='recent-type'>{w.workout_type || 'Workout'}</span>
														<span className='recent-date'>{w.timestamp?.slice(0, 10)}</span>
													</div>
													<div className='recent-right'>
														<span>{w.duration_minutes ? `${Math.round(w.duration_minutes)}m` : '—'}</span>
														<span className='dim-val'>{w.calories_burned ? `${Math.round(w.calories_burned)} cal` : ''}</span>
													</div>
												</div>
											))}
										</div>
									</div>
								</div>
							</>
						)}

						{/* HEART RATE */}
						{tab === 'Heart Rate' && (
							<div className='chart-grid'>
								<div className='chart-card full'>
									<p className='chart-title'>DAILY AVG HEART RATE (BPM)</p>
									{!charts?.heart_rate?.labels?.length ? <Empty /> : (
										<div className='chart-wrap tall'>
											<Line
												data={{ labels: charts.heart_rate.labels, datasets: [lineDataset('BPM', charts.heart_rate.values, '#e05555')] }}
												options={lineOpts}
											/>
										</div>
									)}
								</div>
							</div>
						)}

						{/* CALORIES */}
						{tab === 'Calories' && (
							<div className='chart-grid'>
								<div className='chart-card full'>
									<p className='chart-title'>CALORIES BURNED PER DAY</p>
									{!charts?.calories?.labels?.length ? <Empty /> : (
										<div className='chart-wrap tall'>
											<Bar
												data={{
													labels: charts.calories.labels,
													datasets: [{
														label: 'Calories',
														data: charts.calories.values,
														backgroundColor: ORANGE,
														borderRadius: 2,
													}],
												}}
												options={{ ...lineOpts, plugins: { ...lineOpts.plugins, legend: { display: false } } }}
											/>
										</div>
									)}
								</div>
							</div>
						)}

						{/* STEPS */}
						{tab === 'Steps' && (
							<div className='chart-grid'>
								<div className='chart-card full'>
									<p className='chart-title'>DAILY STEPS</p>
									{!charts?.steps?.labels?.length ? <Empty /> : (
										<div className='chart-wrap tall'>
											<Bar
												data={{
													labels: charts.steps.labels,
													datasets: [{
														label: 'Steps',
														data: charts.steps.values,
														backgroundColor: 'rgba(100,160,255,0.7)',
														borderRadius: 2,
													}],
												}}
												options={lineOpts}
											/>
										</div>
									)}
								</div>
							</div>
						)}

						{/* SLEEP */}
						{tab === 'Sleep' && (
							<div className='chart-grid'>
								<div className='chart-card full'>
									<p className='chart-title'>SLEEP HOURS PER NIGHT</p>
									{!charts?.sleep?.labels?.length ? <Empty /> : (
										<div className='chart-wrap tall'>
											<Line
												data={{ labels: charts.sleep.labels, datasets: [lineDataset('Hours', charts.sleep.values, '#a78bfa')] }}
												options={lineOpts}
											/>
										</div>
									)}
								</div>
							</div>
						)}

						{/* AI ANALYSIS */}
						{tab === 'AI Analysis' && (
							<div className='analysis-layout'>
								<div className='analysis-left'>
									<div className='analyze-box'>
										<textarea
											className='analyze-input'
											placeholder='Ask anything about your data — "What were my best recovery weeks?" or "How does sleep affect my heart rate?"'
											value={context}
											onChange={e => setContext(e.target.value)}
											rows={4}
										/>
										<button className='analyze-btn' onClick={runAnalysis} disabled={!context.trim() || analyzing}>
											{analyzing ? 'ANALYZING...' : 'RUN ANALYSIS'}
										</button>
									</div>

									<div className='history-list'>
										<p className='chart-title'>PAST ANALYSES</p>
										{history.length === 0 && <p className='dim'>No analyses yet.</p>}
										{history.map((a, i) => (
											<button key={i} className={`history-item ${selected === a ? 'active' : ''}`} onClick={() => setSelected(a)}>
												<span className='history-context'>{a.context}</span>
												<span className='history-date'>{new Date(a.created_at).toLocaleDateString()}</span>
											</button>
										))}
									</div>
								</div>

								<div className='analysis-right'>
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
										<div className='result-empty'>Select an analysis or run a new one</div>
									)}
								</div>
							</div>
						)}
					</div>
				)}
			</main>
		</div>
	);
}

function StatCard({ label, value }) {
	return (
		<div className='stat-card'>
			<strong>{value}</strong>
			<span>{label}</span>
		</div>
	);
}

function Empty() {
	return <p className='dim' style={{ padding: '40px 0', textAlign: 'center' }}>No data yet. Sync your watch to get started.</p>;
}
