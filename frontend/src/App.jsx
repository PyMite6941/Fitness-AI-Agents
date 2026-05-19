import { useState } from 'react';

export default function FitnessAI() {
	async function handleFile({ file }) {
		const reader = new FileReader();
		reader.onload = (e) => {
			const text = e.target.result;
		};
	}

	return (
		<>
			<div style={style.heroSection}>
				<h1>FitnessAI</h1>
				<h2>
					<em>Your tool to a better workout</em>
				</h2>
			</div>
			<div style={style.upload}>
				<input
					type='file'
					accept='.csv'
					onChange={(e) => handleFile(e.target.files[0])}
				/>
			</div>
		</>
	);
}
