import { useState } from 'react';
import './App.css';

export default function FitnessAI() {
	const [file, setFile] = useState(null);
	const [context, setContext] = useState('');
	const [loading, setLoading] = useState(false);

	function handleFile(e) {
		setFile(e.target.files[0]);
	}

	async function handleSubmit() {
		if (!file || !context) return;
		setLoading(true);
		// TODO: wire to FastAPI
		setLoading(false);
	}

	return (
		<div className='page'>
			<section className='hero'>
				<div className='hero-overlay' />
				<div className='hero-content'>
					<p className='hero-tag'>AI-POWERED PERFORMANCE</p>
					<h1>TRAIN SMARTER.<br />RECOVER FASTER.</h1>
					<p className='hero-sub'>Drop your fitness data and let the AI do the heavy lifting.</p>
					<a href='#upload' className='hero-btn'>GET STARTED</a>
				</div>
			</section>

			<section className='upload-section' id='upload'>
				<div className='upload-card'>
					<h2>Analyze Your Data</h2>
					<p className='upload-sub'>Upload any fitness dataset and describe what you want to know.</p>

					<label className='file-drop'>
						<input type='file' accept='.csv,.json,.pdf,.xml,.txt' onChange={handleFile} />
						<span className='file-icon'>📂</span>
						<span>{file ? file.name : 'Drop your file here or click to browse'}</span>
					</label>

					<textarea
						className='context-input'
						placeholder='What do you want to know? e.g. "Show me my best performing workout days and identify recovery patterns."'
						value={context}
						onChange={(e) => setContext(e.target.value)}
						rows={4}
					/>

					<button className='submit-btn' onClick={handleSubmit} disabled={!file || !context || loading}>
						{loading ? 'ANALYZING...' : 'ANALYZE'}
					</button>
				</div>
			</section>
		</div>
	);
}
