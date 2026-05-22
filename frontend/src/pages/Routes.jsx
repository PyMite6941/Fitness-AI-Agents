import { useState, useEffect, useRef, useCallback } from 'react';
import { useNavigate } from 'react-router-dom';
import { useAuth } from '@clerk/react';
import { MapContainer, TileLayer, Polyline, Marker, useMap } from 'react-leaflet';
import L from 'leaflet';
import 'leaflet/dist/leaflet.css';
import { api } from '../lib/api';
import './Routes.css';

// Fix leaflet default marker icons
delete L.Icon.Default.prototype._getIconUrl;
L.Icon.Default.mergeOptions({
	iconRetinaUrl: 'https://unpkg.com/leaflet@1.9.4/dist/images/marker-icon-2x.png',
	iconUrl: 'https://unpkg.com/leaflet@1.9.4/dist/images/marker-icon.png',
	shadowUrl: 'https://unpkg.com/leaflet@1.9.4/dist/images/marker-shadow.png',
});

const WORKOUT_TYPES = ['Running', 'Cycling', 'Walking', 'Hiking', 'Other'];

function MapFlyTo({ coords }) {
	const map = useMap();
	useEffect(() => {
		if (coords.length > 0) {
			map.flyTo([coords[coords.length - 1].lat, coords[coords.length - 1].lng], 15, { duration: 0.5 });
		}
	}, [coords.length]);
	return null;
}

function formatDuration(seconds) {
	if (!seconds) return '—';
	const h = Math.floor(seconds / 3600);
	const m = Math.floor((seconds % 3600) / 60);
	const s = seconds % 60;
	return h > 0 ? `${h}h ${m}m` : `${m}m ${s}s`;
}

function formatDistance(meters) {
	if (!meters) return '—';
	return meters >= 1000 ? `${(meters / 1000).toFixed(2)} km` : `${Math.round(meters)} m`;
}

