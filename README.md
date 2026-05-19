Hello! This is a project I made for myself and it also just so happened that I submitted it for the 2026 Hack America Hackathon.

I put a lot of time into this project from making the installation of necessary packages super easy to even considering the small functionality details such as runtime on terrible laptops like mine. I even expanded to supporting cloud models such as Groq, ChatGPT, and Anthropic to name a few. Any support is much appreciated.

Please read the license before downloading as it may change any intentions of downloading this project.

## Setup

If you want to use Ollama as the local model to incorporate AI security practices use the setup.sh which works on every major OS [Linux, MacOS, or Windows].
First give the program the permissions to be ran using this command:
`chmod +x setup.sh`
Run the shell script like this on Powershell after the above command is done:
`bash setup.sh`
Run the shell script like this on MacOS or Linux after the command is done:
`./setup.sh`

However you can just get an API key from a provider like Groq or Anthropic if your device doesn't have a GPU and/or a good CPU, just run the CLI or the Web UI settings and select the proper API key to save.

## Running the Program

The setup shell script automatically creates a virtual environment called `.venv` so activate it through the proper commands.
For Windows use the following command:
`.venv/bin/Activate.ps1`
For MacOS and Linux use the following command:
`source .venv/bin/activate`

Then it is time to activate the running script, run.py. Activate it by using the following command:
`python run.py`

Toggle to the UI that you prefer, whether the CLI or the Web UI version it will communicate with the API if it has been added yet or Local LLM to produce the desired result.

## Features of this project

This project has many useful features that make this project truly stand out, however the pinacle aspect of this project is Streamlit due to the packages that Streamlit makes available for public use compared to the CLI, thus not all of the discussed features are able to be directly applied to the CLI.

- The Chat Feature allows the processing LLM to use natural language to complete the user's task based on the content provided and stored in the ChromaDB. This feature processes the input to then output the various artifacts that are then saved and can be viewed later such as flashcards and quizzes.
- The Add Content Feature is vital for success since without it the ChromaDB would never be updated. This important feature adds the ability to upload not just files but take a photo to be uploaded through the Steamlit package.
- The Setup Shell Script [say that 10 times fast] quickly initalizes everything necessary for the project to run without the hastle of understanding every little bit, abstracting the semi-complex and super boring commands that need to be added. This makes it easy so that the user doesn't have to focus on the difficult aspects of installation themself.
- The essential Config.toml file stores any API keys that are for the cloud-based AI engines securely and makes changing API keys very easy with the interfaces using the functions in `core_stuff.py`.
- The Update Feature allows the program to check for any updates in the GitHub repo instead of manually reinstalling the repo. This feature is accessible in every UI.

## Inspiration

Inspiration for this project has come from me not wanting to have to rely on tools like Quizlet and NotebookLM to study my notes since I have ADHD and I strongly detest studying and I don't enjoy school outside of computers. This tool is meant to help me and others that want this material to learn material to a depth that hasn't been done before and also improve the speed of learning.

## What it does

This AI tool uses imported texts [supports MD files, PDFs, etc] and also image files to extract information to aid studying like a professional RAG would do.

## How I built it

I used Python as the framework, utilizing both the CLI aspect and the Web UI application using Streamlit. These both have different features that are available, only the Web UI can upload photos directly from the camera but that may change in later updates if people desire a change.

## Challenges I ran into

A challenge for me is always staying motivated in a project. Sticking to this task is definitely the first challenge I ran into but also the thought of using AI to create everything was so tempting! Instead I ended up using the AI to debug my code and also teach me how best to use ChromaDB and how it works.

## Accomplishments that I proud of

I am not normally proud of myself for much, though it is an achievement that I have stuck to a project for longer than a week. If you go to my website [here](https://pymite6941.is-a.dev), you can see majority of my projects don't get continued as long due to me losing interest or motivation. I am also proud of me learning new tools, ChromaDB and Ollama, since that would be the necessary foundation for improving my most famous tool [the finance kit](https://github.com/pymite6941/expense-tracker).

## What I learned

I learned how to use ChromaDB, a great platform that stores inputted data in a vector database that is then accessed to get a query. I also learned how to query a Ollama local model in this

## What's next for Study Assistant

I plan to be adding more and more onto Study Assistant so if you want later versions go to this project's [GitHub repo](https://github.com/pymite6941/study-assistant) for the updates and for the whole process.
