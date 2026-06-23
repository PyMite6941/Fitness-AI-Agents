"""
Fitness AI Agents — buffed multi-agent pipeline.
8 agents: context → cleaner → prompt → analyst → sports_scientist → trend → format → QA.

Improvements over v1:
  - Exponential backoff + jitter for rate limits
  - Model quality scoring (tracks success/fail per model)
  - Debug log callbacks for SSE / progress tracking
  - Graceful degradation: falls back to report when structured output fails
  - Chain-of-thought scaffolding in every agent prompt
  - Token-budget-aware task descriptions
  - Warmup: initial small call to detect dead models fast
  - Better JSON extraction with nested object recovery
  - Input validation
  - Structured logging with agent name prefixing
"""

# ── Stdlib ────────────────────────────────────────────────────────────────────
import sys
import io
import queue
import threading
import tempfile
import os
import re as _re
import time as _time
import asyncio
import json
import json as _json
import random as _random
from threading import Lock
from typing import Optional, Literal, List as _List, Callable

# ── Third-party ───────────────────────────────────────────────────────────────
from dotenv import load_dotenv
load_dotenv()

from pydantic import BaseModel, ValidationError

from crewai import Agent, Crew, Task, Process, LLM
from crewai_tools import FileReadTool

import litellm
litellm.cache = None
litellm.drop_params = True

# ── Groq cache_breakpoint patch ───────────────────────────────────────────────
_real_completion = litellm.completion

def _completion_no_cache_breakpoint(*args, **kwargs):
    kwargs["caching"] = False
    for msg in kwargs.get("messages", []):
        if isinstance(msg, dict):
            msg.pop("cache_breakpoint", None)
            if isinstance(msg.get("content"), list):
                for block in msg["content"]:
                    if isinstance(block, dict):
                        block.pop("cache_breakpoint", None)
    return _real_completion(*args, **kwargs)

litellm.completion = _completion_no_cache_breakpoint

# ── Logging helper ────────────────────────────────────────────────────────────
_LOG_LOCK = Lock()
def _log(agent_name: str, msg: str) -> None:
    """Structured log with timestamp and agent name."""
    ts = _time.strftime("%H:%M:%S", _time.localtime())
    with _LOG_LOCK:
        print(f"[{ts}][{agent_name}] {msg}", flush=True)

# ── Model rotation pools ──────────────────────────────────────────────────────
_FAST_MODELS = [
    "groq/llama-3.1-8b-instant",
    "groq/gemma2-9b-it",
    "groq/llama3-8b-8192",
    "openrouter/nvidia/nemotron-nano-9b-v2:free",
    "openrouter/minimax/minimax-m2.5:free",
    "openrouter/meta-llama/llama-3.1-8b-instruct:free",
    "openrouter/mistralai/mistral-7b-instruct:free",
    "openrouter/google/gemma-3-12b-it:free",
    "openrouter/qwen/qwen3-8b:free",
    "openrouter/meta-llama/llama-4-scout:free",
    "openrouter/microsoft/phi-3-mini-128k-instruct:free",
]
_SMART_MODELS = [
    "groq/llama-3.3-70b-versatile",
    "groq/llama3-70b-8192",
    "openrouter/google/gemma-3-27b-it:free",
    "openrouter/qwen/qwen3-coder:free",
    "openrouter/meta-llama/llama-3.3-70b-instruct:free",
    "openrouter/deepseek/deepseek-chat-v3-0324:free",
    "openrouter/meta-llama/llama-4-maverick:free",
    "openrouter/mistralai/mistral-small-3.1-24b-instruct:free",
    "openrouter/google/gemma-3-12b-it:free",
]

# ── Model quality tracker ─────────────────────────────────────────────────────
_model_scores: dict[str, float] = {}  # model_name → exponentially weighted score
_model_score_lock = Lock()
_MODEL_DECAY = 0.9  # exponential decay factor for historical scores

def _record_model_outcome(model: str, success: bool) -> None:
    """Update exponentially weighted moving average for a model."""
    with _model_score_lock:
        prev = _model_scores.get(model, 0.5)
        alpha = 0.3
        _model_scores[model] = prev * (1 - alpha) + (1.0 if success else 0.0) * alpha

def _get_model_score(model: str) -> float:
    with _model_score_lock:
        return _model_scores.get(model, 0.5)

# ── Cooldown state ────────────────────────────────────────────────────────────
_GROQ_MODELS_ALL: frozenset = frozenset(
    m for m in _FAST_MODELS + _SMART_MODELS if m.startswith("groq/")
)
_cooldown: dict[str, float] = {}
_cooldown_lock = Lock()
_crew_lock = Lock()

def _set_cooldown(model: str, seconds: float) -> None:
    until = _time.monotonic() + seconds
    with _cooldown_lock:
        if model.startswith("groq/"):
            for m in _GROQ_MODELS_ALL:
                _cooldown[m] = max(_cooldown.get(m, 0.0), until)
        else:
            _cooldown[model] = max(_cooldown.get(model, 0.0), until)

def _pick_model(pool: list[str]) -> tuple[str, int]:
    """Pick the best model: available, then highest-scoring available."""
    now = _time.monotonic()
    with _cooldown_lock:
        candidates = []
        for idx, model in enumerate(pool):
            if _cooldown.get(model, 0.0) <= now:
                score = _get_model_score(model)
                candidates.append((score, idx, model))
        if candidates:
            candidates.sort(key=lambda x: (-x[0], x[1]))
            _, idx, model = candidates[0]
            return model, idx
        best = min(range(len(pool)), key=lambda i: _cooldown.get(pool[i], 0.0))
        _log("Scheduler", f"All models in cooldown — using {pool[best]}")
        return pool[best], best

