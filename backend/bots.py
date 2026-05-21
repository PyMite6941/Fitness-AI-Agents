from crewai import Agent, Crew, Task, Process, LLM
from crewai_tools import FileReadTool, CSVSearchTool, JSONSearchTool, PDFSearchTool, XMLSearchTool, TXTSearchTool
import os
from pydantic import BaseModel


class Report(BaseModel):
    summary: str
    key_findings: list[str]
    anomalies: list[str]
    recommendations: list[str]


class Bots:
    def __init__(self, context: str):
        self.context = context
        self._smart_config = dict(
            model="openrouter/deepseek/deepseek-r1:free",
            api_key=os.getenv("OPENROUTER_API_KEY"),
            max_tokens=4096,
            max_retries=2,
            timeout=30,
        )
        self._fast_config = dict(
            model="openrouter/groq/groq-1:free",
            api_key=os.getenv("OPENROUTER_API_KEY"),
            max_tokens=4096,
            max_retries=2,
            timeout=30,
        )
        self.file_read = FileReadTool()
        self.csv_search = CSVSearchTool()
        self.json_search = JSONSearchTool()
        self.pdf_search = PDFSearchTool()
        self.xml_search = XMLSearchTool()
        self.txt_search = TXTSearchTool()

    def _smart_llm(self, temperature: float) -> LLM:
        return LLM(**self._smart_config, temperature=temperature)

    def _fast_llm(self, temperature: float) -> LLM:
        return LLM(**self._fast_config, temperature=temperature)

    def create_agents(self):
        self.context_agent = Agent(
            role="Analysis Directive Specialist",
            goal=(
                "Read the user's raw context and rewrite it as a precise, unambiguous "
                "analysis directive. Identify the core question, the most relevant columns "
                "or metrics, and the exact type of analysis needed (trend, comparison, "
                "anomaly, summary, correlation). Output only the directive — nothing else."
            ),
            backstory=(
                "You are an expert at translating vague or freeform requests into sharp, "
                "actionable instructions for data analysts. You have a talent for identifying "
                "what someone actually wants to know versus what they literally said. "
                "You never perform analysis yourself — your only job is to make the analyst's "
                "directive so clear that there is no room for misinterpretation."
            ),
            tools=[],
            verbose=True,
            memory=False,
            llm=self._fast_llm(0.2),
            allow_delegation=False,
            cache=True,
        )

        self.prompt_engineer = Agent(
            role="Data Analysis Prompt Engineer",
            goal=(
                "Take the raw FastAPI input and the analysis directive, then construct a "
                "precise, step-by-step analysis prompt for the data analyst. The prompt must "
                "specify exactly which columns to examine, what calculations to run, what "
                "patterns to look for, and in what order to approach the analysis."
            ),
            backstory=(
                "You are a specialist in writing technical prompts for data analysis pipelines. "
                "You understand how LLM-based analysts think and know that vague instructions "
                "produce vague results. You break every analysis job into clear, ordered steps "
                "with explicit column names, metric names, and success criteria. You never "
                "perform the analysis yourself — you only write the instruction that makes it happen."
            ),
            tools=[],
            verbose=True,
            memory=False,
            llm=self._fast_llm(0.4),
            allow_delegation=False,
            cache=True,
        )

        self.data_analyst = Agent(
            role="Senior Data Analyst",
            goal=(
                "Follow the analysis prompt exactly. Use the correct tool for the file type "
                "provided, then extract only the findings that directly answer the prompt. "
                "Never speculate beyond what the data shows."
            ),
            backstory=(
                "You are a rigorous data analyst with experience across many domains and file formats. "
                "You always select the right tool for the file type: CSVSearchTool for .csv, "
                "JSONSearchTool for .json, PDFSearchTool for .pdf, XMLSearchTool for .xml, "
                "TXTSearchTool for .txt, and FileReadTool for any other file type. "
                "You back every finding with statistics and never go beyond the scope of your prompt."
            ),
            tools=[
                self.file_read,
                self.csv_search,
                self.json_search,
                self.pdf_search,
                self.xml_search,
                self.txt_search,
            ],
            verbose=True,
            memory=True,
            llm=self._smart_llm(0.1),
            max_rpm=15,
            allow_delegation=False,
            cache=True,
        )

        self.output_formatter = Agent(
            role="Output Format Specialist",
            goal=(
                "Look at the original user request and the analyst's findings, then produce "
                "the output in the exact format the request implies — JSON for structured/API "
                "consumers, CSV for tabular/export use cases, or plain text for human-readable "
                "reports. Never change the content — only change the structure and format."
            ),
            backstory=(
                "You are an expert in data serialization and output formatting. You read the "
                "original request to determine what will consume the output — an API, a spreadsheet, "
                "or a human — and format accordingly. You treat JSON and CSV as absolute output "
                "types: if the request implies a downstream system, you output valid JSON or CSV "
                "with no extra commentary. If the request implies a human reader, you produce a "
                "clean, structured plain-text report. For any format you do not recognise, you "
                "default to plain text."
            ),
            tools=[],
            verbose=True,
            memory=False,
            llm=self._smart_llm(0.1),
            allow_delegation=False,
            cache=True,
        )

    def create_tasks(self):
        self.interpret_task = Task(
            description=(
                f"The user has provided this context about what they want analyzed:\n\n"
                f"CONTEXT: {self.context}\n\n"
                "Rewrite this into a structured analysis directive by answering these four questions:\n"
                "1. What is the single core question to answer?\n"
                "2. Which columns or metrics are most relevant to that question?\n"
                "3. What analysis type is needed — trend over time, comparison between groups, "
                "anomaly detection, statistical summary, or correlation?\n"
                "4. Are there any constraints or focus areas implied by the context "
                "(e.g. a date range, a specific user, a threshold)?\n\n"
                "Write the final directive as 3-5 plain sentences addressed directly to a data analyst."
            ),
            expected_output=(
                "A single block of 3-5 plain sentences. No headers, no bullet points, no preamble. "
                "Written as a direct instruction to a data analyst. Must specify: the question to answer, "
                "the relevant columns, the analysis type, and any constraints."
            ),
            agent=self.context_agent,
        )

        self.prompt_task = Task(
            description=(
                f"You have received the original user request and an analysis directive.\n\n"
                f"ORIGINAL REQUEST: {self.context}\n\n"
                "Using the directive from the previous step, write a precise step-by-step "
                "analysis prompt for the data analyst. Your prompt must include:\n"
                "1. The exact columns to load and examine\n"
                "2. The specific calculations or aggregations to run\n"
                "3. What patterns, outliers, or trends to look for\n"
                "4. The order in which to approach the analysis\n"
                "5. What a complete, correct answer looks like"
            ),
            expected_output=(
                "A numbered step-by-step prompt addressed to a data analyst. "
                "Each step must be specific and actionable — no vague instructions. "
                "Must reference exact column names or metric types where possible."
            ),
            context=[self.interpret_task],
            agent=self.prompt_engineer,
        )

        self.analyze_task = Task(
            description=(
                "You have been given a dataset at path {data} and a step-by-step analysis prompt.\n\n"
                "First, check the file extension of {data} and select the correct tool:\n"
                "- .csv  → CSVSearchTool\n"
                "- .json → JSONSearchTool\n"
                "- .pdf  → PDFSearchTool\n"
                "- .xml  → XMLSearchTool\n"
                "- .txt  → TXTSearchTool\n"
                "- anything else → FileReadTool\n\n"
                "Then follow every step in the prompt from the previous task exactly. "
                "Report only what the data shows."
            ),
            expected_output=(
                "A structured data analysis report containing:\n"
                "1. Data quality summary (rows found, nulls or missing values noted)\n"
                "2. Key statistics relevant to the prompt (averages, ranges, outliers)\n"
                "3. 3-5 findings that directly answer the prompt\n"
                "4. 2-3 actionable recommendations based solely on the data"
            ),
            context=[self.prompt_task],
            agent=self.data_analyst,
            output_pydantic=Report,
        )

        self.format_task = Task(
            description=(
                f"You have the analyst's findings and the original user request below.\n\n"
                f"ORIGINAL REQUEST: {self.context}\n\n"
                "Determine the correct output format from the request:\n"
                "- If the request mentions an API, frontend, or structured data → output valid JSON\n"
                "- If the request mentions export, spreadsheet, or tabular data → output valid CSV\n"
                "- If the format is unrecognised or unspecified → output a clean plain-text report\n\n"
                "Do not change any findings — only reformat them."
            ),
            expected_output=(
                "The analyst's findings reformatted as either valid JSON, valid CSV, or a clean "
                "plain-text report — determined entirely by what the original request implies. "
                "No commentary, no preamble, just the formatted output."
            ),
            context=[self.analyze_task],
            agent=self.output_formatter,
            output_file="output.log",
        )

    def create_crew(self, data):
        self.crew = Crew(
            agents=[self.context_agent, self.prompt_engineer, self.data_analyst, self.output_formatter],
            tasks=[self.interpret_task, self.prompt_task, self.analyze_task, self.format_task],
            process=Process.sequential,
            verbose=True,
            memory=True,
            embedder={
                "provider": "fastembed",
                "config": {
                    "model": "BAAI/bge-small-en-v1.5",
                }
            },
        )
        self.crew.kickoff(inputs={"data": data})
