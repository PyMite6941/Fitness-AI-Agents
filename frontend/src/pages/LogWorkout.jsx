import { useState, useEffect, useCallback } from 'react';
import { useNavigate, useSearchParams } from 'react-router-dom';
import { useAuth } from '@clerk/react';
import { MapContainer, TileLayer, Polyline, Marker, useMapEvents } from 'react-leaflet';
import L from 'leaflet';
import 'leaflet/dist/leaflet.css';
import { api } from '../lib/api';
import './LogWorkout.css';

// Fix leaflet marker icons
delete L.Icon.Default.prototype._getIconUrl;
L.Icon.Default.mergeOptions({
	iconRetinaUrl: 'https://unpkg.com/leaflet@1.9.4/dist/images/marker-icon-2x.png',
	iconUrl:       'https://unpkg.com/leaflet@1.9.4/dist/images/marker-icon.png',
	shadowUrl:     'https://unpkg.com/leaflet@1.9.4/dist/images/marker-shadow.png',
});

const WORKOUT_TYPES = [
	'Running', 'Cycling', 'Walking', 'Hiking',
	'Weightlifting', 'Swimming', 'HIIT', 'Other',
];

const STAT_FIELDS = [
	{ key: 'duration_minutes',   label: 'Duration',  unit: 'min',  required: true, min: 0.5 },
	{ key: 'calories_burned',    label: 'Calories',  unit: 'kcal', required: false },
	{ key: 'avg_heart_rate',     label: 'Avg HR',    unit: 'bpm',  required: false },
	{ key: 'max_heart_rate',     label: 'Max HR',    unit: 'bpm',  required: false },
	{ key: 'ending_heart_rate',  label: 'End HR',    unit: 'bpm',  required: false },
	{ key: 'distance_meters',    label: 'Distance',  unit: 'm',    required: false },
];

const APPS = [
	{ id: 'strava',       name: 'Strava',        desc: 'Sync runs, rides, and GPS routes automatically.',    color: '#FC4C02', status: 'available' },
	{ id: 'apple_health', name: 'Apple Health',  desc: 'Requires the iOS companion app.',                   color: '#fff',    status: 'ios_only' },
	{ id: 'google_fit',   name: 'Google Fit',    desc: 'Coming soon.',                                      color: '#4285F4', status: 'soon' },
	{ id: 'garmin',       name: 'Garmin Connect',desc: 'Coming soon.',                                      color: '#007CC3', status: 'soon' },
	{ id: 'fitbit',       name: 'Fitbit',        desc: 'Coming soon.',                                      color: '#00B0B9', status: 'soon' },
];

function localNow() {
	const d = new Date();
	d.setSeconds(0, 0);
	return d.toISOString().slice(0, 16);
}

function haversine(lat1, lng1, lat2, lng2) {
	const R = 6371000, p = Math.PI / 180;
	const a = Math.sin((lat2 - lat1) * p / 2) ** 2
		+ Math.cos(lat1 * p) * Math.cos(lat2 * p) * Math.sin((lng2 - lng1) * p / 2) ** 2;
	return 2 * R * Math.asin(Math.sqrt(Math.min(1, Math.max(0, a))));
}

function calcRouteDistance(pts) {
	let d = 0;
	for (let i = 1; i < pts.length; i++)
		d += haversine(pts[i-1].lat, pts[i-1].lng, pts[i].lat, pts[i].lng);
	return d;
}

function formatDist(m) {
	if (!m) return '—';
	return m >= 1000 ? `${(m / 1000).toFixed(2)} km` : `${Math.round(m)} m`;
}

// Leaflet click handler — lives inside MapContainer
function MapClickHandler({ onAdd }) {
	useMapEvents({ click: e => onAdd({ lat: e.latlng.lat, lng: e.latlng.lng, timestamp: new Date().toISOString() }) });
	return null;
}

function parseGPX(text) {
	const xml = new DOMParser().parseFromString(text, 'text/xml');
	return [...xml.querySelectorAll('trkpt')].map(pt => ({
		lat: parseFloat(pt.getAttribute('lat')),
		lng: parseFloat(pt.getAttribute('lon')),
		timestamp: pt.querySelector('time')?.textContent || new Date().toISOString(),
	})).filter(p => !isNaN(p.lat) && !isNaN(p.lng));
}

