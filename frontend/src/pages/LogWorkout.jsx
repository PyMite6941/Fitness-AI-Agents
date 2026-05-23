import { useState, useEffect } from 'react';
import { useNavigate, useSearchParams } from 'react-router-dom';
import { useAuth } from '@clerk/react';
import { api } from '../lib/api';
import './LogWorkout.css';

const WORKOUT_TYPES = [
	'Running', 'Cycling', 'Walking', 'Hiking',
	'Weightlifting', 'Swimming', 'HIIT', 'Other',
];

const FIELDS = [
	{ key: 'duration_minutes', label: 'Duration',  unit: 'min',  required: true,  min: 0.5 },
	{ key: 'calories_burned',  label: 'Calories',  unit: 'kcal', required: false },
	{ key: 'avg_heart_rate',   label: 'Avg HR',    unit: 'bpm',  required: false },
	{ key: 'max_heart_rate',   label: 'Max HR',    unit: 'bpm',  required: false },
	{ key: 'distance_meters',  label: 'Distance',  unit: 'm',    required: false },
];

const APPS = [
	{
		id: 'strava',
		name: 'Strava',
		desc: 'Sync runs, rides, and workouts automatically.',
		color: '#FC4C02',
		status: 'available',
	},
	{
		id: 'apple_health',
		name: 'Apple Health',
		desc: 'Requires the iOS companion app.',
		color: '#fff',
		status: 'ios_only',
	},
	{
		id: 'google_fit',
		name: 'Google Fit',
		desc: 'Coming soon.',
		color: '#4285F4',
		status: 'soon',
	},
	{
		id: 'garmin',
		name: 'Garmin Connect',
		desc: 'Coming soon.',
		color: '#007CC3',
		status: 'soon',
	},
	{
		id: 'fitbit',
		name: 'Fitbit',
		desc: 'Coming soon.',
		color: '#00B0B9',
		status: 'soon',
	},
];

function localNow() {
	const d = new Date();
	d.setSeconds(0, 0);
	return d.toISOString().slice(0, 16);
}

