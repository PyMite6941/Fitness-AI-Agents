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
	{ id: 'strava',        name: 'Strava',         desc: 'Sync runs, rides, and GPS routes automatically.',                                                                   color: '#FC4C02', status: 'available' },
	{ id: 'fitbit',        name: 'Fitbit',          desc: 'Connect your Fitbit account to sync workouts and sleep automatically.',                                             color: '#00B0B9', status: 'oauth'     },
	{ id: 'nike_run_club', name: 'Nike Run Club',   desc: 'Import your NRC data export (ZIP or JSON) — includes dates, GPS, HR, and distance.',                               color: '#111',    status: 'import'   },
	{ id: 'garmin',        name: 'Garmin Connect',  desc: 'Export any activity from Garmin Connect as .gpx or .tcx and upload it here.',                                      color: '#007CC3', status: 'import'   },
	{ id: 'apple_health',  name: 'Apple Health',    desc: 'Export from Health app → Profile → Export All Health Data. Upload the ZIP or export.xml.',                         color: '#fff',    status: 'import'   },
	{ id: 'google_fit',    name: 'Google Fit',      desc: 'Export via takeout.google.com (select Fit only). Upload the downloaded ZIP.',                                      color: '#4285F4', status: 'import'   },
	// Universal file import — any app/device that exports .fit / .tcx / .gpx.
	{ id: 'coros',         name: 'COROS',           desc: 'COROS app → activity → Export as .FIT or .GPX, then upload here.',                                                  color: '#FF4F00', status: 'file'     },
	{ id: 'suunto',        name: 'Suunto',          desc: 'Suunto app → workout → Export (.FIT/.GPX/.TCX) and upload here.',                                                    color: '#0a0a0a', status: 'file'     },
	{ id: 'wahoo',         name: 'Wahoo',           desc: 'Wahoo / ELEMNT → export a ride as .FIT or .TCX and upload here.',                                                    color: '#1BA0E2', status: 'file'     },
	{ id: 'polar',         name: 'Polar Flow',      desc: 'Polar Flow → activity → Export session (.TCX/.GPX) and upload here.',                                                color: '#D6001C', status: 'file'     },
	{ id: 'zwift',         name: 'Zwift',           desc: 'Zwift saves a .FIT for every ride (Documents/Zwift/Activities). Upload it here.',                                    color: '#FC6719', status: 'file'     },
	{ id: 'peloton',       name: 'Peloton',         desc: 'Peloton workout → download .TCX and upload here for HR + output.',                                                   color: '#181A1D', status: 'file'     },
	{ id: 'mapmyrun',      name: 'MapMyRun',        desc: 'MapMyRun / MapMyFitness → workout → Export (.TCX/.GPX) and upload here.',                                            color: '#E01D2C', status: 'file'     },
	{ id: 'other',         name: 'Other (.fit/.tcx/.gpx)', desc: 'Any other app or device — upload a standard .fit, .tcx, or .gpx file.',                                          color: '#888',    status: 'file'     },
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
	const [syncing, setSyncing]       = useState(null);
	const [syncResult, setSyncResult] = useState(null);
	const [nikeResult,    setNikeResult]    = useState(null);
	const [fitbitResult,  setFitbitResult]  = useState(null);
	const [garminResult,  setGarminResult]  = useState(null);
	const [appleResult,   setAppleResult]   = useState(null);
	const [googleResult,  setGoogleResult]  = useState(null);
	const [fileResult,    setFileResult]    = useState(null);

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

	// datetime-local gives "YYYY-MM-DDTHH:mm" in local time — append offset so toISOString() is correct
	function localToISO(localStr) {
		const d = new Date(localStr);
		const off = -d.getTimezoneOffset();
		const sign = off >= 0 ? '+' : '-';
		const pad = n => String(Math.floor(Math.abs(n))).padStart(2, '0');
		return `${localStr}:00${sign}${pad(off / 60)}:${pad(off % 60)}`;
	}

	async function handleSubmit(e) {
		e.preventDefault();
		setError('');
		setSaving(true);
		try {
			const token = await getToken();
			const startISO = localToISO(datetime);

			// Build workout payload
			const workout = {
				workout_type: workoutType.toLowerCase(),
				timestamp: startISO,
				notes: notes || undefined,
			};
			for (const { key } of STAT_FIELDS) {
				if (fields[key] !== undefined && fields[key] !== '') {
					const val = parseFloat(fields[key]);
					if (!isNaN(val)) workout[key] = val;
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

	async function connectFitbit() {
		try {
			const token = await getToken();
			const { url } = await api.fitbitConnectUrl(token);
			window.location.href = url;
		} catch (e) { setError(e.message); }
	}

	async function syncFitbit() {
		setSyncing('fitbit');
		setFitbitResult(null);
		setError('');
		try {
			const token = await getToken();
			const res = await api.fitbitSync(token);
			setFitbitResult(res);
			setSuccess(`Synced ${res.synced} workout${res.synced !== 1 ? 's' : ''} and ${res.sleep_synced} sleep log${res.sleep_synced !== 1 ? 's' : ''} from Fitbit.`);
		} catch (e) { setError(e.message); }
		finally { setSyncing(null); }
	}

	async function handleGarminImport(e) {
		const file = e.target.files[0];
		if (!file) return;
		setSyncing('garmin');
		setGarminResult(null);
		setError('');
		try {
			const token = await getToken();
			const res = await api.garminImport(token, file);
			if (res.error) { setError(`Garmin import error: ${res.error}`); return; }
			setGarminResult(res);
			setSuccess(`Imported ${res.imported} workout${res.imported !== 1 ? 's' : ''} and ${res.routes_saved} route${res.routes_saved !== 1 ? 's' : ''} from Garmin.`);
		} catch (e) { setError(e.message); }
		finally { setSyncing(null); e.target.value = ''; }
	}

	async function handleAppleImport(e) {
		const file = e.target.files[0];
		if (!file) return;
		setSyncing('apple_health');
		setAppleResult(null);
		setError('');
		try {
			const token = await getToken();
			const res = await api.appleImport(token, file);
			if (res.error) { setError(`Apple Health import error: ${res.error}`); return; }
			setAppleResult(res);
			setSuccess(`Imported ${res.imported} workout${res.imported !== 1 ? 's' : ''} and ${res.readings_synced} health reading${res.readings_synced !== 1 ? 's' : ''} from Apple Health.`);
		} catch (e) { setError(e.message); }
		finally { setSyncing(null); e.target.value = ''; }
	}

	async function handleGoogleFitImport(e) {
		const file = e.target.files[0];
		if (!file) return;
		setSyncing('google_fit');
		setGoogleResult(null);
		setError('');
		try {
			const token = await getToken();
			const res = await api.googleFitImport(token, file);
			if (res.error) { setError(`Google Fit import error: ${res.error}`); return; }
			setGoogleResult(res);
			setSuccess(`Imported ${res.imported} workout${res.imported !== 1 ? 's' : ''} and ${res.readings_synced} daily reading${res.readings_synced !== 1 ? 's' : ''} from Google Fit.`);
		} catch (e) { setError(e.message); }
		finally { setSyncing(null); e.target.value = ''; }
	}

	async function handleFileImport(e, source, sourceName) {
		const file = e.target.files[0];
		if (!file) return;
		setSyncing(source);
		setFileResult(null);
		setError('');
		try {
			const token = await getToken();
			const res = await api.fileImport(token, file, source);
			if (res.error) { setError(`${sourceName} import error: ${res.error}`); return; }
			setFileResult({ source, ...res });
			setSuccess(`Imported ${res.imported} workout${res.imported !== 1 ? 's' : ''} and ${res.routes_saved} route${res.routes_saved !== 1 ? 's' : ''} from ${sourceName}.`);
		} catch (e) { setError(e.message); }
		finally { setSyncing(null); e.target.value = ''; }
	}

	async function handleNikeImport(e) {
		const file = e.target.files[0];
		if (!file) return;
		setSyncing('nike_run_club');
		setNikeResult(null);
		setError('');
		try {
			const token = await getToken();
			const res = await api.nikeImport(token, file);
			if (res.error) { setError(`Nike import error: ${res.error}`); return; }
			setNikeResult(res);
			setSuccess(`Imported ${res.imported} workout${res.imported !== 1 ? 's' : ''} and ${res.routes_saved} route${res.routes_saved !== 1 ? 's' : ''} from Nike Run Club.`);
		} catch (e) { setError(e.message); }
		finally { setSyncing(null); e.target.value = ''; }
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
							const busy = syncing === app.id;

							// Per-app last-result display
							const resultMap = {
								strava:        syncResult   ? `Last sync: ${syncResult.synced} workouts, ${syncResult.routes_saved} routes` : null,
								fitbit:        fitbitResult ? `Last sync: ${fitbitResult.synced} workouts, ${fitbitResult.sleep_synced} sleep logs` : null,
								nike_run_club: nikeResult   ? `Last import: ${nikeResult.imported} workouts, ${nikeResult.routes_saved} routes` : null,
								garmin:        garminResult ? `Last import: ${garminResult.imported} workouts, ${garminResult.routes_saved} routes` : null,
								apple_health:  appleResult  ? `Last import: ${appleResult.imported} workouts, ${appleResult.readings_synced} readings` : null,
								google_fit:    googleResult ? `Last import: ${googleResult.imported} workouts, ${googleResult.readings_synced} readings` : null,
								[fileResult?.source]: fileResult ? `Last import: ${fileResult.imported} workouts, ${fileResult.routes_saved} routes` : null,
							};

							// Per-app file accept & handler
							const importConfig = {
								nike_run_club: { accept: '.zip,.json', handler: handleNikeImport },
								garmin:        { accept: '.gpx,.tcx',  handler: handleGarminImport },
								apple_health:  { accept: '.zip,.xml',  handler: handleAppleImport },
								google_fit:    { accept: '.zip',       handler: handleGoogleFitImport },
							};

							return (
								<div key={app.id} className='app-card'>
									<div className='app-card-top'>
										<div className='app-dot' style={{ background: app.color }} />
										<span className='app-name'>{app.name}</span>
										{isConnected && <span className='app-badge connected'>Connected</span>}
									</div>
									<p className='app-desc'>{app.desc}</p>

									{resultMap[app.id] && (
										<p className='sync-result'>{resultMap[app.id]}</p>
									)}

									{/* Strava — OAuth + sync */}
									{app.status === 'available' && (
										<div className='app-actions'>
											{isConnected
												? <button className='app-btn sync' onClick={syncStrava} disabled={busy}>{busy ? 'Syncing…' : 'Sync Now'}</button>
												: <button className='app-btn connect' onClick={connectStrava}>Connect Strava</button>
											}
										</div>
									)}

									{/* Fitbit — OAuth + sync */}
									{app.status === 'oauth' && (
										<div className='app-actions'>
											{isConnected
												? <button className='app-btn sync' onClick={syncFitbit} disabled={busy}>{busy ? 'Syncing…' : 'Sync Now'}</button>
												: <button className='app-btn connect' onClick={connectFitbit}>Connect Fitbit</button>
											}
										</div>
									)}

									{/* File-import platforms (app-specific exports) */}
									{app.status === 'import' && (() => {
										const cfg = importConfig[app.id];
										if (!cfg) return null;
										return (
											<div className='app-actions'>
												<label className={`app-btn sync${busy ? ' disabled' : ''}`} style={{ cursor: busy ? 'not-allowed' : 'pointer' }}>
													{busy ? 'Importing…' : 'Upload Export'}
													<input type='file' accept={cfg.accept} style={{ display: 'none' }}
														disabled={busy} onChange={cfg.handler} />
												</label>
											</div>
										);
									})()}

									{/* Universal file import (.fit/.tcx/.gpx) — any device/app */}
									{app.status === 'file' && (
										<div className='app-actions'>
											<label className={`app-btn sync${busy ? ' disabled' : ''}`} style={{ cursor: busy ? 'not-allowed' : 'pointer' }}>
												{busy ? 'Importing…' : 'Upload File'}
												<input type='file' accept='.fit,.tcx,.gpx' style={{ display: 'none' }}
													disabled={busy} onChange={(e) => handleFileImport(e, app.id, app.name)} />
											</label>
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

					<div className='strava-import-info' style={{ marginTop: '1rem' }}>
						<p className='import-info-title'>HOW TO EXPORT NIKE RUN CLUB DATA</p>
						<ol style={{ paddingLeft: '1.2rem', lineHeight: '1.8' }}>
							<li>Open the Nike app → Profile → Settings → Privacy Settings</li>
							<li>Tap <strong>Download Your Data</strong> and request the export</li>
							<li>Nike emails you a ZIP file — download it</li>
							<li>Upload the ZIP (or any activity JSON from inside it) using the button above</li>
						</ol>
						<p className='import-info-title' style={{ marginTop: '0.75rem' }}>WHAT NRC IMPORTS</p>
						<ul>
							<li>All runs, walks, hikes, and cycle activities with original dates</li>
							<li>GPS coordinates per data point → saved to Routes page</li>
							<li>Heart rate (avg, max, ending) from wearable data</li>
							<li>Distance (km), duration, and calories</li>
							<li>Activity name as notes</li>
						</ul>
					</div>
				</div>
			)}
		</div>
	);
}
