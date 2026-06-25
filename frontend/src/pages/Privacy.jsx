import { useState } from 'react';
import { useNavigate } from 'react-router-dom';
import { useAuth } from '@clerk/react';
import { api } from '../lib/api';
import './Privacy.css';

export default function Privacy() {
	const navigate = useNavigate();
	const { isSignedIn, getToken } = useAuth();
	const [busy, setBusy] = useState(false);
	const [done, setDone] = useState('');
	const [error, setError] = useState('');

	async function deleteData() {
		if (!window.confirm('Permanently delete ALL your FitnessAI data? This cannot be undone.')) return;
		setBusy(true); setError('');
		try {
			const res = await api.deleteMyData(await getToken());
			const total = Object.values(res.deleted || {}).reduce((a, v) => a + (typeof v === 'number' ? v : 0), 0);
			setDone(`Deleted ${total} records. Your account data has been erased.`);
		} catch (e) { setError(e.message); }
		finally { setBusy(false); }
	}

	return (
		<div className='privacy-page'>
			<button className='privacy-back' onClick={() => navigate('/')}>← Back</button>
			<h1>Privacy &amp; Your Data</h1>
			<p className='privacy-lead'>Plain-English version. Your health data is yours — you can export or delete it anytime.</p>

			<section>
				<h2>What we store</h2>
				<p>Fitness data you import or log: workouts, heart rate, HRV, sleep, steps, GPS routes, and AI analysis results. It's keyed to your account and isolated by row-level security so no other user (or the public API key) can read it.</p>
			</section>
			<section>
				<h2>How it's used</h2>
				<p>Only to show your dashboard and to run analysis <strong>when you ask</strong> (AI Coach, Chat, Analyze). Nothing runs on your data in the background, and we never sell it.</p>
			</section>
			<section>
				<h2>Who processes it</h2>
				<ul>
					<li><strong>Clerk</strong> — sign-in/accounts.</li>
					<li><strong>Supabase</strong> — encrypted database (your data, row-level secured).</li>
					<li><strong>Groq / OpenRouter</strong> — the AI models. Only a compact summary of your data is sent at the moment you request an analysis or chat; it isn't used to train models.</li>
				</ul>
			</section>
			<section>
				<h2>Security</h2>
				<p>HTTPS everywhere. Row-level security denies all access except your authenticated requests. Paired devices use revocable, hashed tokens — never your password.</p>
			</section>

			<section className='privacy-danger'>
				<h2>Delete my data</h2>
				<p>Erase everything we store for your account: workouts, readings, routes, analyses, plans, integrations, and paired devices.</p>
				{isSignedIn ? (
					<>
						<button className='privacy-del-btn' onClick={deleteData} disabled={busy}>
							{busy ? 'Deleting…' : 'Permanently delete all my data'}
						</button>
						{done && <p className='privacy-done'>{done}</p>}
						{error && <p className='privacy-err'>{error}</p>}
					</>
				) : (
					<p className='privacy-muted'>Sign in to delete your data.</p>
				)}
			</section>

			<p className='privacy-foot'>Questions? This is a portfolio project — reach out via the GitHub repo.</p>
		</div>
	);
}
