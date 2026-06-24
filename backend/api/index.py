"""Vercel serverless entry point.

Vercel's Python runtime serves the ASGI `app` exported here. All paths are routed
to this function by vercel.json, and FastAPI handles the sub-routing. The app loads
without the heavy AI stack (crewai is lazy-imported only inside /analyze), so it fits
Vercel's size limit and runs always-on.
"""
import os
import sys

# Make the backend root importable (main.py, routes/, auth.py live one level up).
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from main import app  # noqa: E402  (re-exported for the Vercel Python runtime)
