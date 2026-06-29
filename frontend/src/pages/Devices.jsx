import { useState, useEffect, useCallback } from 'react';
import { useNavigate } from 'react-router-dom';
import { useAuth, UserButton } from '@clerk/react';
import { api } from '../lib/api';
import './Devices.css';

export default function Devices() {
	const { getToken } = useAuth();
	const navigate = useNavigate();
	const [devices, setDevices] = useState([]);
	const [code, setCode] = useState('');
	const [busy, setBusy] = useState('');
	const [copied, setCopied] = useState(false);
	const [error, setError] = useState('');

	const load = useCallback(async () => {
		try { setDevices((await api.listDevices(await getToken())).devices || []); }
		catch (e) { setError(e.message); }
	}, [getToken]);

	useEffect(() => {
		let active = true;
		queueMicrotask(() => {
			if (active) load();
		});
		return () => { active = false; };
	}, [load]);

	async function generate() {
		setBusy('gen'); setError(''); setCode('');
		try {
			const { token } = await api.pairDevice(await getToken(), 'Phone');
			setCode(token);
			load();
		} catch (e) { setError(e.message); }
		finally { setBusy(''); }
	}

	async function revoke(id) {
		setBusy('rev' + id);
		try { await api.revokeDevice(await getToken(), id); load(); }
		catch (e) { setError(e.message); }
		finally { setBusy(''); }
	}

	function copy() {
		navigator.clipboard?.writeText(code).then(() => { setCopied(true); setTimeout(() => setCopied(false), 1600); });
	}

	const qr = code ? `https://api.qrserver.com/v1/create-qr-code/?size=200x200&bgcolor=20-20-20&color=255-60-0&data=${encodeURIComponent(code)}` : '';

	return (
		<div className='dev-page'>
			<header className='dev-nav'>
				<button className='dev-back' onClick={() => navigate('/dashboard')}>← Dashboard</button>
				<span className='dev-logo'>Paired Devices</span>
				<UserButton />
			</header>

			{error && <div className='dev-error'>{error}</div>}

			<div className='dev-card'>
				<p className='dev-card-title'>PAIR A PHONE</p>
				<p className='dev-sub'>
					Generate a one-time code, then paste it into the FitnessAI phone app (or scan the QR).
					The code links the phone to your account — it's a revocable key, never your password.
				</p>
				<button className='dev-btn' onClick={generate} disabled={busy === 'gen'}>
					{busy === 'gen' ? 'Generating…' : '+ Generate pairing code'}
				</button>

				{code && (
					<div className='dev-code-box'>
						<p className='dev-code-label'>Paste this into the app (shown once):</p>
						<div className='dev-code-row'>
							<code>{code}</code>
							<button onClick={copy}>{copied ? '✓ Copied' : 'Copy'}</button>
						</div>
						<img className='dev-qr' src={qr} alt='Pairing QR code' />
						<p className='dev-warn'>⚠ Save it now — for security we only store a hash and can't show it again.</p>
					</div>
				)}
			</div>

			<div className='dev-card'>
				<p className='dev-card-title'>YOUR DEVICES</p>
				{devices.length === 0 ? (
					<p className='dev-muted'>No devices paired yet.</p>
				) : (
					<ul className='dev-list'>
						{devices.map(d => (
							<li key={d.id} className={d.revoked ? 'revoked' : ''}>
								<div>
									<strong>{d.device_name || 'Phone'}</strong>
									<span>{d.token_hint} · paired {(d.created_at || '').slice(0, 10)}
										{d.last_used_at ? ` · last sync ${d.last_used_at.slice(0, 10)}` : ' · never synced'}</span>
								</div>
								{d.revoked
									? <span className='dev-revoked-tag'>revoked</span>
									: <button className='dev-revoke' onClick={() => revoke(d.id)} disabled={busy === 'rev' + d.id}>Revoke</button>}
							</li>
						))}
					</ul>
				)}
			</div>

			<p className='dev-foot'>The native tracker app is on the roadmap — see <a href='/app'>Get the app</a>.</p>
		</div>
	);
}
