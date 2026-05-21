import os
import pandas as pd

from bots import Bots

data_path = os.path.join(os.path.dirname(__file__), "test_data.csv")
bots = Bots("What are the key insights from this dataset?")
bots.create_agents()
bots.create_tasks()
bots.create_crew(data_path)