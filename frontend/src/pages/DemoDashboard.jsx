import { useNavigate } from 'react-router-dom';
import {
	Chart as ChartJS,
	CategoryScale, LinearScale, PointElement, LineElement,
	BarElement, ArcElement, Tooltip, Legend, Filler,
} from 'chart.js';
import { Line } from 'react-chartjs-2';
import './DemoDashboard.css';

ChartJS.register(CategoryScale, LinearScale, PointElement, LineElement, BarElement, ArcElement, Tooltip, Legend, Filler);

const ORANGE = '#ff3c00';
const WHITE_DIM = 'rgba(255,255,255,0.06)';

// Static sample data — no auth, no API. Mirrors the cached demo analysis
// (31 days across an Apple Watch Ultra + a Garmin Fenix 7).
const WEEKS = ['Wk 1', 'Wk 2', 'Wk 3', 'Wk 4', 'Wk 5'];
const RESTING_HR = [63, 62, 61, 60, 60];
const LOAD = [320, 355, 372, 398, 412];

const STATS = [
	{ label: 'Weekly training load', value: '412', unit: 'TRIMP', change: '+11%', up: true },
	{ label: 'Avg resting HR', value: '60', unit: 'bpm', change: '-3 bpm', up: false },
	{ label: 'Acute:Chronic', value: '1.24', unit: '', change: 'optimal', up: true },
	{ label: 'Sleep consistency', value: '82', unit: '%', change: '7.0 h avg', up: true },
];

const COMPARISON = [
	{ metric: 'Avg resting HR (bpm)', a: '61', b: '59', winner: 'b' },
	{ metric: 'Avg sleep (h)', a: '7.1', b: '6.8', winner: 'a' },
	{ metric: 'Avg HRV (ms)', a: '58', b: '61', winner: 'b' },
	{ metric: 'Workouts logged', a: '12', b: '13', winner: 'tie' },
	{ metric: 'Avg active calories', a: '2,480', b: '2,540', winner: 'b' },
];

const FINDINGS = [
	'Resting HR fell from 63 → 60 bpm over the month — a classic marker of improving aerobic efficiency.',
	'Training load rose +11% week-over-week to 412 TRIMP at an acute:chronic ratio of 1.24 — productive overload, still in the safe 0.8–1.3 band.',
	'Device agreement is strong: resting HR within 2 bpm, calories within 2.4% — no source needs to be discarded.',
];
const RECS = [
	'Hold the progression — add ≤8% load next week to keep the acute:chronic ratio under 1.3.',
	'Add one Zone 2 session (45–60 min, HR 120–140) to deepen the aerobic base.',
	'Protect sleep on hard days — sub-6 h nights cost ~4 bpm of next-day resting HR.',
];

const chartOpts = {
	responsive: true, maintainAspectRatio: false,
	plugins: { legend: { display: false }, tooltip: { backgroundColor: '#1a1a1a' } },
	scales: {
		x: { grid: { color: WHITE_DIM }, ticks: { color: 'rgba(255,255,255,0.3)' } },
		y: { grid: { color: WHITE_DIM }, ticks: { color: 'rgba(255,255,255,0.3)' } },
	},
};

export default function DemoDashboard() {
	const navigate = useNavigate();
	return (
		<div className='demo-dash'>
			<div className='demo-banner'>
				<span><strong>DEMO</strong> — sample data, no account needed. Sign up to analyze your own.</span>
				<button onClick={() => navigate('/')}>Get started free →</button>
			</div>

			<header className='demo-dash-head'>
				<h1>Your Fitness, Decoded</h1>
				<p>31 days · Apple Watch Ultra + Garmin Fenix 7 · merged into one analysis</p>
			</header>

			<div className='demo-stats'>
				{STATS.map(s => (
					<div className='demo-stat' key={s.label}>
						<span className='demo-stat-value'>{s.value}<span className='demo-stat-unit'>{s.unit}</span></span>
						<span className={`demo-stat-change ${s.up ? 'up' : 'down'}`}>{s.change}</span>
						<span className='demo-stat-label'>{s.label}</span>
					</div>
				))}
			</div>

			<div className='demo-grid'>
				<div className='demo-card'>
					<p className='demo-card-title'>RESTING HR TREND (bpm)</p>
					<div className='demo-chart'>
						<Line data={{ labels: WEEKS, datasets: [{ data: RESTING_HR, borderColor: ORANGE, backgroundColor: 'rgba(255,60,0,0.12)', borderWidth: 2, tension: 0.4, fill: true, pointRadius: 3 }] }} options={chartOpts} />
					</div>
				</div>
				<div className='demo-card'>
					<p className='demo-card-title'>WEEKLY TRAINING LOAD (TRIMP)</p>
					<div className='demo-chart'>
						<Line data={{ labels: WEEKS, datasets: [{ data: LOAD, borderColor: '#60a5fa', backgroundColor: 'rgba(96,165,250,0.12)', borderWidth: 2, tension: 0.4, fill: true, pointRadius: 3 }] }} options={chartOpts} />
					</div>
				</div>
			</div>

			<div className='demo-card'>
				<p className='demo-card-title'>AI ANALYSIS · APPLE WATCH vs GARMIN <span className='demo-score'>9/10</span></p>
				<p className='demo-summary'>
					Across 31 days your two devices tell a consistent story: aerobic fitness is improving
					(resting HR down ~3 bpm, HRV trending up) while training load sits in the optimal zone.
					The Apple Watch and Garmin agree within 2–3% on every shared metric.
				</p>
				<table className='demo-table'>
					<thead><tr><th>Metric</th><th style={{ color: ORANGE }}>Apple Watch</th><th style={{ color: '#60a5fa' }}>Garmin</th></tr></thead>
					<tbody>
						{COMPARISON.map((r, i) => (
							<tr key={i}>
								<td>{r.metric}</td>
								<td style={r.winner === 'a' ? { color: '#22c55e', fontWeight: 600 } : {}}>{r.a}</td>
								<td style={r.winner === 'b' ? { color: '#22c55e', fontWeight: 600 } : {}}>{r.b}</td>
							</tr>
						))}
					</tbody>
				</table>
				<div className='demo-cols'>
					<div>
						<p className='demo-col-title'>FINDINGS</p>
						<ol>{FINDINGS.map((f, i) => <li key={i}>{f}</li>)}</ol>
					</div>
					<div>
						<p className='demo-col-title'>RECOMMENDATIONS</p>
						<ol>{RECS.map((r, i) => <li key={i}>{r}</li>)}</ol>
					</div>
				</div>
			</div>

			<div className='demo-cta'>
				<h2>This is one analysis. Yours is one click away.</h2>
				<button onClick={() => navigate('/')}>Create your free account</button>
			</div>
		</div>
	);
}