def _wait_until_available() -> None:
    now = _time.monotonic()
    with _cooldown_lock:
        fast_waits = [max(0.0, _cooldown.get(m, 0.0) - now) for m in _FAST_MODELS]
        smart_waits = [max(0.0, _cooldown.get(m, 0.0) - now) for m in _SMART_MODELS]
    sleep_s = max(min(fast_waits), min(smart_waits))
    if sleep_s > 0:
        _log("Scheduler", f"All providers cooling — resuming in {sleep_s:.0f}s")
        _time.sleep(sleep_s)

# ── Retry-after parsing ───────────────────────────────────────────────────────
def _parse_retry_after(err_str: str) -> float:
    m = _re.search(r"retry_after_seconds['\"\s:]+(\d+(?:\.\d+)?)", err_str)
    if m:
        return float(m.group(1)) + 5
    m = _re.search(r"[Pp]lease try again in (\d+(?:\.\d+)?)s", err_str)
    if m:
        return float(m.group(1)) + 2
    return 35.0

def _exponential_backoff(attempt: int) -> float:
    """Exponential backoff with jitter: base 5s, cap 120s."""
    base = 5.0 * (2 ** attempt)
    jitter = _random.uniform(0, 0.5 * base)
    return min(base + jitter, 120.0)

# ── Robust JSON extraction ────────────────────────────────────────────────────
def _extract_json(text: str) -> str:
    """Extract valid JSON from model output, handling fences, preamble, truncation."""
    if not text or not text.strip():
        return '{"output_type":"report","summary":"No output from model.","findings":["Analysis produced no output."],"recommendations":["Check data quality and try again."]}'

    text = text.strip()

    # Remove markdown fences
    text = _re.sub(r"^```(?:json)?\s*", "", text, flags=_re.MULTILINE)
    text = _re.sub(r"\s*```$", "", text, flags=_re.MULTILINE)
    text = text.strip()

    # Try direct parse
    try:
        _json.loads(text)
        return text
    except _json.JSONDecodeError:
        pass

    # Try to find well-formed JSON object
    start = text.find("{")
    if start != -1:
        depth = 0
        in_string = False
        escape_next = False
        for i, ch in enumerate(text[start:], start):
            if escape_next:
                escape_next = False
                continue
            if ch == "\\" and in_string:
                escape_next = True
                continue
            if ch == '"':
                in_string = not in_string
                continue
            if in_string:
                continue
            if ch == "{":
                depth += 1
            elif ch == "}":
                depth -= 1
                if depth == 0:
                    candidate = text[start: i + 1]
                    try:
                        _json.loads(candidate)
                        return candidate
                    except _json.JSONDecodeError:
                        break
        # If we found a start but couldn't close, try to repair it
        # This handles truncated JSON — we'll wrap it in a report fallback

    # Try to find array
    start = text.find("[")
    if start != -1:
        depth = 0
        in_string = False
        escape_next = False
        for i, ch in enumerate(text[start:], start):
            if escape_next:
                escape_next = False
                continue
            if ch == "\\" and in_string:
                escape_next = True
                continue
            if ch == '"':
                in_string = not in_string
                continue
            if in_string:
                continue
            if ch == "[":
                depth += 1
            elif ch == "]":
                depth -= 1
                if depth == 0:
                    return text[start: i + 1]

    # Last resort: wrap everything in a report
    _log("JSON-Parser", "Could not extract valid JSON — wrapping in fallback report")
    escaped = text.replace("\\", "\\\\").replace('"', '\\"')[:2000]
    return _json.dumps({
        "output_type": "report",
        "summary": "Model produced non-JSON output. See findings for raw text.",
        "findings": [f"Raw output: {escaped[:500]}"],
        "recommendations": ["Review raw output above."],
    })

# ── API key resolution ────────────────────────────────────────────────────────
_OR_KEY = os.getenv("OPENROUTER_API_KEY", "")
if _OR_KEY:
    os.environ.setdefault("OPENAI_API_KEY", _OR_KEY)

def _api_key_for(model: str) -> str | None:
    if model.startswith("groq/"):
        return os.getenv("GROQ_API_KEY")
    return os.getenv("OPENROUTER_API_KEY")

# ── Output schemas ────────────────────────────────────────────────────────────

class DataPoint(BaseModel):
    label: str
    value: float
    category: Optional[str] = None
    x_value: Optional[float] = None
    value2: Optional[float] = None

class CodeBlock(BaseModel):
    language: str
    title: str
    code: str

class MetricItem(BaseModel):
    label: str
    value: str
    unit: Optional[str] = None
    trend: Optional[str] = None
    change: Optional[str] = None
    context: Optional[str] = None

class ComparisonRow(BaseModel):
    metric: str
    value_a: str
    value_b: str
    winner: Optional[Literal["a", "b", "tie"]] = None

class FormattedOutput(BaseModel):
    output_type: Literal["chart", "report", "code", "table", "metrics", "comparison", "heatmap"]
    chart_type: Optional[Literal["bar", "line", "pie", "scatter", "funnel", "radar"]] = None
    chart_title: Optional[str] = None
    x_axis_label: Optional[str] = None
    y_axis_label: Optional[str] = None
    data_points: Optional[list[DataPoint]] = None
    radar_b_label: Optional[str] = None
    code_blocks: Optional[list[CodeBlock]] = None
    table_headers: Optional[list[str]] = None
    table_rows: Optional[list[list[str]]] = None
    metrics: Optional[list[MetricItem]] = None
    comparison_a_label: Optional[str] = None
    comparison_b_label: Optional[str] = None
    comparison_rows: Optional[list[ComparisonRow]] = None
    heatmap_title: Optional[str] = None
    heatmap_row_labels: Optional[list[str]] = None
    heatmap_col_labels: Optional[list[str]] = None
    heatmap_values: Optional[list[list[float]]] = None
    summary: str
    findings: list[str]
    recommendations: list[str]
    quality_score: Optional[int] = None
    quality_verdict: Optional[str] = None

