import { useState, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import { useAuth, UserButton } from '@clerk/react';
import {
	Chart as ChartJS,
	CategoryScale, LinearScale, PointElement, LineElement,
	BarElement, ArcElement, Tooltip, Legend, Filler,
} from 'chart.js';
import { Line, Bar, Doughnut } from 'react-chartjs-2';
import { api } from '../lib/api';
import './Dashboard.css';

ChartJS.register(CategoryScale, LinearScale, PointElement, LineElement, BarElement, ArcElement, Tooltip, Legend, Filler);

const ORANGE       = '#ff3c00';
const ORANGE_DIM   = 'rgba(255,60,0,0.12)';
const WHITE_DIM    = 'rgba(255,255,255,0.06)';
const TYPE_COLORS  = ['#ff3c00','#ff6b3d','#ff9a7a','#ffcab7','#cc3000','#992400'];

const chartDefaults = {
	responsive: true, maintainAspectRatio: false,
	plugins: {
		legend: { display: false },
		tooltip: { backgroundColor: '#1a1a1a', titleColor: '#fff', bodyColor: 'rgba(255,255,255,0.6)', borderColor: '#2a2a2a', borderWidth: 1, padding: 10 },
	},
	scales: {
		x: { grid: { color: WHITE_DIM }, ticks: { color: 'rgba(255,255,255,0.25)', font: { size: 11 } }, border: { color: 'transparent' } },
		y: { grid: { color: WHITE_DIM }, ticks: { color: 'rgba(255,255,255,0.25)', font: { size: 11 } }, border: { color: 'transparent' } },
	},
};

const doughnutOpts = {
	responsive: true, maintainAspectRatio: false,
	plugins: {
		legend: { position: 'right', labels: { color: 'rgba(255,255,255,0.45)', font: { size: 12 }, padding: 16, boxWidth: 10 } },
		tooltip: { backgroundColor: '#1a1a1a', titleColor: '#fff', bodyColor: 'rgba(255,255,255,0.6)' },
	},
	cutout: '68%',
};

const NAV = [
	{ id: 'Overview',    icon: '◈' },
	{ id: 'Heart Rate',  icon: '♥' },
	{ id: 'Calories',    icon: '⚡' },
	{ id: 'Steps',       icon: '◎' },
	{ id: 'Sleep',       icon: '◑' },
	{ id: 'AI Analysis', icon: '✦' },
];

function trend(curr, prev) {
	if (!prev || prev === 0) return null;
	const pct = Math.round(((curr - prev) / prev) * 100);
	return { pct: Math.abs(pct), up: pct >= 0, zero: pct === 0 };
}

function TrendBadge({ curr, prev, invert = false }) {
	const t = trend(curr, prev);
	if (!t || t.zero) return null;
	const positive = invert ? !t.up : t.up;
	return (
		<span className={`trend-badge ${positive ? 'up' : 'down'}`}>
			{t.up ? '↑' : '↓'} {t.pct}%
		</span>
	);
}

function lineDs(label, values, color = ORANGE) {
	return {
		label, data: values,
		borderColor: color, backgroundColor: `${color}18`,
		borderWidth: 2, pointRadius: 2, pointBackgroundColor: color,
		tension: 0.4, fill: true,
	};
}

export default function Dashboard() {
	const { getToken } = useAuth();
	const navigate = useNavigate();

	const [tab, setTab]         = useState('Overview');
	const [stats, setStats]     = useState(null);
	const [charts, setCharts]   = useState(null);
	const [summary, setSummary] = useState(null);
	const [history, setHistory] = useState([]);
	const [context, setContext] = useState('');
	const [analyzing, setAnalyzing] = useState(false);
	const [selected, setSelected]   = useState(null);
	const [error, setError]     = useState('');
	const [loading, setLoading] = useState(true);

	useEffect(() => { load(); }, []);

	async function load() {
		setLoading(true);
		try {
			const token = await getToken();
			const [me, chartData, hist, sum] = await Promise.all([
				api.getMe(token),
				api.getCharts(token),
				api.getHistory(token),
				api.getSummary(token),
			]);
			setStats(me);
			setCharts(chartData);
			setHistory(hist.analyses);
			setSummary(sum);
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

	const noChart = !charts || charts.calories.labels.length === 0;
	const tw = summary?.this_week;
	const lw = summary?.last_week;
	const rec = summary?.records;

	return (
		<div className='dash'>
			{/* ── Sidebar ── */}
			<aside className='sidebar'>
				<a href='/' className='sidebar-logo'>FitnessAI</a>

				{summary && (
					<div className='streak-badge'>
						<span className='streak-flame'>🔥</span>
						<div>
							<strong>{summary.streak}</strong>
							<span>day streak</span>
						</div>
					</div>
				)}

				<nav className='sidebar-nav'>
					{NAV.map(({ id, icon }) => (
						<button key={id} className={`sidebar-link ${tab === id ? 'active' : ''}`} onClick={() => setTab(id)}>
							<span className='nav-icon'>{icon}</span>{id}
						</button>
					))}
					<div className='sidebar-divider' />
					<button className='sidebar-link' onClick={() => navigate('/routes')}>
						<span className='nav-icon'>⊕</span>Routes
					</button>
					<button className='sidebar-link log-workout-btn' onClick={() => navigate('/log')}>
						+ Log Workout
					</button>
				</nav>

				<div className='sidebar-user'><UserButton /></div>
			</aside>

			{/* ── Main ── */}
			<main className='dash-main'>
				<header className='dash-header'>
					<div>
						<h1>{tab}</h1>
						{tab === 'Overview' && tw && (
							<p className='dash-subtitle'>This week vs last week</p>
						)}
					</div>
					{error && <div className='dash-error'>{error}</div>}
				</header>

				{loading && (
					<div className='dash-loading'>
						<div className='skeleton-row'>
							{[1,2,3].map(i => <div key={i} className='skeleton-card' />)}
						</div>
						<div className='skeleton-chart' />
					</div>
				)}

				{!loading && (
					<div className='dash-content'>

						{/* ── OVERVIEW ── */}
						{tab === 'Overview' && (
							<>
								{/* Weekly metric cards */}
								{tw && (
									<div className='week-row'>
										<WeekCard
											label='WORKOUTS'    accent='#ff3c00'
											value={tw.workouts} unit='sessions'
											trend={<TrendBadge curr={tw.workouts} prev={lw?.workouts} />}
										/>
										<WeekCard
											label='DISTANCE'    accent='#22c55e'
											value={tw.distance_km} unit='km'
											trend={<TrendBadge curr={tw.distance_km} prev={lw?.distance_km} />}
										/>
										<WeekCard
											label='CALORIES'    accent='#f59e0b'
											value={tw.calories} unit='kcal'
											trend={<TrendBadge curr={tw.calories} prev={lw?.calories} />}
										/>
										<WeekCard
											label='AVG HR'      accent='#e05555'
											value={tw.avg_hr || '—'} unit={tw.avg_hr ? 'bpm' : ''}
											trend={tw.avg_hr ? <TrendBadge curr={tw.avg_hr} prev={lw?.avg_hr} invert /> : null}
										/>
									</div>
								)}

								{/* Charts */}
								<div className='chart-grid'>
									<div className='chart-card wide'>
										<p className='chart-title'>CALORIES BURNED</p>
										{noChart ? <Empty /> : (
											<div className='chart-wrap'>
												<Line data={{ labels: charts.calories.labels, datasets: [lineDs('Cal', charts.calories.values)] }} options={chartDefaults} />
											</div>
										)}
									</div>
									<div className='chart-card wide'>
										<p className='chart-title'>HEART RATE</p>
										{noChart ? <Empty /> : (
											<div className='chart-wrap'>
												<Line data={{ labels: charts.heart_rate.labels, datasets: [lineDs('BPM', charts.heart_rate.values, '#e05555')] }} options={chartDefaults} />
											</div>
										)}
									</div>
									<div className='chart-card'>
										<p className='chart-title'>WORKOUT TYPES</p>
										{!charts?.workout_types?.labels?.length ? <Empty /> : (
											<div className='chart-wrap'>
												<Doughnut
													data={{ labels: charts.workout_types.labels, datasets: [{ data: charts.workout_types.values, backgroundColor: TYPE_COLORS, borderWidth: 0 }] }}
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
													<div>
														<span className='recent-type'>{w.workout_type || 'Workout'}</span>
														<span className='recent-date'>{w.timestamp?.slice(0, 10)}</span>
													</div>
													<div className='recent-right'>
														<span>{w.duration_minutes ? `${Math.round(w.duration_minutes)}m` : '—'}</span>
														{w.calories_burned ? <span className='dim-val'>{Math.round(w.calories_burned)} kcal</span> : null}
													</div>
												</div>
											))}
										</div>
									</div>
								</div>

								{/* Personal Records */}
								{rec && Object.values(rec).some(Boolean) && (
									<div className='records-section'>
										<p className='section-label'>PERSONAL RECORDS</p>
										<div className='records-grid'>
											{rec.longest_km   && <RecordCard value={rec.longest_km}  unit='km'    label='Longest Run' />}
											{rec.best_pace    && <RecordCard value={rec.best_pace}   unit='/km'   label='Best Pace' />}
											{rec.max_steps    && <RecordCard value={rec.max_steps.toLocaleString()} unit='steps' label='Most Steps (day)' />}
											{rec.max_calories && <RecordCard value={rec.max_calories} unit='kcal' label='Most Calories' />}
											{rec.max_hr       && <RecordCard value={rec.max_hr}       unit='bpm'  label='Peak Heart Rate' />}
										</div>
									</div>
								)}
							</>
						)}

						{/* ── HEART RATE ── */}
						{tab === 'Heart Rate' && (
							<div className='chart-grid'>
								<div className='chart-card full'>
									<p className='chart-title'>DAILY AVG HEART RATE (BPM)</p>
									{!charts?.heart_rate?.labels?.length ? <Empty /> : (
										<div className='chart-wrap tall'>
											<Line data={{ labels: charts.heart_rate.labels, datasets: [lineDs('BPM', charts.heart_rate.values, '#e05555')] }} options={chartDefaults} />
										</div>
									)}
								</div>
							</div>
						)}

						{/* ── CALORIES ── */}
						{tab === 'Calories' && (
							<div className='chart-grid'>
								<div className='chart-card full'>
									<p className='chart-title'>CALORIES BURNED PER DAY</p>
									{!charts?.calories?.labels?.length ? <Empty /> : (
										<div className='chart-wrap tall'>
											<Bar data={{ labels: charts.calories.labels, datasets: [{ label: 'Cal', data: charts.calories.values, backgroundColor: ORANGE, borderRadius: 3 }] }} options={chartDefaults} />
										</div>
									)}
								</div>
							</div>
						)}

						{/* ── STEPS ── */}
						{tab === 'Steps' && (
							<div className='chart-grid'>
								<div className='chart-card full'>
									<p className='chart-title'>DAILY STEPS</p>
									{!charts?.steps?.labels?.length ? <Empty /> : (
										<div className='chart-wrap tall'>
											<Bar data={{ labels: charts.steps.labels, datasets: [{ label: 'Steps', data: charts.steps.values, backgroundColor: 'rgba(100,160,255,0.7)', borderRadius: 3 }] }} options={chartDefaults} />
										</div>
									)}
								</div>
							</div>
						)}

						{/* ── SLEEP ── */}
						{tab === 'Sleep' && (
							<div className='chart-grid'>
								<div className='chart-card full'>
									<p className='chart-title'>SLEEP HOURS PER NIGHT</p>
									{!charts?.sleep?.labels?.length ? <Empty /> : (
										<div className='chart-wrap tall'>
											<Line data={{ labels: charts.sleep.labels, datasets: [lineDs('Hrs', charts.sleep.values, '#a78bfa')] }} options={chartDefaults} />
										</div>
									)}
								</div>
							</div>
						)}

						{/* ── AI ANALYSIS ── */}
						{tab === 'AI Analysis' && (
							<div className='analysis-layout'>
								<div className='analysis-left'>
									<div className='analyze-box'>
										<textarea
											className='analyze-input'
											placeholder='Ask anything — "What were my best recovery weeks?" or "How does sleep affect my heart rate?"'
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

function WeekCard({ label, value, unit, trend, accent }) {
	return (
		<div className='week-card' style={{ '--accent': accent }}>
			<div className='week-card-top'>
				<span className='week-label'>{label}</span>
				{trend}
			</div>
			<div className='week-value'>
				{value ?? '—'}
				{unit && <span className='week-unit'>{unit}</span>}
			</div>
		</div>
	);
}

function RecordCard({ value, unit, label }) {
	return (
		<div className='record-card'>
			<span className='record-badge'>PR</span>
			<div className='record-value'>{value}<span className='record-unit'>{unit}</span></div>
			<div className='record-label'>{label}</div>
		</div>
	);
}

function Empty() {
	return <p className='dim empty-msg'>No data yet — sync your watch or log a workout to get started.</p>;
}
