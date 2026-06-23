"""
Fitness AI Agents — Hugging Face Space
Gradio interface wrapping the multi-agent pipeline.
"""

import os
import sys
import json
import tempfile
import csv
import io
import random
from pathlib import Path

import gradio as gr

# Import the agent pipeline. On a deployed Space, `bots.py` is vendored into this
# directory by `prepare_space.py`. For local dev it lives in ../backend — fall back
# to that so you can run the demo without copying the file.
try:
    from bots import Bots, FormattedOutput
except ImportError:
    sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "backend"))
    from bots import Bots, FormattedOutput


# ── Sample data generator ─────────────────────────────────────────────────────

def _generate_sample_csv() -> str:
    """Generate 31 days of realistic fitness watch data as a temp CSV path."""
    import datetime

    rows = []
    base = datetime.datetime.now() - datetime.timedelta(days=31)
    devices = ["Apple Watch Ultra", "Garmin Fenix 7"]
    workout_types = ["Running", "Cycling", "Swimming", "Strength", "Walking", None]

    for day_offset in range(31):
        d = base + datetime.timedelta(days=day_offset)
        device = devices[day_offset % 2]
        is_rest = day_offset % 5 == 0  # rest every 5th day

        timestamp = d.replace(hour=7 + day_offset % 12, minute=0).isoformat()
        hr_rest = random.randint(55, 68)
        steps = random.randint(2000, 15000)
        sleep_h = round(random.uniform(5.5, 8.5), 1)
        hrv = random.randint(35, 85)
        calories = random.randint(1800, 3200)
        workout_type = None if is_rest else random.choice(workout_types)

        row = {
            "timestamp": timestamp,
            "heart_rate": hr_rest,
            "steps": steps,
            "sleep_hours": sleep_h,
            "hrv": hrv,
            "calories": calories,
            "device": device,
            "workout_type": workout_type,
        }
        rows.append(row)

    # Non-rest days: add a workout reading with elevated HR
    for i, row in enumerate(rows):
        if row["workout_type"]:
            hr_peak = random.randint(140, 195)
            duration = random.randint(20, 90)
            row["heart_rate"] = hr_peak
            row["duration_min"] = duration
        else:
            row["duration_min"] = 0

    tmp = tempfile.NamedTemporaryFile(mode="w", suffix=".csv", delete=False, newline="")
    writer = csv.DictWriter(tmp, fieldnames=rows[0].keys())
    writer.writeheader()
    writer.writerows(rows)
    tmp.close()
    return tmp.name


# ── Analysis runner ───────────────────────────────────────────────────────────

def run_analysis(context: str, use_sample: bool, file_input) -> tuple:
    """
    Run the multi-agent pipeline and return results as formatted components.
    """
    if not context or not context.strip():
        context = "Analyze my overall fitness trends from the past month."

    data_path = "(no file)"
    if use_sample:
        data_path = _generate_sample_csv()
    elif file_input is not None:
        # Gradio file input: write to temp file
        suffix = Path(file_input.name).suffix or ".csv"
        tmp = tempfile.NamedTemporaryFile(mode="w", suffix=suffix, delete=False)
        if hasattr(file_input, "read"):
            content = file_input.read()
            if isinstance(content, bytes):
                tmp.write(content.decode("utf-8", errors="replace"))
            else:
                tmp.write(content)
        else:
            tmp.write(str(file_input))
        tmp.close()
        data_path = tmp.name

    # Run the pipeline
    try:
        bots = Bots(context=context)
        bots.create_agents()
        bots.create_tasks()
        result_json = bots.create_crew(data=data_path)
        parsed = json.loads(result_json)
    except Exception as e:
        return (
            f"# Pipeline Error\n\n```\n{str(e)}\n```",
            "Error",
            "The pipeline encountered an error. Check API keys and try again.",
            str(e),
        )

    # Clean up temp files
    if data_path != "(no file)" and os.path.exists(data_path):
        try:
            os.unlink(data_path)
        except Exception:
            pass

    # Build display output
    output_type = parsed.get("output_type", "report")
    summary = parsed.get("summary", "No summary generated.")
    findings = parsed.get("findings", [])
    recommendations = parsed.get("recommendations", [])
    quality_score = parsed.get("quality_score")
    quality_verdict = parsed.get("quality_verdict", "")

    # --- Summary markdown ---
    summary_md = f"""## Analysis Summary

{summary}

**Output Type:** `{output_type}`"""

    if quality_score:
        verdict_str = quality_verdict or ""
        summary_md += f"\n\n**Quality Score:** {quality_score}/10 — {verdict_str}"

    # --- Findings ---
    findings_md = "## Findings\n\n"
    if findings:
        for i, f in enumerate(findings, 1):
            findings_md += f"{i}. {f}\n"
    else:
        findings_md += "No specific findings recorded."

    # --- Recommendations ---
    recs_md = "## Recommendations\n\n"
    if recommendations:
        for i, r in enumerate(recommendations, 1):
            recs_md += f"{i}. {r}\n"
    else:
        recs_md += "No recommendations generated."

    # --- Raw JSON (collapsible) ---
    raw_json = json.dumps(parsed, indent=2, ensure_ascii=False)

    return summary_md, findings_md, recs_md, raw_json


