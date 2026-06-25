import { useState, useRef, useEffect } from 'react';
import { useNavigate } from 'react-router-dom';
import { useAuth, UserButton } from '@clerk/react';
import { api } from '../lib/api';
import './Chat.css';

const SUGGESTIONS = [
	'How did sleep affect my runs?',
	'What was my best training week?',
	'Am I improving over time?',
	'Which source has the most data?',
];

export default function Chat() {
	const { getToken } = useAuth();
	const navigate = useNavigate();
	const [messages, setMessages] = useState([]);
	const [input, setInput] = useState('');
	const [busy, setBusy] = useState(false);
	const [error, setError] = useState('');
	const [quota, setQuota] = useState(null);
	const endRef = useRef(null);

	useEffect(() => { endRef.current?.scrollIntoView({ behavior: 'smooth' }); }, [messages, busy]);

	async function send(text) {
		const content = (text ?? input).trim();
		if (!content || busy) return;
		setInput(''); setError('');
		const next = [...messages, { role: 'user', content }];
		setMessages(next);
		setBusy(true);
		try {
			const token = await getToken();
			const { reply, quota: q } = await api.chat(token, next);
			setMessages([...next, { role: 'assistant', content: reply }]);
			if (q) setQuota(q);
		} catch (e) {
			setError(e.message || 'Chat failed.');
			setMessages(next);
		} finally { setBusy(false); }
	}

	return (
		<div className='chat-page'>
			<header className='chat-nav'>
				<button className='chat-back' onClick={() => navigate('/dashboard')}>← Dashboard</button>
				<span className='chat-logo'>Chat with your data</span>
				<UserButton />
			</header>

			<div className='chat-body'>
				{messages.length === 0 && (
					<div className='chat-empty'>
						<h2>Ask anything about your fitness data</h2>
						<p>Grounded in your own history — it won't make numbers up.</p>
						<div className='chat-suggest'>
							{SUGGESTIONS.map(s => <button key={s} onClick={() => send(s)}>{s}</button>)}
						</div>
					</div>
				)}
				{messages.map((m, i) => (
					<div key={i} className={`chat-msg ${m.role}`}>
						<div className='chat-bubble'>{m.content}</div>
					</div>
				))}
				{busy && <div className='chat-msg assistant'><div className='chat-bubble typing'>…</div></div>}
				{error && <div className='chat-err'>{error}</div>}
				<div ref={endRef} />
			</div>

			<div className='chat-input-row'>
				<input
					value={input}
					onChange={e => setInput(e.target.value)}
					onKeyDown={e => e.key === 'Enter' && send()}
					placeholder='Ask about your runs, sleep, heart rate, trends…'
					disabled={busy}
				/>
				<button onClick={() => send()} disabled={busy || !input.trim()}>Send</button>
			</div>
			{quota && (quota.tokens_remaining || quota.requests_remaining) && (
				<div className='chat-quota'>
					{quota.model}
					{quota.tokens_remaining ? ` · ${quota.tokens_remaining} tokens left` : ''}
					{quota.requests_remaining ? ` · ${quota.requests_remaining} requests left` : ''}
				</div>
			)}
		</div>
	);
}
