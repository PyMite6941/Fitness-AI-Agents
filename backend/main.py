from dotenv import load_dotenv
load_dotenv()

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from routes import watch, analysis, user, charts, gps, integrations

app = FastAPI(title="Fitness AI Agents")

app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(watch.router,    prefix="/watch",    tags=["Watch"])
app.include_router(analysis.router, prefix="/analyze",  tags=["Analysis"])
app.include_router(user.router,     prefix="/user",     tags=["User"])
app.include_router(charts.router,   prefix="/charts",   tags=["Charts"])
app.include_router(gps.router,          prefix="/routes",       tags=["GPS"])
app.include_router(integrations.router, prefix="/integrations", tags=["Integrations"])

@app.get("/health")
async def health():
    return {"status": "ok"}
