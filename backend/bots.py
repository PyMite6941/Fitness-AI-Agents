from crewai import Agent, Crew, Task, Process, LLM
from crewai.tools import tool
import os
import pandas as pd

class Bots:
    def __init__(self,context:str):
        self.context = context
        self.smart_llm = LLM(
            model="openrouter/deepseek/deepseek-r1:free",
            api_key=os.getenv('OPENROUTER_API_KEY'),
            tokens=4096,
            temperature=0.7,
            max_retries=2,
            timeout=30
        )

        self.fast_llm = LLM(
            model="openrouter/groq/groq-1:free",
            api_key=os.getenv('OPENROUTER_API_KEY'),
            tokens=4096,
            temperature=0.3,
            max_retries=2,
            timeout=30
        )

    @tool("process_data", return_direct=True)
    def process_data(dataset: pd.DataFrame) -> str:
        dataset = dataset.dropna()
        return dataset.to_string(index=False)

    @tool("analyze_data", return_direct=True)
    def analyze_data(dataset: pd.DataFrame) -> str:
        summary = dataset.describe()
        return summary.to_string()

    def create_agents(self):
        self.data_analyst = Agent(
            role="Senior Fitness Data Analyst",
            goal=(
                "Analyze the provided fitness dataset and extract accurate, "
                "actionable insights that are directly relevant to the user's context. "
                "Prioritize patterns, trends, and anomalies that matter most for fitness outcomes."
            ),
            backstory=(
                "You are a seasoned data analyst with 15 years of experience in sports science "
                "and fitness performance. You've worked with professional athletes and everyday "
                "gym-goers alike, turning raw workout logs and health metrics into clear, "
                "personalized recommendations. You are meticulous about data quality — you always "
                "clean data before drawing conclusions and never speculate beyond what the numbers show."
            ),
            tools=[self.process_data, self.analyze_data],
            verbose=True,
            memory=True,
            llm=self.smart_llm,
            max_rpm=15,
            allow_delegation=False,
            cache=True
        )

    def create_tasks(self):
        self.analyze_task = Task(
            description=(
                f"You have been given a fitness dataset and the following user context:\n\n"
                f"CONTEXT: {self.context}\n\n"
                "Step 1: Use the process_data tool to clean the dataset and get a readable view.\n"
                "Step 2: Use the analyze_data tool to compute summary statistics.\n"
                "Step 3: Based on both outputs and the user context, identify the most relevant trends, "
                "strengths, and areas for improvement."
            ),
            expected_output=(
                "A structured fitness analysis report containing:\n"
                "1. Data quality summary (rows cleaned, missing values removed)\n"
                "2. Key statistics (averages, ranges, notable outliers)\n"
                "3. 3-5 specific insights tied directly to the user's context\n"
                "4. 2-3 concrete, actionable recommendations"
            ),
            tools=[self.process_data, self.analyze_data],
            agent=self.data_analyst,
            output_file="output.log",
            human_input=False
        )

    def create_crew(self):
        self.crew = Crew(
            agents=[self.data_analyst],
            tasks=[self.analyze_task],
            process=Process.sequential,
            verbose=True,
        )
        self.crew.kickoff(inputs={})