export default function LogWorkout() {
	const { getToken, userId } = useAuth();
	const navigate = useNavigate();
	const [searchParams] = useSearchParams();

	const [mode, setMode] = useState('manual');
	const [workoutType, setWorkoutType] = useState('Running');
	const [datetime, setDatetime] = useState(localNow);
	const [fields, setFields] = useState({});
	const [notes, setNotes] = useState('');
	const [saving, setSaving] = useState(false);
	const [error, setError] = useState('');
	const [success, setSuccess] = useState('');
	const [integrations, setIntegrations] = useState({});
	const [syncing, setSyncing] = useState(null);

	useEffect(() => {
		loadIntegrations();
		// handle redirect back from Strava OAuth
		const connected = searchParams.get('connected');
		const err = searchParams.get('error');
		if (connected) {
			setSuccess(`${connected.charAt(0).toUpperCase() + connected.slice(1)} connected! You can now sync your workouts.`);
			setMode('connect');
			loadIntegrations();
		}
		if (err) setError(`Connection failed: ${err}`);
	}, []);

	async function loadIntegrations() {
		try {
			const token = await getToken();
			const res = await api.getIntegrationStatus(token);
			setIntegrations(res);
		} catch {
			// silently fail — integrations are non-critical
		}
	}

	function setField(key, val) {
		setFields(prev => ({ ...prev, [key]: val }));
	}

	async function handleSubmit(e) {
		e.preventDefault();
		setError('');
		setSaving(true);
		try {
			const token = await getToken();
			const payload = {
				workout_type: workoutType.toLowerCase(),
				timestamp: new Date(datetime).toISOString(),
				notes: notes || undefined,
			};
			for (const { key } of FIELDS) {
				if (fields[key] !== undefined && fields[key] !== '') {
					payload[key] = parseFloat(fields[key]);
				}
			}
			await api.logWorkout(token, payload);
			setSuccess('Workout saved!');
			setTimeout(() => navigate('/dashboard'), 1200);
		} catch (e) {
			setError(e.message);
		} finally {
			setSaving(false);
		}
	}

	async function connectStrava() {
		try {
			const token = await getToken();
			const { url } = await api.stravaConnectUrl(token);
			window.location.href = url;
		} catch (e) {
			setError(e.message);
		}
	}

	async function syncStrava() {
		setSyncing('strava');
		setError('');
		try {
			const token = await getToken();
			const res = await api.stravaSync(token);
			setSuccess(`Synced ${res.synced} activities from Strava.`);
		} catch (e) {
			setError(e.message);
		} finally {
			setSyncing(null);
		}
	}

	return (
		<div className='log-page'>
			<div className='log-header'>
				<button className='log-back' onClick={() => navigate('/dashboard')}>← Dashboard</button>
				<h1>LOG WORKOUT</h1>
				<div className='mode-tabs'>
					<button className={mode === 'manual' ? 'active' : ''} onClick={() => setMode('manual')}>Manual</button>
					<button className={mode === 'connect' ? 'active' : ''} onClick={() => setMode('connect')}>Connect App</button>
				</div>
			</div>

			{error && <div className='log-error'>{error}</div>}
			{success && <div className='log-success'>{success}</div>}

			{/* ── MANUAL ENTRY ── */}
			{mode === 'manual' && (
				<>
					<div className='accuracy-notice'>
						<span className='notice-icon'>⚠</span>
						<span>
							Manual entries only reflect what you type — no pace, calorie burn rate, or heart rate zone analysis is calculated from actual sensor data. For accurate insights, connect a fitness app below.
						</span>
					</div>

					<form className='log-form' onSubmit={handleSubmit}>
						<div className='log-section'>
							<label className='log-label'>WORKOUT TYPE</label>
							<div className='type-grid'>
								{WORKOUT_TYPES.map(t => (
									<button
										key={t}
										type='button'
										className={`type-chip ${workoutType === t ? 'active' : ''}`}
										onClick={() => setWorkoutType(t)}
									>
										{t}
									</button>
								))}
							</div>
						</div>

						<div className='log-section'>
							<label className='log-label'>DATE & TIME</label>
							<input
								className='log-input'
								type='datetime-local'
								value={datetime}
								onChange={e => setDatetime(e.target.value)}
								required
							/>
						</div>

						<div className='log-section'>
							<label className='log-label'>STATS</label>
							<div className='stats-grid'>
								{FIELDS.map(({ key, label, unit, required, min }) => (
									<div className='stat-field' key={key}>
										<label className='stat-label'>
											{label}{required && <span className='req'>*</span>}
										</label>
										<div className='stat-input-wrap'>
											<input
												className='log-input'
												type='number'
												step='any'
												min={min}
												placeholder='—'
												value={fields[key] ?? ''}
												onChange={e => setField(key, e.target.value)}
												required={required}
											/>
											<span className='stat-unit'>{unit}</span>
										</div>
									</div>
								))}
							</div>
						</div>

						<div className='log-section'>
							<label className='log-label'>NOTES <span className='optional'>(optional)</span></label>
							<textarea
								className='log-input log-textarea'
								placeholder='How did it feel? Any PRs?'
								value={notes}
								onChange={e => setNotes(e.target.value)}
								rows={3}
							/>
						</div>

						<button className='log-submit' type='submit' disabled={saving || !!success}>
							{saving ? 'SAVING...' : 'SAVE WORKOUT'}
						</button>
					</form>
				</>
			)}

			{/* ── CONNECT APP ── */}
			{mode === 'connect' && (
				<div className='connect-section'>
					<p className='connect-intro'>
						Link a fitness platform to automatically import workouts with accurate sensor data — GPS pace, real-time heart rate, and calorie calculations from your device.
					</p>
					<div className='app-grid'>
						{APPS.map(app => {
							const isConnected = integrations[app.id];
							return (
								<div key={app.id} className={`app-card ${app.status === 'soon' ? 'soon' : ''}`}>
									<div className='app-card-top'>
										<div className='app-dot' style={{ background: app.color }} />
										<span className='app-name'>{app.name}</span>
										{isConnected && <span className='app-badge connected'>Connected</span>}
										{app.status === 'soon' && <span className='app-badge soon'>Soon</span>}
										{app.status === 'ios_only' && <span className='app-badge ios'>iOS Only</span>}
									</div>
									<p className='app-desc'>{app.desc}</p>
									{app.status === 'available' && (
										<div className='app-actions'>
											{isConnected ? (
												<button
													className='app-btn sync'
													onClick={syncStrava}
													disabled={syncing === app.id}
												>
													{syncing === app.id ? 'Syncing...' : 'Sync Now'}
												</button>
											) : (
												<button className='app-btn connect' onClick={connectStrava}>
													Connect Strava
												</button>
											)}
										</div>
									)}
								</div>
							);
						})}
					</div>
				</div>
			)}
		</div>
	);
}
