"""Lightweight, rate-limit-resilient LLM caller for the Vercel backend.

One chat call, but rotated across a POOL of free Groq + OpenRouter models: if one
returns 429 (or any error), it immediately tries the next — so a single rate-limited
model never fails the request. Also reads the providers' rate-limit headers and
returns how much quota is left, so the API can surface "tokens remaining".

This is the light counterpart to backend/bots.py's heavy CrewAI rotation.
"""
import os

import httpx

GROQ_KEY = os.getenv("GROQ_API_KEY", "")
OR_KEY   = os.getenv("OPENROUTER_API_KEY", "")
GROQ_URL = "https://api.groq.com/openai/v1/chat/completions"
OR_URL   = "https://openrouter.ai/api/v1/chat/completions"

# Reliable/fast first (Groq has generous free limits), then free OpenRouter models.
_GROQ_MODELS = ["llama-3.3-70b-versatile", "llama-3.1-8b-instant", "gemma2-9b-it", "llama3-70b-8192"]
_OR_MODELS = [
    "meta-llama/llama-3.3-70b-instruct:free",
    "deepseek/deepseek-chat-v3-0324:free",
    "google/gemma-3-27b-it:free",
    "meta-llama/llama-3.1-8b-instruct:free",
]


def _pool():
    pool = []
    if GROQ_KEY:
        pool += [(GROQ_URL, GROQ_KEY, m) for m in _GROQ_MODELS]
    if OR_KEY:
        pool += [(OR_URL, OR_KEY, m) for m in _OR_MODELS]
    return pool


def _quota_from(model: str, headers) -> dict:
    """Pull remaining-quota info from provider rate-limit headers (best-effort)."""
    g = headers.get
    return {
        "model": model,
        "tokens_remaining": g("x-ratelimit-remaining-tokens") or g("x-ratelimit-remaining"),
        "requests_remaining": g("x-ratelimit-remaining-requests"),
        "reset": g("x-ratelimit-reset-tokens") or g("x-ratelimit-reset-requests"),
    }


class LLMUnavailable(Exception):
    pass


async def complete(messages, json_mode: bool = False, max_tokens: int = 1500,
                   temperature: float = 0.4) -> tuple[str, dict]:
    """Return (content, quota). Rotates models on 429/any error. Raises LLMUnavailable if all fail."""
    pool = _pool()
    if not pool:
        raise LLMUnavailable("No model API key configured (GROQ_API_KEY / OPENROUTER_API_KEY).")

    last = ""
    async with httpx.AsyncClient(timeout=60) as client:
        for url, key, model in pool:
            payload = {"model": model, "messages": messages,
                       "temperature": temperature, "max_tokens": max_tokens}
            if json_mode:
                payload["response_format"] = {"type": "json_object"}
            try:
                r = await client.post(url, headers={"Authorization": f"Bearer {key}"}, json=payload)
            except Exception as e:
                last = f"{model}: {str(e)[:50]}"
                continue
            if r.status_code == 200:
                content = r.json()["choices"][0]["message"]["content"]
                return content, _quota_from(model, r.headers)
            # 429 (rate limit), 4xx (e.g. model lacks json mode), 5xx -> just rotate.
            last = f"{model} HTTP {r.status_code}"
    raise LLMUnavailable(f"All models busy/unavailable ({last}).")
