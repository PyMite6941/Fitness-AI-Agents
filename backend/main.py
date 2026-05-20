from fastapi import FastAPI, UploadFile, File, HTTPException, BackgroundTasks
import io
import pandas as pd

from bots import Bots

app = FastAPI()

@app.post("/start_process")
async def process_stuff(context:str,data_file:UploadFile = File()):
    bots = Bots(context=context)
    filename = data_file.name
    extension = filename.split(".")[-1].lower() if "." in filename else ""
    contents = await data_file.read()
    buffer = io.BytesIO(contents)
    try:
        if extension in ["csv", "txt"]:
            df = pd.read_csv(buffer)
        elif extension in ["xlsx", "xls"]:
            df = pd.read_excel(buffer)
        elif extension == "parquet":
            df = pd.read_parquet(buffer)
        elif extension == "json":
            df = pd.read_json(buffer)
        else:
            raise HTTPException(
                status_code=400, 
                detail=f"Unsupported file extension: .{extension}"
            )
        df = df.dropna()
        try:
            bots.create_agents()
            bots.create_tasks()
            bots.create_crew(df)
        except Exception as e:
            raise HTTPException(status_code=500, detail=f"Crew execution failed: {str(e)}")
    except Exception as e:
        raise HTTPException(
            status_code=422, 
            detail=f"Failed to parse the file. Ensure it is formatted correctly. Error: {str(e)}"
        )
    finally:
        buffer.close()