# ── Progress callback ─────────────────────────────────────────────────────────
AnalysisProgressCallback = Callable[[str, str], None]  # agent_name, message

# ── Agent pipeline ────────────────────────────────────────────────────────────

class Bots:
    """
    Fitness AI multi-agent pipeline.

    Usage:
        bots = Bots(context="Compare my Strava and Garmin data")
        bots.create_agents()
        bots.create_tasks()
        result = bots.create_crew(data="path/to/file.csv")
        # result is a FormattedOutput JSON string
    """

    def __init__(self, context: str, progress_callback: AnalysisProgressCallback | None = None):
        if not context or not context.strip():
            context = "Provide a general fitness analysis of the available data."
        self.context = context
        self._ctx = context.replace("{", "{{").replace("}", "}}")
        self._fast_idx = 0
        self._smart_idx = 0
        self.file_read = FileReadTool()
        self._progress = progress_callback or (lambda name, msg: None)

    def _emit(self, agent: str, msg: str) -> None:
        _log(agent, msg)
        self._progress(agent, msg)

    def _smart_llm(self, temperature: float, max_tokens: int = 2048) -> LLM:
        model, self._smart_idx = _pick_model(_SMART_MODELS)
        return LLM(
            model=model,
            api_key=_api_key_for(model),
            max_tokens=max_tokens,
            max_retries=1,
            timeout=180,
            temperature=temperature,
        )

    def _fast_llm(self, temperature: float, max_tokens: int = 1024) -> LLM:
        model, self._fast_idx = _pick_model(_FAST_MODELS)
        return LLM(
            model=model,
            api_key=_api_key_for(model),
            max_tokens=max_tokens,
            max_retries=1,
            timeout=120,
            temperature=temperature,
        )

    def create_agents(self):
        """Create all 8 agents with buffed prompts (chain-of-thought, specificity)."""
        self._emit("Setup", "Creating agents...")

        self.context_agent = Agent(
            role="Fitness Analysis Directive Specialist",
            goal=(
                "Read the user's raw fitness context and rewrite it as a precise, unambiguous "
                "analysis directive. Identify: (1) core question type (performance, recovery, "
                "trend, comparison, anomaly), (2) relevant fitness metrics (heart rate, HRV, "
                "steps, sleep, calories, workout type, duration, pace, elevation), "
                "(3) data sources involved (Strava, Apple Health, Garmin, Fitbit, manual), "
                "(4) analysis type (trend, source comparison, anomaly detection, correlation), "
                "(5) constraints (date range, activity types, thresholds). "
                "Think step by step: first classify the question, then identify metrics/sources, "
                "then specify the analysis approach."
            ),
            backstory=(
                "You translate vague fitness requests into sharp, actionable instructions. "
                "You understand fitness terminology (HR zones, recovery, training load, "
                "periodization, lactate threshold, VO2max, acute:chronic ratio) "
                "but never perform analysis — you only clarify the directive. "
                "Your output is a crystal-clear brief for a senior data analyst."
            ),
            tools=[],
            verbose=True,
            memory=False,
            llm=self._fast_llm(0.2),
            allow_delegation=False,
            cache=False,
        )

        self.data_cleaner = Agent(
            role="Fitness Data Quality Inspector",
            goal=(
                "Read every file path provided and produce a concise data quality report. "
                "For each file: report type/size, column names, row count, missing value counts, "
                "and 2 sample rows. Then flag fitness-specific issues:\n"
                "- HR <30 or >250 bpm (physiologically impossible)\n"
                "- Steps >100k/day (mechanical error or non-walking data)\n"
                "- Sleep >16h or <2h (sensor error or nap misclassification)\n"
                "- HRV <10ms or >200ms (artifact-prone readings)\n"
                "- Calories >8000/day (unless ultra-endurance)\n"
                "- Missing workout types (unlabeled activity)\n"
                "- Timestamps in the future or >90 days in the past\n"
                "- Device field inconsistencies (same day, different source names)\n"
                "If no file provided: report 'No file provided — analysis uses context only.' "
                "Keep under 200 words. Be specific about which rows have issues."
            ),
            backstory=(
                "You are a meticulous data auditor with deep knowledge of fitness data ranges. "
                "You use FileReadTool once per file, then summarise its structure and "
                "flag any domain-anomalous values. You know that resting HR of 120 is suspicious, "
                "30k steps without a workout logged is worth noting, and "
                "a sudden HRV drop of 30%+ may indicate illness or overtraining. "
                "You never miss obvious data quality problems."
            ),
            tools=[self.file_read],
            verbose=True,
            memory=False,
            max_iter=5,
            llm=self._fast_llm(0.1, max_tokens=512),
            allow_delegation=False,
            cache=False,
        )

        self.prompt_engineer = Agent(
            role="Fitness Analysis Prompt Engineer",
            goal=(
                "Construct a precise, step-by-step analysis prompt for the data analyst. "
                "Your prompt MUST specify in order:\n"
                "1. Exact columns to load from each dataset (timestamp, heart_rate, "
                "workout_type, duration_min, distance_km, calories, device, etc.)\n"
                "2. Fitness-specific calculations with formulas:\n"
                "   - HR zone distribution: Zone 1 (<120bpm), Zone 2 (120-140), "
                "Zone 3 (140-160), Zone 4 (160-180), Zone 5 (>180)\n"
                "   - TRIMP (Training Impulse): duration_min × HR_zone_factor\n"
                "   - Acute:Chronic Workload Ratio: 7-day volume / 28-day avg volume\n"
                "   - Sleep efficiency: sleep_hours / time_in_bed × 100\n"
                "3. Patterns to detect with criteria:\n"
                "   - Progressive overload: volume increase >10% week-over-week\n"
                "   - Fatigue: declining HR in zone 4+ workouts over consecutive days\n"
                "   - Recovery: HRV increase >10% after rest days\n"
                "4. Comparisons to make: week-over-week, source-vs-source, "
                "workout-type comparisons with specific metrics\n"
                "5. What a complete answer includes: min/max/avg per metric, trends, flags"
            ),
            backstory=(
                "You write technical prompts for fitness data analysis pipelines. "
                "You reference specific metrics: TRIMP, CTL/ATL/TSB, HR zone percentages, "
                "acute:chronic workload ratio, sleep efficiency, calorie balance, "
                "EPOC, training monotony, and strain. "
                "Vague instructions produce vague results — you are never vague. "
                "Every step in your prompt is independently verifiable."
            ),
            tools=[],
            verbose=True,
            memory=False,
            llm=self._fast_llm(0.4),
            allow_delegation=False,
            cache=False,
        )

        self.data_analyst = Agent(
            role="Senior Fitness Data Analyst",
            goal=(
                "Execute the analysis prompt with precision. Rules:\n"
                "1. Call FileReadTool exactly ONCE per file path — do not re-read.\n"
                "2. Compute ALL requested metrics: weekly trends, HR zone time, "
                "training load, sleep-recovery correlations, multi-source comparisons.\n"
                "3. Never speculate beyond the data — if you lack data for a metric, say so.\n"
                "4. Back every finding with specific numbers (dates, values, deltas).\n"
                "5. If multiple data sources exist, analyze each separately then compare.\n"
                "6. Output: data source summary, key statistics per source, "
                "3-5 specific findings with numbers, 2-3 draft recommendations."
            ),
            backstory=(
                "You are a rigorous analyst specializing in fitness data. "
                "You call FileReadTool exactly once per file — no exceptions. "
                "You calculate zone minutes, estimate TRIMP from HR data, "
                "detect staleness patterns, and cross-reference sources for consistency. "
                "You back every finding with concrete numbers from the data. "
                "You can calculate: weekly volume totals (minutes, distance, TSS), "
                "average HR per zone, sleep consistency score, "
                "workout frequency trends, and multi-week performance trajectories."
            ),
            tools=[self.file_read],
            verbose=True,
            memory=False,
            max_iter=10,
            llm=self._smart_llm(0.1, max_tokens=3072),
            allow_delegation=False,
            cache=False,
        )

        self.sports_scientist = Agent(
            role="Exercise Physiology Specialist",
            goal=(
                "Interpret the analyst's findings through the lens of exercise science. "
                "For each finding, provide physiological context:\n"
                "1. Heart rate trends → cardiovascular adaptation: "
                "   - Decreasing resting HR → improved aerobic efficiency\n"
                "   - Faster HR recovery post-exercise → better fitness\n"
                "   - HR drift over long workouts → hydration/nutrition status\n"
                "2. Training load → appropriate vs excessive:\n"
                "   - Acute:chronic ratio <0.8 → detraining risk\n"
                "   - Acute:chronic ratio 0.8-1.3 → optimal training zone\n"
                "   - Acute:chronic ratio >1.3 → overreaching risk\n"
                "   - >1.5 → high injury risk\n"
                "3. Sleep & HRV → recovery status:\n"
                "   - HRV trending up → positive adaptation\n"
                "   - HRV dropping 3+ consecutive days → accumulated fatigue\n"
                "   - Sleep <6h or poor efficiency → compromised recovery\n"
                "4. Generate 2-3 specific, evidence-backed training recommendations "
                "based on actual data patterns seen."
            ),
            backstory=(
                "You are an exercise physiologist with expertise in: "
                "heart rate zone training (Zone 1-5, lactate threshold, aerobic/anaerobic), "
                "training load management (acute:chronic workload ratio, TRIMP, "
                "CTL/ATL/TSB, training monotony), "
                "recovery science (HRV trends, resting heart rate as fitness indicator, "
                "sleep stage impact on adaptation), "
                "overtraining detection (performance plateau, elevated resting HR, "
                "mood disturbances, increased injury rate, HRV suppression), "
                "periodization (macro/meso/micro cycles, deload weeks, peaking), "
                "and nutritional timing relative to training sessions. "
                "You translate raw numbers into physiological meaning and actionable coaching."
            ),
            tools=[],
            verbose=True,
            memory=False,
            max_iter=6,
            llm=self._smart_llm(0.15, max_tokens=2048),
            allow_delegation=False,
            cache=False,
        )

        self.trend_analyzer = Agent(
            role="Fitness Trend & Pattern Analyst",
            goal=(
                "Analyze the data for time-based fitness patterns. For each dimension, "
                "provide a specific finding with numbers:\n"
                "1. Weekly periodicity: calculate workout frequency per weekday. "
                "   Report consistency score (0-1) and the most/least active days.\n"
                "2. Progressive overload: compute week-over-week volume change %. "
                "   Flag if >15% increase (too fast) or >20% decrease (detraining).\n"
                "3. Fatigue accumulation: compare first vs last workout in each "
                "   block of 3+ consecutive training days. Report performance delta.\n"
                "4. Plateau detection: if any metric has <5% change over 2+ weeks, flag it.\n"
                "5. Rest day recovery: compare performance on day-after-rest vs "
                "   day-after-training. Quantify the delta as % improvement.\n"
                "6. Cross-metric correlations: report sleep vs next-day HR, "
                "   steps vs workout frequency, HRV vs resting HR."
            ),
            backstory=(
                "You are a pattern recognition specialist for time-series fitness data. "
                "You detect: weekly training consistency scores, "
                "month-over-month volume trends, "
                "consecutive workout fatigue curves, "
                "sleep-HR correlation coefficients, "
                "rest day impact deltas, "
                "and multi-week performance trajectories. "
                "You identify whether changes are meaningful trends (>10% consistent change) "
                "or random variation (<5% day-to-day). "
                "You never confuse correlation with causation."
            ),
            tools=[],
            verbose=True,
            memory=False,
            max_iter=6,
            llm=self._smart_llm(0.15, max_tokens=2048),
            allow_delegation=False,
            cache=False,
        )

        self.output_formatter = Agent(
            role="Fitness Output Format Specialist",
            goal=(
                "Convert all findings into a strict FormattedOutput JSON object. "
                "OUTPUT TYPE SELECTION (pick first that fits):\n"
                "1. 'code'       → answer includes runnable training plan, pace calc, HR zone calc\n"
                "2. 'metrics'    → 3-8 fitness KPIs (weekly volume, avg HR, recovery score, etc.)\n"
                "3. 'comparison' → two sources, periods, or activity types compared side-by-side\n"
                "4. 'heatmap'    → matrix data like time-of-day × day-of-week activity grid, "
                "                    HR zone × activity type. Max 10×10.\n"
                "5. 'table'      → workout log summaries, weekly plan vs actual. Max 20 rows.\n"
                "6. 'chart'      → 2+ numeric values for visual comparison. Choose:\n"
                "   bar → calories by type, weekly volume by source\n"
                "   line → HR trends, weight, weekly totals over time\n"
                "   pie → workout type distribution, HR zone splits (2-6 slices)\n"
                "   scatter → HR vs pace, sleep vs performance\n"
                "   radar → multi-attribute fitness profile (endurance, strength, recovery)\n"
                "   funnel → program adherence, workout completion rates\n"
                "7. 'report'     → qualitative narrative fitness assessment (fallback)\n\n"
                "ALWAYS include: summary (2-3 sentences answering original question), "
                "findings (3-5 specific strings with numbers), "
                "recommendations (2-3 actionable strings).\n"
                "ALWAYS include source attribution: mention which device/platform "
                "each finding relates to when data comes from multiple sources.\n"
                "Return ONLY raw JSON. No markdown fences, no commentary, no preamble."
            ),
            backstory=(
                "You serialise fitness analysis results into one of 7 output modes. "
                "You never output markdown fences around JSON — just the raw object. "
                "You always ensure summary, findings, and recommendations are populated. "
                "You prefer structured outputs (metrics, chart, comparison) over narrative report "
                "because structured data drives the UI."
            ),
            tools=[],
            verbose=True,
            memory=False,
            llm=self._smart_llm(0.1, max_tokens=2048),
            allow_delegation=False,
            cache=False,
        )

        self.qa_critic = Agent(
            role="Fitness Analysis Quality Critic",
            goal=(
                "Rate the completed fitness analysis 1-10. Evaluate:\n"
                "1. Domain accuracy (1-10): Are HR zone interpretations physiologically correct? "
                "Is training load assessment valid? Are recovery recommendations sound?\n"
                "2. Specificity (1-10): Are recommendations actionable and specific "
                "(e.g. 'add one Zone 2 session of 45 min' vs 'do more cardio')?\n"
                "3. Evidence quality (1-10): Are claims backed by actual numbers from the data?\n"
                "4. Multi-source awareness (1-10): Are device discrepancies noted? "
                "Is data properly attributed?\n"
                "5. Completeness (1-10): Does it answer the original question? "
                "Is anything important missing?\n\n"
                "Output ONLY: {\"score\": <int 1-10>, \"verdict\": \"<1-2 sentences>\"}\n"
                "Score 10 = complete, accurate, actionable coaching insight. "
                "Score <5 = vague, missing evidence, or physiological errors. "
                "Flag made-up numbers and unrealistic recommendations."
            ),
            backstory=(
                "You review fitness analyses for quality. Score 10 = a complete, "
                "domain-accurate analysis with actionable coaching insights. "
                "Score <5 = vague, missing data backing, or fitness terminology errors. "
                "You flag made-up numbers, unrealistic recommendations, "
                "and failures to account for multi-source data. "
                "You are strict — a 7/10 is good. Output ONLY the raw JSON."
            ),
            tools=[],
            verbose=True,
            memory=False,
            llm=self._fast_llm(0.2, max_tokens=512),
            allow_delegation=False,
            cache=False,
        )

    def create_tasks(self):
        """Create all 8 tasks with rich context chaining."""
        self._emit("Setup", "Creating tasks...")

        self.interpret_task = Task(
            description=(
                f"The user wants: {self._ctx}\n\n"
                "Rewrite this into a structured fitness analysis directive:\n"
                "1. The single core question to answer "
                "(choose: performance, recovery, trend, comparison, anomaly detection, summary)\n"
                "2. Relevant fitness metrics "
                "(HR, HRV, steps, sleep, calories, workout type, duration, pace, elevation)\n"
                "3. Data sources involved "
                "(Strava, Apple Health, Garmin, Fitbit, Google Fit, manual, unknown)\n"
                "4. Analysis type "
                "(trend over time, source comparison, anomaly detection, summary stats, correlation)\n"
                "5. Any constraints "
                "(date range, specific activity types, threshold values, exclude rest days)\n\n"
                "Write 3-5 plain sentences addressed directly to a data analyst. "
                "No headers, no bullets — just clear instructions."
            ),
            expected_output=(
                "3-5 plain sentences. Direct instruction specifying: "
                "the question, relevant metrics, sources, analysis type, constraints. "
                "No formatting, no headers."
            ),
            agent=self.context_agent,
        )

        self.clean_task = Task(
            description=(
                "Dataset path(s):\n{data}\n\n"
                "If {data} is not '(no file)', use FileReadTool to read each path once.\n"
                "For each file report: type/size, column names, row count, missing values per column, "
                "obvious issues, 2 sample records.\n"
                "FLAG these fitness anomalies with row numbers if possible:\n"
                "- HR <30 or >250 bpm\n"
                "- Steps >100k in a day\n"
                "- Sleep duration >16h or <2h\n"
                "- HRV <10ms or >200ms\n"
                "- Calories >8000 in a day\n"
                "- Missing workout_type field (unlabeled activity)\n"
                "- Timestamps in the future or >90 days old\n"
                "- Inconsistent device names (same device, different spelling)\n"
                "If no file: report 'No file provided — analysis uses context only.'\n"
                "Keep under 200 words."
            ),
            expected_output="Concise data quality report under 200 words. Plain prose or bullets. No JSON.",
            context=[self.interpret_task],
            agent=self.data_cleaner,
        )

        self.prompt_task = Task(
            description=(
                f"Original request: {self._ctx}\n\n"
                "Using the directive and data quality report, write a step-by-step fitness analysis prompt:\n"
                "1. Exact columns to load from each dataset\n"
                "2. Fitness-specific calculations with numeric thresholds:\n"
                "   - Avg HR, max HR, min HR per workout type\n"
                "   - HR zone distribution (Zone 1: <120, Zone 2: 120-140, Zone 3: 140-160, "
                "Zone 4: 160-180, Zone 5: >180 bpm)\n"
                "   - Weekly volume (total minutes, total distance, total calories)\n"
                "   - Sleep-HR correlation (compare night's sleep vs next day resting HR)\n"
                "   - TRIMP estimate = duration_min × zone_factor\n"
                "3. Patterns to detect with criteria:\n"
                "   - Progressive overload: week-over-week volume change %\n"
                "   - Recovery impact: rest day vs training day performance delta\n"
                "   - Multi-source alignment: same-metric comparison across devices\n"
                "4. Comparisons to make: week-over-week, source-vs-source, activity-type comparison\n"
                "5. What a complete answer includes: min/max/avg per metric, direction flags"
            ),
            expected_output=(
                "Numbered step-by-step prompt. Each step specific and actionable. "
                "References exact column names and fitness metrics with thresholds. "
                "Specifies which calculations and comparisons to run."
            ),
            context=[self.interpret_task, self.clean_task],
            agent=self.prompt_engineer,
        )

        self.analyze_task = Task(
            description=(
                "Dataset path(s):\n{data}\n\n"
                "If file paths are provided (one per line), read each with FileReadTool exactly once. "
                "If multiple files, analyze them together and cross-reference. "
                "If no file, answer from the analysis prompt using reasoning.\n\n"
                "Follow EVERY step in the prompt below. Read each file only once. "
                "Compute ALL requested metrics. Report specific numbers with dates. "
                "If multiple sources, compare them.\n\n"
                "Your output MUST contain:\n"
                "1. Data source summary with device breakdown\n"
                "2. Key fitness statistics (weekly trends, averages, ranges, outliers per source)\n"
                "3. 3-5 concrete findings that answer the prompt (with exact numbers)\n"
                "4. 2-3 initial recommendations for the sports scientist to refine"
            ),
            expected_output=(
                "Thorough fitness analysis containing: data source summary, "
                "key statistics with numbers, 3-5 specific findings, 2-3 draft recommendations. "
                "Every finding backed by concrete values."
            ),
            context=[self.prompt_task],
            agent=self.data_analyst,
        )

        self.sport_task = Task(
            description=(
                f"Original request: {self._ctx}\n\n"
                "Review the data analyst's findings through an exercise science lens.\n"
                "Provide expert interpretation:\n\n"
                "1. CARDIOVASCULAR FITNESS ASSESSMENT:\n"
                "   - What do HR trends indicate? (improving aerobic base, lactate threshold shift, "
                "recovery efficiency)\n"
                "   - Is resting HR trending down (positive adaptation) or up (fatigue)?\n"
                "   - Are HR recovery rates improving?\n\n"
                "2. TRAINING LOAD ANALYSIS:\n"
                "   - Acute:chronic workload ratio estimate (compare recent 7d vs prior 28d)\n"
                "   - <0.8 → detraining, 0.8-1.3 → optimal, 1.3-1.5 → high, >1.5 → injury risk\n"
                "   - Is volume progression sustainable?\n\n"
                "3. RECOVERY STATUS:\n"
                "   - HRV trend: up (good), stable (ok), down 3+ days (concerning)\n"
                "   - Sleep quality: duration, consistency, efficiency\n"
                "   - Rest day sufficiency: are rest days producing measurable recovery?\n\n"
                "4. MULTI-SOURCE DATA NOTE:\n"
                "   - Are discrepancies significant enough to affect interpretation?\n"
                "   - Is one source more reliable?\n\n"
                "5. Generate 2-3 specific, science-backed training recommendations "
                "based on the actual data patterns seen."
            ),
            expected_output=(
                "Exercise science interpretation with: cardiovascular assessment, "
                "training load analysis with acute:chronic context, recovery status, "
                "multi-source data quality note, 2-3 specific evidence-based recommendations."
            ),
            context=[self.analyze_task],
            agent=self.sports_scientist,
        )

        self.trend_task = Task(
            description=(
                f"Original request: {self._ctx}\n\n"
                "Analyze the full analysis for time-based fitness patterns. "
                "For each dimension, provide a finding with specific numbers:\n\n"
                "1. WEEKLY PERIODICITY:\n"
                "   - Workout frequency per day of week\n"
                "   - Consistency score (0-1): how evenly distributed?\n"
                "   - Pattern: e.g. 'hard Mon/Wed/Fri, easy Tue/Thu'\n\n"
                "2. PROGRESSIVE OVERLOAD:\n"
                "   - Week-over-week volume change % (duration, distance, or frequency)\n"
                "   - Is volume increasing >10%/week? (possibly too fast)\n"
                "   - Is volume flat or decreasing? (possible plateau or detraining)\n\n"
                "3. FATIGUE ACCUMULATION:\n"
                "   - Compare performance (HR, pace, duration) across consecutive training days\n"
                "   - Is there a declining trend across the week?\n\n"
                "4. PLATEAU DETECTION:\n"
                "   - Any metric with <5% change over 2+ weeks? Flag it.\n\n"
                "5. RECOVERY IMPACT (REST DAYS):\n"
                "   - Performance on day-after-rest vs day-after-training\n"
                "   - Quantify the delta as % improvement\n\n"
                "6. CROSS-METRIC CORRELATIONS:\n"
                "   - Sleep duration vs next-day resting HR\n"
                "   - Steps vs workout frequency\n"
                "   - HRV vs resting HR\n"
                "   - Note: correlation ≠ causation"
            ),
            expected_output=(
                "Pattern analysis containing: weekly consistency score, volume trend direction "
                "with % change, fatigue assessment with examples, plateau flags, "
                "recovery impact quantified, cross-metric correlations if supported."
            ),
            context=[self.analyze_task, self.sport_task],
            agent=self.trend_analyzer,
        )

        self.format_task = Task(
            description=(
                f"Original request: {self._ctx}\n\n"
                "Convert all findings (data analyst + sports scientist + trend analyst) "
                "into a FormattedOutput JSON object.\n\n"
                "OUTPUT TYPE PRIORITY (pick first that fits the answer):\n"
                "1. 'code'       → training plan, pace calc, HR zone calc\n"
                "2. 'metrics'    → 3-8 fitness KPIs with labels + values + optional trend\n"
                "3. 'comparison' → two entities: set comparison_a_label, comparison_b_label, "
                "                   comparison_rows (metric, value_a, value_b, winner)\n"
                "4. 'heatmap'    → matrix: heatmap_row_labels, heatmap_col_labels, "
                "                   heatmap_values (2D float array), heatmap_title. Max 10x10.\n"
                "5. 'table'      → table_headers + table_rows. Max 20 rows.\n"
                "6. 'chart'      → data_points + chart_type + chart_title. "
                "                   bar/line/pie/scatter/funnel/radar\n"
                "7. 'report'     → qualitative narrative (fallback)\n\n"
                "ALWAYS include:\n"
                "- summary: 2-3 sentences directly answering the original question\n"
                "- findings: 3-5 specific strings with numbers and source attribution\n"
                "- recommendations: 2-3 actionable coaching strings\n"
                "- Set unused fields to None (JSON null)\n"
                "- Source attribution: mention device/platform per finding\n\n"
                "Return ONLY the raw JSON object. No markdown, no fences, no commentary."
            ),
            expected_output=(
                "Single raw JSON object matching FormattedOutput. "
                "No markdown fences. Parseable by json.loads() without modification."
            ),
            context=[self.analyze_task, self.sport_task, self.trend_task],
            agent=self.output_formatter,
            output_pydantic=FormattedOutput,
        )

        self.qa_task = Task(
            description=(
                f"Original request: {self._ctx}\n\n"
                "Review the completed fitness analysis. Score 1-10 on five dimensions:\n\n"
                "1. Domain accuracy (1-10): "
                "Are HR zone interpretations correct? Training load assessment valid? "
                "Recovery recommendations physiologically sound?\n\n"
                "2. Specificity (1-10): "
                "Are recommendations specific, measurable, time-bound "
                "(e.g. 'add 30 min Zone 2 on Thursday' vs 'do more cardio')?\n\n"
                "3. Evidence quality (1-10): "
                "Are findings backed by concrete numbers from the data? "
                "Any made-up or uncited numbers?\n\n"
                "4. Multi-source awareness (1-10): "
                "Are device discrepancies noted? Is data properly attributed? "
                "Are conflated sources flagged?\n\n"
                "5. Completeness (1-10): "
                "Does it answer the original question? Anything important missing?\n\n"
                "Return ONLY: "
                '{"score": <int 1-10>, "verdict": "<1-2 sentences: what was done well and what gap remains>"}'
            ),
            expected_output='Raw JSON only: {"score": <int>, "verdict": "<string>"}. No markdown.',
            context=[self.analyze_task, self.sport_task, self.trend_task, self.format_task],
            agent=self.qa_critic,
        )

    # ── Input validation ───────────────────────────────────────────────────

    def _validate_input(self, data: str) -> str:
        """Validate and normalize the data input."""
        if data is None:
            return "(no file)"
        data = str(data).strip()
        if not data:
            return "(no file)"
        parts = [p.strip() for p in data.split("\n") if p.strip()]
        valid_parts = []
        for p in parts:
            p = p.strip().strip('"').strip("'")
            if p == "(no file)":
                continue
            if os.path.isfile(p):
                valid_parts.append(p)
            else:
                self._emit("Validator", f"Path not found, skipping: {p}")
        if not valid_parts:
            return "(no file)"
        return "\n".join(valid_parts)

    # ── Pipeline execution ─────────────────────────────────────────────────

    def create_crew(self, data) -> str:
        """Run the full pipeline with model rotation, retries, and graceful fallback."""
        with _crew_lock:
            return self._run_pipeline(data)

    def _build_fallback_output(self, error: Exception | None = None) -> str:
        """Build a minimum viable FormattedOutput when the pipeline fails entirely."""
        fallback = FormattedOutput(
            output_type="report",
            summary="The fitness analysis pipeline encountered an error. A partial report is provided.",
            findings=[
                f"Pipeline error: {str(error)[:200] if error else 'Unknown error'}",
                "Not all agents completed their analysis.",
            ],
            recommendations=[
                "Check API keys and provider status.",
                "Ensure data files are accessible and well-formatted.",
                "Try with a simpler context or smaller date range.",
            ],
            quality_score=1,
            quality_verdict="Pipeline failed to complete. Score reflects incomplete execution.",
        )
        return fallback.model_dump_json()

    def _run_pipeline(self, raw_data) -> str:
        data = self._validate_input(raw_data)
        max_attempts = (len(_FAST_MODELS) + len(_SMART_MODELS)) * 2

        for attempt in range(max_attempts):
            _wait_until_available()
            fast_model, self._fast_idx = _pick_model(_FAST_MODELS)
            smart_model, self._smart_idx = _pick_model(_SMART_MODELS)

            self._emit("Scheduler", f"Attempt {attempt+1}/{max_attempts} — fast={fast_model} smart={smart_model}")

            self.create_agents()
            self.create_tasks()

            crew = Crew(
                agents=[
                    self.context_agent, self.data_cleaner, self.prompt_engineer,
                    self.data_analyst, self.sports_scientist, self.trend_analyzer,
                    self.output_formatter, self.qa_critic,
                ],
                tasks=[
                    self.interpret_task, self.clean_task, self.prompt_task,
                    self.analyze_task, self.sport_task, self.trend_task,
                    self.format_task, self.qa_task,
                ],
                process=Process.sequential,
                verbose=True,
                memory=False,
            )

            try:
                result = crew.kickoff(inputs={"data": data})
                _record_model_outcome(fast_model, True)
                _record_model_outcome(smart_model, True)
            except Exception as e:
                err_str = str(e)
                is_404 = "404" in err_str
                is_rate_limit = any(x in err_str for x in ("429", "RateLimitError", "rate_limit_exceeded"))
                is_bad_request = any(x in err_str for x in ("BadRequestError", "invalid_request_error"))
                is_server_err = any(c in err_str for c in ("402", "401", "503", "529", "500"))
                is_rotatable = is_404 or is_rate_limit or is_bad_request or is_server_err

                _record_model_outcome(fast_model, False)
                _record_model_outcome(smart_model, False)

                if is_rotatable and attempt < max_attempts - 1:
                    if is_rate_limit:
                        cooldown_s = _parse_retry_after(err_str)
                        _set_cooldown(fast_model, cooldown_s)
                        _set_cooldown(smart_model, cooldown_s)
                        self._emit("Scheduler", f"RATE-LIMIT fast={fast_model} → {cooldown_s:.0f}s cooldown")
                    elif is_404 or is_bad_request:
                        _set_cooldown(fast_model, 600)
                        _set_cooldown(smart_model, 600)
                        self._emit("Scheduler", f"UNAVAILABLE fast={fast_model} → 10min cooldown")
                    else:
                        cooldown_s = _exponential_backoff(attempt)
                        _set_cooldown(fast_model, cooldown_s)
                        _set_cooldown(smart_model, cooldown_s)
                        self._emit("Scheduler", f"SERVER-ERR → {cooldown_s:.0f}s exponential backoff")
                    continue

                self._emit("Pipeline", f"Fatal error after {attempt+1} attempts: {err_str[:200]}")
                return self._build_fallback_output(e)

            # ── Extract formatted output ──────────────────────────────────
            formatted = self._extract_formatted_output(crew, result)

            if formatted:
                qa_raw = result.raw if hasattr(result, "raw") else str(result)
                try:
                    qa = _json.loads(_extract_json(qa_raw))
                    formatted.quality_score = int(qa.get("score", 0)) or None
                    formatted.quality_verdict = qa.get("verdict")
                except Exception:
                    pass
                return formatted.model_dump_json()

            # Fallback: extract from raw result
            self._emit("Pipeline", "No structured output — extracting from raw result")
            raw = result.raw if hasattr(result, "raw") else str(result)
            extracted = _extract_json(raw)
            try:
                parsed = _json.loads(extracted)
                if isinstance(parsed, dict) and "output_type" in parsed:
                    return extracted
            except Exception:
                pass
            return self._build_fallback_output()

        return self._build_fallback_output(Exception("Max attempts exceeded"))

    def _extract_formatted_output(self, crew: Crew, result) -> FormattedOutput | None:
        """Try to extract FormattedOutput from crew tasks or result, with graceful fallback."""
        fmt_task_out = crew.tasks[6].output if len(crew.tasks) > 6 else None
        if fmt_task_out:
            if getattr(fmt_task_out, "pydantic", None):
                return fmt_task_out.pydantic
            try:
                raw = fmt_task_out.raw or ""
                parsed = _json.loads(_extract_json(raw))
                return FormattedOutput(**parsed)
            except (ValidationError, Exception) as e:
                self._emit("Formatter", f"Pydantic validation failed: {str(e)[:100]} — attempting repair")

                # Attempt repair: try to coerce partial output into valid FormattedOutput
                try:
                    raw = fmt_task_out.raw or ""
                    parsed = _json.loads(_extract_json(raw))
                    if isinstance(parsed, dict):
                        if "output_type" not in parsed:
                            parsed["output_type"] = "report"
                        if "summary" not in parsed:
                            parsed["summary"] = "Analysis completed (summary not generated)."
                        if "findings" not in parsed or not isinstance(parsed["findings"], list):
                            parsed["findings"] = [str(parsed.get("summary", "See raw output."))]
                        if "recommendations" not in parsed or not isinstance(parsed["recommendations"], list):
                            parsed["recommendations"] = ["Review the analysis findings for recommendations."]
                        return FormattedOutput(**parsed)
                except Exception:
                    pass

        return None