# ── Gradio UI ─────────────────────────────────────────────────────────────────

CUSTOM_CSS = """
.container { max-width: 960px; margin: auto; }
h1 { color: #10b981; }
"""

with gr.Blocks(theme=gr.themes.Soft(), css=CUSTOM_CSS, title="Fitness AI Agents") as demo:
    gr.Markdown(
        """
        # 🏋️ Fitness AI Agents

        Multi-source fitness data analysis powered by a pipeline of 8 specialized AI agents.
        Connect your wearables and get AI-powered insights — metrics, charts, comparisons, and coaching.

        **How it works:**
        1. Enter what you want to know (e.g., "Compare my running and cycling performance")
        2. Upload your fitness data CSV or use sample data
        3. The agent pipeline runs: Context → Data Cleaner → Prompt Engineer → Data Analyst → 
           Sports Scientist → Trend Analyst → Output Formatter → QA Critic
        4. Get structured insights with recommendations
        """
    )

    with gr.Row():
        with gr.Column(scale=2):
            context_input = gr.Textbox(
                label="What do you want to know?",
                placeholder='e.g., "Compare my running and cycling heart rate trends" or "How is my recovery this month?"',
                lines=3,
                value="Analyze my overall fitness trends from the past month.",
            )
        with gr.Column(scale=1):
            sample_checkbox = gr.Checkbox(
                label="Use sample data (31 days)",
                value=True,
                info="Generate realistic fitness watch data for demo",
            )

    with gr.Row():
        file_input = gr.File(
            label="Or upload your own CSV (columns: timestamp, heart_rate, steps, sleep_hours, hrv, device, workout_type)",
            file_types=[".csv"],
            type="filepath",
        )

    run_btn = gr.Button("🚀 Run Analysis", variant="primary", size="lg")

    with gr.Tabs():
        with gr.TabItem("📋 Summary"):
            summary_output = gr.Markdown(label="Summary", value="Click **Run Analysis** to start.")
        with gr.TabItem("🔍 Findings"):
            findings_output = gr.Markdown(label="Findings")
        with gr.TabItem("💡 Recommendations"):
            recs_output = gr.Markdown(label="Recommendations")
        with gr.TabItem("📄 Raw JSON"):
            raw_output = gr.Code(label="Raw Output", language="json")

    run_btn.click(
        fn=run_analysis,
        inputs=[context_input, sample_checkbox, file_input],
        outputs=[summary_output, findings_output, recs_output, raw_output],
    )

    gr.Markdown(
        """
        ---
        ### ⚙️ How It Works

        | Agent | Role |
        |---|---|
        | **Context Agent** | Interprets your question into a precise analysis directive |
        | **Data Cleaner** | Validates data quality and flags anomalies |
        | **Prompt Engineer** | Writes a step-by-step analysis plan |
        | **Data Analyst** | Computes metrics, trends, and cross-references sources |
        | **Sports Scientist** | Interprets findings through exercise physiology |
        | **Trend Analyst** | Detects patterns, plateaus, and correlations |
        | **Output Formatter** | Serialises results into structured JSON |
        | **QA Critic** | Scores quality and flags issues |

        Built with [CrewAI](https://crewai.com) | Models via Groq + OpenRouter
        """
    )


if __name__ == "__main__":
    demo.launch()
