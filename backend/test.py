import pandas as pd

from bots import Bots

df = pd.read_csv("test_data.csv")

bots = Bots("What are the key insights from this dataset?")
bots.create_agents()
bots.create_tasks()
bots.create_crew(df)