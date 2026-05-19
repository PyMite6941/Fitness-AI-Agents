from crewai import Agent, Crew, Task
from crewai.tools import tool

@tool("get_data")
def get_data(self):