export default function Routes() {
	const { getToken } = useAuth();
	const navigate = useNavigate();
	const [view, setView] = useState('list'); // 'list' | 'track' | 'detail'
	const [routes, setRoutes] = useState([]);
	const [selected, setSelected] = useState(null);
	const [loading, setLoading] = useState(true);
	const [error, setError] = useState('');

	// Tracker state
	const [tracking, setTracking] = useState(false);
	const [coords, setCoords] = useState([]);
	const [workoutType, setWorkoutType] = useState('Running');
	const [startTime, setStartTime] = useState(null);
	const [elapsed, setElapsed] = useState(0);
	const [distance, setDistance] = useState(0);
	const [saving, setSaving] = useState(false);
	const watchId = useRef(null);
	const timerRef = useRef(null);

	useEffect(() => {
		loadRoutes();
		return () => stopTracking();
	}, []);

	async function loadRoutes() {
		setLoading(true);
		try {
			const token = await getToken();
			const res = await api.getRoutes(token);
			setRoutes(res.routes);
		} catch (e) {
			setError(e.message);
		} finally {
			setLoading(false);
		}
	}

	function haversine(lat1, lng1, lat2, lng2) {
		const R = 6371000, p = Math.PI / 180;
		const a = Math.sin((lat2 - lat1) * p / 2) ** 2 +
			Math.cos(lat1 * p) * Math.cos(lat2 * p) * Math.sin((lng2 - lng1) * p / 2) ** 2;
		return 2 * R * Math.asin(Math.sqrt(a));
	}

	const startTracking = useCallback(() => {
		if (!navigator.geolocation) {
			setError('Geolocation is not supported by your browser.');
			return;
		}
		setCoords([]);
		setDistance(0);
		setElapsed(0);
		setStartTime(new Date());
		setTracking(true);

		watchId.current = navigator.geolocation.watchPosition(
			(pos) => {
				const point = {
					lat: pos.coords.latitude,
					lng: pos.coords.longitude,
					timestamp: new Date().toISOString(),
					altitude: pos.coords.altitude,
					speed: pos.coords.speed,
				};
				setCoords(prev => {
					if (prev.length > 0) {
						const d = haversine(prev[prev.length - 1].lat, prev[prev.length - 1].lng, point.lat, point.lng);
						setDistance(dd => dd + d);
					}
					return [...prev, point];
				});
			},
			(err) => setError(`GPS error: ${err.message}`),
			{ enableHighAccuracy: true, maximumAge: 0, timeout: 10000 }
		);

		timerRef.current = setInterval(() => setElapsed(e => e + 1), 1000);
	}, []);

	function stopTracking() {
		if (watchId.current !== null) {
			navigator.geolocation.clearWatch(watchId.current);
			watchId.current = null;
		}
		if (timerRef.current) {
			clearInterval(timerRef.current);
			timerRef.current = null;
		}
		setTracking(false);
	}

	async function saveRoute() {
		if (coords.length < 2) { setError('Not enough GPS data to save.'); return; }
		setSaving(true);
		try {
			const token = await getToken();
			const route = await api.saveRoute(token, {
				workout_type: workoutType.toLowerCase(),
				coordinates: coords,
				started_at: startTime.toISOString(),
				ended_at: new Date().toISOString(),
			});
			stopTracking();
			setRoutes(prev => [route, ...prev]);
			setSelected(route);
			setView('detail');
		} catch (e) {
			setError(e.message);
		} finally {
			setSaving(false);
		}
	}

	const livePolyline = coords.map(c => [c.lat, c.lng]);
	const defaultCenter = coords.length > 0
		? [coords[0].lat, coords[0].lng]
		: [51.505, -0.09];

	return (
		<div className='routes-page'>
			<div className='routes-header'>
				<button className='back-btn' style={{ padding: '0', marginRight: '12px' }} onClick={() => navigate('/dashboard')}>← Dashboard</button>
				<h1>ROUTES</h1>
				<div className='routes-tabs'>
					<button className={view === 'list' ? 'active' : ''} onClick={() => { stopTracking(); setView('list'); }}>History</button>
					<button className={view === 'track' ? 'active' : ''} onClick={() => setView('track')}>Track</button>
				</div>
			</div>

			{error && <div className='routes-error'>{error}</div>}

			{/* TRACKER */}
			{view === 'track' && (
				<div className='tracker'>
					<div className='tracker-map'>
						<MapContainer center={defaultCenter} zoom={15} style={{ height: '100%', width: '100%' }}>
							<TileLayer
								url="https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png"
								attribution='&copy; <a href="https://carto.com/">CARTO</a>'
							/>
							{livePolyline.length > 1 && <Polyline positions={livePolyline} color="#ff3c00" weight={4} />}
							{coords.length > 0 && <Marker position={[coords[coords.length - 1].lat, coords[coords.length - 1].lng]} />}
							<MapFlyTo coords={coords} />
						</MapContainer>
					</div>

					<div className='tracker-controls'>
						<div className='tracker-stats'>
							<div className='tracker-stat'><strong>{formatDuration(elapsed)}</strong><span>Duration</span></div>
							<div className='tracker-stat'><strong>{formatDistance(distance)}</strong><span>Distance</span></div>
							<div className='tracker-stat'><strong>{coords.length}</strong><span>Points</span></div>
						</div>

						{!tracking ? (
							<>
								<select className='type-select' value={workoutType} onChange={e => setWorkoutType(e.target.value)}>
									{WORKOUT_TYPES.map(t => <option key={t}>{t}</option>)}
								</select>
								<button className='track-btn start' onClick={startTracking}>START TRACKING</button>
							</>
						) : (
							<div className='tracker-actions'>
								<button className='track-btn stop' onClick={stopTracking}>PAUSE</button>
								<button className='track-btn save' onClick={saveRoute} disabled={saving || coords.length < 2}>
									{saving ? 'SAVING...' : 'FINISH & SAVE'}
								</button>
							</div>
						)}
					</div>
				</div>
			)}

			{/* ROUTE DETAIL */}
			{view === 'detail' && selected && (
				<div className='route-detail'>
					<button className='back-btn' onClick={() => setView('list')}>← Back</button>
					<div className='detail-stats'>
						<div className='detail-stat'><strong>{formatDistance(selected.distance_meters)}</strong><span>Distance</span></div>
						<div className='detail-stat'><strong>{formatDuration(selected.duration_seconds)}</strong><span>Duration</span></div>
						<div className='detail-stat'><strong>{selected.pace || '—'}</strong><span>Pace</span></div>
						<div className='detail-stat'><strong style={{ textTransform: 'capitalize' }}>{selected.workout_type}</strong><span>Type</span></div>
					</div>
					{selected.coordinates?.length > 1 && (
						<div className='detail-map'>
							<MapContainer
								center={[selected.coordinates[0].lat, selected.coordinates[0].lng]}
								zoom={14}
								style={{ height: '100%', width: '100%' }}
							>
								<TileLayer
									url="https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png"
									attribution='&copy; <a href="https://carto.com/">CARTO</a>'
								/>
								<Polyline
									positions={selected.coordinates.map(c => [c.lat, c.lng])}
									color="#ff3c00"
									weight={4}
								/>
								<Marker position={[selected.coordinates[0].lat, selected.coordinates[0].lng]} />
								<Marker position={[selected.coordinates[selected.coordinates.length - 1].lat, selected.coordinates[selected.coordinates.length - 1].lng]} />
							</MapContainer>
						</div>
					)}
				</div>
			)}

			{/* ROUTE LIST */}
			{view === 'list' && (
				<div className='routes-list'>
					{loading && <p className='dim'>Loading routes...</p>}
					{!loading && routes.length === 0 && (
						<div className='routes-empty'>
							<p>No routes yet.</p>
							<button className='track-btn start' onClick={() => setView('track')}>TRACK YOUR FIRST ROUTE</button>
						</div>
					)}
					{routes.map(r => (
						<button key={r.id} className='route-card' onClick={() => { setSelected(r); setView('detail'); }}>
							<div className='route-card-left'>
								<span className='route-type'>{r.workout_type}</span>
								<span className='route-date'>{new Date(r.started_at).toLocaleDateString('en-US', { weekday: 'short', month: 'short', day: 'numeric' })}</span>
							</div>
							<div className='route-card-right'>
								<span className='route-dist'>{formatDistance(r.distance_meters)}</span>
								<span className='route-meta'>{formatDuration(r.duration_seconds)} · {r.pace || '—'}</span>
							</div>
							{r.coordinates?.length > 1 && (
								<div className='route-card-map'>
									<MapContainer
										center={[r.coordinates[0].lat, r.coordinates[0].lng]}
										zoom={13}
										zoomControl={false}
										dragging={false}
										scrollWheelZoom={false}
										style={{ height: '100%', width: '100%', pointerEvents: 'none' }}
									>
										<TileLayer url="https://{s}.basemaps.cartocdn.com/dark_all/{z}/{x}/{y}{r}.png" attribution='' />
										<Polyline positions={r.coordinates.map(c => [c.lat, c.lng])} color="#ff3c00" weight={3} />
									</MapContainer>
								</div>
							)}
						</button>
					))}
				</div>
			)}
		</div>
	);
}
