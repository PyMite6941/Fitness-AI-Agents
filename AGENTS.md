# Fitness AI Agents — Architecture

## Pipeline Overview

Eight agents run sequentially. Each passes its output to the next via `context=[]`.

```
context_agent → data_cleaner → prompt_engineer → data_analyst → sports_scientist → trend_analyzer → output_formatter → qa_critic
```

---

## Agents

### 1. context_agent — Fitness Analysis Directive Specialist
- **LLM:** fast, temperature 0.2
- **Job:** Translates raw user context into a precise fitness analysis directive — identifies core question, relevant metrics (HR, steps, sleep, HRV), data sources, and analysis type
- **Tools:** none
- **Memory:** off

### 2. data_cleaner — Fitness Data Quality Inspector
- **LLM:** fast, temperature 0.1
- **Job:** Reads all files, reports column names, row counts, missing values. Flags fitness-specific anomalies: unrealistic HR (<30 or >250 bpm), steps >100k/day, sleep >16h, timestamp gaps, device inconsistencies
- **Tools:** FileReadTool
- **Memory:** off

### 3. prompt_engineer — Fitness Analysis Prompt Engineer
- **LLM:** fast, temperature 0.4
- **Job:** Writes a step-by-step fitness analysis prompt specifying exact columns, fitness calculations (HR zone distribution, training load, recovery ratios), patterns to detect (progressive overload, fatigue, periodicity), and comparisons to make
- **Tools:** none
- **Memory:** off

### 4. data_analyst — Senior Fitness Data Analyst
- **LLM:** smart, temperature 0.1
- **Job:** Executes the analysis prompt. Computes weekly trends, HR zone time, multi-source comparisons, sleep-HR correlations. Calls FileReadTool once per file
- **Tools:** FileReadTool
- **Memory:** on

### 5. sports_scientist — Exercise Physiology Specialist
- **LLM:** smart, temperature 0.15
- **Job:** Interprets findings through exercise science. Assesses cardiovascular fitness from HR trends, evaluates training load (acute:chronic ratio, TRIMP), checks recovery status (HRV, sleep), detects overtraining risk. Generates 2-3 evidence-based training recommendations
- **Tools:** none
- **Memory:** off

### 6. trend_analyzer — Fitness Trend & Pattern Analyst
- **LLM:** smart, temperature 0.15
- **Job:** Detects time-based patterns: weekly periodicity, progressive overload rate, fatigue accumulation curves, plateau detection, rest-day recovery deltas, cross-metric correlations (sleep vs HR, steps vs workout frequency)
- **Tools:** none
- **Memory:** off

### 7. output_formatter — Fitness Output Format Specialist
- **LLM:** smart, temperature 0.1
- **Job:** Serialises all findings into FormattedOutput JSON. Prefers fitness-appropriate output types: metrics for KPIs, chart for trends, comparison for source/period contrasts, table for workout logs, heatmap for zone×time grids. Includes source attribution in findings
- **Tools:** none
- **Memory:** off

### 8. qa_critic — Fitness Analysis Quality Critic
- **LLM:** fast, temperature 0.2
- **Job:** Scores the analysis 1-10 on domain accuracy, specificity, evidence quality, multi-source awareness, and completeness. Flags physiological errors, made-up numbers, and vague recommendations
- **Tools:** none
- **Memory:** off

---

## File Type → Tool Mapping

| Extension | Tool |
|---|---|
| `.csv` | CSVSearchTool |
| `.json` | JSONSearchTool |
| `.pdf` | PDFSearchTool |
| `.xml` | XMLSearchTool |
| `.txt` | TXTSearchTool |
| anything else | FileReadTool |

---

## Output Format Logic

| Original request implies... | Output format |
|---|---|
| API, frontend, structured data | Valid JSON |
| Export, spreadsheet, tabular | Valid CSV |
| Unrecognised or unspecified | Plain-text report |

---

## Output Schema (Pydantic)

```python
class FormattedOutput(BaseModel):
    output_type: Literal["chart", "report", "code", "table", "metrics", "comparison", "heatmap"]
    chart_type: Optional[Literal["bar", "line", "pie", "scatter", "funnel", "radar"]]
    chart_title: Optional[str]
    data_points: Optional[list[DataPoint]]
    metrics: Optional[list[MetricItem]]
    comparison_rows: Optional[list[ComparisonRow]]
    heatmap_values: Optional[list[list[float]]]
    table_headers: Optional[list[str]]
    table_rows: Optional[list[list[str]]]
    code_blocks: Optional[list[CodeBlock]]
    summary: str
    findings: list[str]
    recommendations: list[str]
    quality_score: Optional[int]
    quality_verdict: Optional[str]
```

Full schema in `bots.py`. Used on `format_task` via `output_pydantic=FormattedOutput`.

---

## LLMs

| Config | Model | Used by |
|---|---|---|
| fast | `openrouter/groq/groq-1:free` or Groq/OpenRouter free pool | context_agent, data_cleaner, prompt_engineer, qa_critic |
| smart | `openrouter/deepseek/deepseek-r1:free` or Groq/OpenRouter large pool | data_analyst, sports_scientist, trend_analyzer, output_formatter |

Model rotation with cooldown tracking handles rate limits. See `_FAST_MODELS` / `_SMART_MODELS` in `bots.py`.

---

## Memory & Embeddings

- `memory=False` on all agents (stateless pipeline)
- Embedder: `fastembed` with `BAAI/bge-small-en-v1.5` (local, no API key required) — unused currently

---

## Usage

```python
from bots import Bots
import os

bots = Bots(context="Compare my Strava and Garmin heart rate data for running")
bots.create_agents()
bots.create_tasks()

data_path = os.path.join(os.path.dirname(__file__), "backend/test_data.csv")
result = bots.create_crew(data=data_path)
# result is a FormattedOutput JSON string
```