export default function LogWorkout() {
	const { getToken } = useAuth();
	const navigate = useNavigate();
	const [searchParams] = useSearchParams();

	const [mode, setMode]           = useState('manual');
	const [workoutType, setType]    = useState('Running');
	const [datetime, setDatetime]   = useState(localNow);
	const [fields, setFields]       = useState({});
	const [notes, setNotes]         = useState('');
	const [saving, setSaving]       = useState(false);
	const [error, setError]         = useState('');
	const [success, setSuccess]     = useState('');
	const [integrations, setIntegrations] = useState({});
	const [syncing, setSyncing]     = useState(null);
	const [syncResult, setSyncResult] = useState(null);

	// Route state
	const [routeMode, setRouteMode] = useState('none');  // 'none' | 'draw' | 'gpx'
	const [routePoints, setRoutePoints] = useState([]);
	const [routeDist, setRouteDist] = useState(0);

	useEffect(() => {
		loadIntegrations();
		const connected = searchParams.get('connected');
		const err = searchParams.get('error');
		if (connected) {
			setSuccess(`${connected.charAt(0).toUpperCase() + connected.slice(1)} connected!`);
			setMode('connect');
			loadIntegrations();
		}
		if (err) setError(`Connection failed: ${err}`);
	}, []);

	useEffect(() => {
		setRouteDist(calcRouteDistance(routePoints));
		// Auto-fill distance field from drawn/uploaded route
		if (routePoints.length >= 2) {
			const d = calcRouteDistance(routePoints);
			setFields(prev => ({ ...prev, distance_meters: Math.round(d).toString() }));
		}
	}, [routePoints]);

	async function loadIntegrations() {
		try {
			const token = await getToken();
			setIntegrations(await api.getIntegrationStatus(token));
		} catch { /* non-critical */ }
	}

	const addPoint = useCallback(pt => setRoutePoints(prev => [...prev, pt]), []);

	function handleGPX(e) {
		const file = e.target.files[0];
		if (!file) return;
		const reader = new FileReader();
		reader.onload = ev => {
			const pts = parseGPX(ev.target.result);
			if (pts.length < 2) { setError('GPX file has fewer than 2 track points.'); return; }
			setRoutePoints(pts);
			// Set start time from first GPX point if available
			if (pts[0].timestamp) setDatetime(pts[0].timestamp.slice(0, 16));
			setError('');
		};
		reader.readAsText(file);
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
			const startISO = new Date(datetime).toISOString();

			// Build workout payload
			const workout = {
				workout_type: workoutType.toLowerCase(),
				timestamp: startISO,
				notes: notes || undefined,
			};
			for (const { key } of STAT_FIELDS) {
				if (fields[key] !== undefined && fields[key] !== '') {
					workout[key] = parseFloat(fields[key]);
				}
			}
			await api.logWorkout(token, workout);

			// If a route was drawn or uploaded, save it too
			if (routePoints.length >= 2) {
				const durMin = parseFloat(fields.duration_minutes) || 0;
				const endISO = new Date(new Date(datetime).getTime() + durMin * 60000).toISOString();
				await api.saveRoute(token, {
					workout_type: workoutType.toLowerCase(),
					coordinates:  routePoints,
					started_at:   startISO,
					ended_at:     endISO,
				});
			}

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
		} catch (e) { setError(e.message); }
	}

	async function syncStrava() {
		setSyncing('strava');
		setSyncResult(null);
		setError('');
		try {
			const token = await getToken();
			const res = await api.stravaSync(token);
			setSyncResult(res);
			setSuccess(`Synced ${res.synced} workouts and ${res.routes_saved} routes from Strava.`);
		} catch (e) { setError(e.message); }
		finally { setSyncing(null); }
	}

	const polyline = routePoints.map(p => [p.lat, p.lng]);
	const mapCenter = routePoints.length > 0
		? [routePoints[0].lat, routePoints[0].lng]
		: [51.505, -0.09];

	return (
		<div className='log-page'>
			<div className='log-header'>
				<button className='log-back' onClick={() => navigate('/dashboard')}>← Dashboard</button>
				<h1>LOG WORKOUT</h1>
				<div className='mode-tabs'>
					<button className={mode === 'manual'  ? 'active' : ''} onClick={() => setMode('manual')}>Manual</button>
					<button className={mode === 'connect' ? 'active' : ''} onClick={() => setMode('connect')}>Connect App</button>
				</div>
			</div>

			{error   && <div className='log-error'>{error}</div>}
			{success && <div className='log-success'>{success}</div>}

			{/* ── MANUAL ENTRY ── */}
			{mode === 'manual' && (
				<>
					<div className='accuracy-notice'>
						<span className='notice-icon'>⚠</span>
						<span>
							Manual entries only reflect what you type — pace, calorie burn rate, and heart rate zones are <strong>not</strong> calculated from sensor data. For accurate analysis, connect Strava or another app.
						</span>
					</div>

					<form className='log-form' onSubmit={handleSubmit}>

						{/* Workout type */}
						<div className='log-section'>
							<label className='log-label'>WORKOUT TYPE</label>
							<div className='type-grid'>
								{WORKOUT_TYPES.map(t => (
									<button key={t} type='button'
										className={`type-chip ${workoutType === t ? 'active' : ''}`}
										onClick={() => setType(t)}>{t}</button>
								))}
							</div>
						</div>

						{/* Date + time */}
						<div className='log-section'>
							<label className='log-label'>DATE & TIME</label>
							<input className='log-input' type='datetime-local' value={datetime}
								onChange={e => setDatetime(e.target.value)} required />
						</div>

						{/* Stats grid */}
						<div className='log-section'>
							<label className='log-label'>STATS</label>
							<div className='stats-grid'>
								{STAT_FIELDS.map(({ key, label, unit, required, min }) => (
									<div className='stat-field' key={key}>
										<label className='stat-label'>
											{label}{required && <span className='req'>*</span>}
										</label>
										<div className='stat-input-wrap'>
											<input className='log-input' type='number' step='any' min={min}
												placeholder='—' value={fields[key] ?? ''}
												onChange={e => setField(key, e.target.value)}
												required={required} />
											<span className='stat-unit'>{unit}</span>
										</div>
									</div>
								))}
							</div>
						</div>

						{/* Route section */}
						<div className='log-section'>
							<label className='log-label'>ROUTE <span className='optional'>(optional)</span></label>
							<div className='route-mode-tabs'>
								<button type='button' className={routeMode === 'none' ? 'active' : ''} onClick={() => { setRouteMode('none'); setRoutePoints([]); }}>None</button>
								<button type='button' className={routeMode === 'draw' ? 'active' : ''} onClick={() => setRouteMode('draw')}>Draw on Map</button>
								<button type='button' className={routeMode === 'gpx'  ? 'active' : ''} onClick={() => setRouteMode('gpx')}>Upload GPX</button>
							</div>

							{routeMode === 'draw' && (
								<div className='route-draw-wrap'>
									<p className='route-hint'>Click the map to add waypoints. Each click adds the next point in your route.</p>
									<div className='route-map-container'>
										<MapContainer center={mapCenter} zoom={13} style={{ height: '100%', width: '100%' }}>
											<TileLayer url="https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png" attribution='' />
											<MapClickHandler onAdd={addPoint} />
											{polyline.length > 1 && <Polyline positions={polyline} color="#ff3c00" weight={3} />}
											{routePoints.length > 0 && <Marker position={[routePoints[0].lat, routePoints[0].lng]} />}
											{routePoints.length > 1  && <Marker position={[routePoints[routePoints.length-1].lat, routePoints[routePoints.length-1].lng]} />}
										</MapContainer>
									</div>
									<div className='route-info'>
										<span>{routePoints.length} point{routePoints.length !== 1 ? 's' : ''}</span>
										<span>{formatDist(routeDist)}</span>
										{routePoints.length > 0 && (
											<button type='button' className='route-clear' onClick={() => setRoutePoints([])}>Clear</button>
										)}
									</div>
								</div>
							)}

							{routeMode === 'gpx' && (
								<div className='gpx-wrap'>
									<label className='gpx-label'>
										<span>Choose .gpx file</span>
										<input type='file' accept='.gpx' onChange={handleGPX} className='gpx-input' />
									</label>
									{routePoints.length > 0 && (
										<div className='gpx-loaded'>
											<span>✓ {routePoints.length} points loaded · {formatDist(routeDist)}</span>
											<button type='button' className='route-clear' onClick={() => setRoutePoints([])}>Remove</button>
										</div>
									)}
								</div>
							)}
						</div>

						{/* Notes */}
						<div className='log-section'>
							<label className='log-label'>NOTES <span className='optional'>(optional)</span></label>
							<textarea className='log-input log-textarea' placeholder='How did it feel? Any PRs?'
								value={notes} onChange={e => setNotes(e.target.value)} rows={3} />
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
						Link a fitness platform to import workouts with full sensor data — real GPS routes, per-second heart rate, and device-calculated calories and pace.
					</p>
					<div className='app-grid'>
						{APPS.map(app => {
							const isConnected = integrations[app.id];
							return (
								<div key={app.id} className={`app-card ${app.status === 'soon' ? 'soon' : ''}`}>
									<div className='app-card-top'>
										<div className='app-dot' style={{ background: app.color }} />
										<span className='app-name'>{app.name}</span>
										{isConnected     && <span className='app-badge connected'>Connected</span>}
										{app.status === 'soon'     && <span className='app-badge soon'>Soon</span>}
										{app.status === 'ios_only' && <span className='app-badge ios'>iOS Only</span>}
									</div>
									<p className='app-desc'>{app.desc}</p>

									{app.id === 'strava' && isConnected && syncResult && (
										<p className='sync-result'>
											Last sync: {syncResult.synced} workout{syncResult.synced !== 1 ? 's' : ''}, {syncResult.routes_saved} route{syncResult.routes_saved !== 1 ? 's' : ''}
										</p>
									)}

									{app.status === 'available' && (
										<div className='app-actions'>
											{isConnected ? (
												<button className='app-btn sync' onClick={syncStrava} disabled={syncing === app.id}>
													{syncing === app.id ? 'Syncing...' : 'Sync Now'}
												</button>
											) : (
												<button className='app-btn connect' onClick={connectStrava}>Connect Strava</button>
											)}
										</div>
									)}
								</div>
							);
						})}
					</div>

					<div className='strava-import-info'>
						<p className='import-info-title'>WHAT STRAVA IMPORTS</p>
						<ul>
							<li>All activity types (runs, rides, hikes, swims, gym)</li>
							<li>GPS route with per-second coordinates → saved to Routes page</li>
							<li>Heart rate stream including ending BPM</li>
							<li>Distance, moving time, calories, avg + max HR</li>
							<li>Activity name as notes</li>
						</ul>
					</div>
				</div>
			)}
		</div>
	);
}
