import { useState, useEffect, useCallback } from 'react';
import { useNavigate } from 'react-router-dom';
import { useAuth, UserButton } from '@clerk/react';
import { api } from '../lib/api';
import './Coach.css';

const BAND = { green: '#22c55e', amber: '#f59e0b', red: '#ef4444' };
const ALERT_COLOR = { ok: '#22c55e', info: '#60a5fa', warn: '#f59e0b', danger: '#ef4444' };

export default function Coach() {
	const { getToken } = useAuth();
	const navigate = useNavigate();

	const [readiness, setReadiness] = useState(null);
	const [alerts, setAlerts] = useState([]);
	const [plan, setPlan] = useState(null);
	const [loading, setLoading] = useState(true);
	const [goal, setGoal] = useState('');
	const [weeks, setWeeks] = useState(8);
	const [busy, setBusy] = useState('');
	const [error, setError] = useState('');
	const [usage, setUsage] = useState(null);

	const load = useCallback(async () => {
		setLoading(true);
		try {
			const token = await getToken();
			const [r, a, p] = await Promise.all([
				api.getReadiness(token).catch(() => null),
				api.getAlerts(token).catch(() => ({ alerts: [] })),
				api.getPlan(token).catch(() => ({ plan: null })),
			]);
			setReadiness(r);
			setAlerts(a?.alerts || []);
			setPlan(p?.plan ? p : null);
		} catch (e) { setError(e.message); }
		finally { setLoading(false); }
	}, [getToken]);

	useEffect(() => {
		let active = true;
		queueMicrotask(() => {
			if (active) load();
		});
		return () => { active = false; };
	}, [load]);

	async function generate() {
		if (!goal.trim()) { setError('Tell the coach your goal first.'); return; }
		setBusy('generate'); setError('');
		try {
			const token = await getToken();
			const p = await api.createPlan(token, goal.trim(), Number(weeks));
			setPlan(p); setGoal(''); if (p.usage) setUsage(p.usage);
		} catch (e) { setError(e.message || 'Could not generate a plan.'); }
		finally { setBusy(''); }
	}

	async function adapt() {
		setBusy('adapt'); setError('');
		try { const r = await api.adaptPlan(await getToken()); setPlan(r); if (r.usage) setUsage(r.usage); }
		catch (e) { setError(e.message); }
		finally { setBusy(''); }
	}

	async function endPlan() {
		setBusy('end');
		try { await api.endPlan(await getToken()); setPlan(null); }
		catch (e) { setError(e.message); }
		finally { setBusy(''); }
	}

	const p = plan?.plan;
	const curWeek = p?.current_week || 1;
	const week = p?.weeks?.find(w => w.week === curWeek) || p?.weeks?.[0];

	return (
		<div className='coach-page'>
			<header className='coach-nav'>
				<button className='coach-back' onClick={() => navigate('/dashboard')}>← Dashboard</button>
				<span className='coach-logo'>AI Coach</span>
				<div style={{ display: 'flex', gap: 12, alignItems: 'center' }}>
					<button className='coach-link' onClick={() => navigate('/chat')}>💬 Chat</button>
					<UserButton />
				</div>
			</header>

			{error && <div className='coach-error'>{error}</div>}
			{usage?.used != null && (
				<div className='coach-usage'>AI usage today: {usage.used}/{usage.limit}</div>
			)}

			{/* Readiness + Alerts */}
			<div className='coach-top'>
				<div className='coach-card readiness'>
					<p className='coach-card-title'>TODAY'S READINESS</p>
					{loading ? <div className='coach-skel' /> : readiness?.available ? (
						<>
							<div className='ready-score' style={{ color: BAND[readiness.band] }}>
								{readiness.score}<span>/100</span>
							</div>
							<div className='ready-bar'><div style={{ width: `${readiness.score}%`, background: BAND[readiness.band] }} /></div>
							<p className='ready-advice'>{readiness.advice}</p>
						</>
					) : (
						<p className='coach-muted'>{readiness?.reason || 'Not enough recent data yet — sync or log a few days of HR / HRV / sleep.'}</p>
					)}
				</div>

				<div className='coach-card alerts'>
					<p className='coach-card-title'>HEALTH WATCHDOG</p>
					{loading ? <div className='coach-skel' /> : (
						<ul className='alert-list'>
							{alerts.map((al, i) => (
								<li key={i}>
									<span className='alert-dot' style={{ background: ALERT_COLOR[al.level] || '#888' }} />
									<div><strong>{al.title}</strong><span>{al.detail}</span></div>
								</li>
							))}
						</ul>
					)}
				</div>
			</div>

			{/* Plan */}
			{!plan ? (
				<div className='coach-card plan-setup'>
					<p className='coach-card-title'>SET A GOAL</p>
					<h2>What do you want to achieve?</h2>
					<input className='goal-input' value={goal} onChange={e => setGoal(e.target.value)}
						placeholder='e.g. Run a sub-50 10k · Build a base · Lose weight · First half-marathon' />
					<div className='goal-row'>
						<label>Over
							<select value={weeks} onChange={e => setWeeks(e.target.value)}>
								{[4, 6, 8, 10, 12, 16].map(w => <option key={w} value={w}>{w} weeks</option>)}
							</select>
						</label>
						<button className='coach-btn' onClick={generate} disabled={busy === 'generate'}>
							{busy === 'generate' ? 'Building your plan…' : 'Generate plan'}
						</button>
					</div>
					<p className='coach-muted'>The AI builds a week-by-week plan personalized to your recent training, and adapts it as you go.</p>
				</div>
			) : (
				<div className='coach-card'>
					<div className='plan-head'>
						<div>
							<p className='coach-card-title'>YOUR PLAN</p>
							<h2>{p.title || plan.goal}</h2>
							<p className='coach-muted'>{plan.goal} · {plan.weeks} weeks · week {curWeek} of {plan.weeks}
								{p.week_planned ? ` · ${p.week_done}/${p.week_planned} done this week` : ''}</p>
						</div>
						<div className='plan-actions'>
							<button className='coach-btn ghost' onClick={adapt} disabled={busy === 'adapt'}>{busy === 'adapt' ? 'Adapting…' : '↻ Adapt'}</button>
							<button className='coach-btn ghost danger' onClick={endPlan} disabled={busy === 'end'}>End</button>
						</div>
					</div>

					{week && (
						<>
							<p className='week-focus'>Week {week.week}: <strong>{week.focus}</strong></p>
							<div className='day-grid'>
								{week.days?.map((d, i) => {
									const rest = /rest/i.test(d.type || '');
									return (
										<div key={i} className={`day-card${rest ? ' rest' : ''}${d.done ? ' done' : d.past && !rest ? ' missed' : ''}`}>
											<span className='day-name'>{(d.day || '').slice(0, 3)}</span>
											<span className='day-title'>{d.title}</span>
											{!rest && <span className='day-detail'>{d.detail}</span>}
											{!rest && (d.target_distance_km || d.target_minutes) && (
												<span className='day-target'>{d.target_distance_km ? `${d.target_distance_km} km` : ''}{d.target_distance_km && d.target_minutes ? ' · ' : ''}{d.target_minutes ? `${d.target_minutes} min` : ''}</span>
											)}
											<span className='day-status'>{rest ? '😴' : d.done ? '✓ done' : d.past ? '○ missed' : 'upcoming'}</span>
										</div>
									);
								})}
							</div>
						</>
					)}
				</div>
			)}
		</div>
	);
}
