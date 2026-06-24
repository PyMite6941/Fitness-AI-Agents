import os
from datetime import datetime, timezone

import jwt
from jwt import PyJWKClient
from fastapi import Depends, HTTPException, Security
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer

from db import get_db

bearer = HTTPBearer()

# Opaque tokens issued to paired phones start with this prefix so we can route
# them to the device-token path instead of Clerk JWT verification.
DEVICE_TOKEN_PREFIX = "fit_"

CLERK_JWKS_URL    = os.getenv("CLERK_JWKS_URL")
CLERK_ISSUER_URL  = os.getenv("CLERK_ISSUER_URL", "")  # e.g. https://pretty-bird-74.clerk.accounts.dev

_jwks_client: PyJWKClient | None = None


def get_jwks_client() -> PyJWKClient:
    global _jwks_client
    if _jwks_client is None:
        _jwks_client = PyJWKClient(CLERK_JWKS_URL, cache_keys=True)
    return _jwks_client


def _decode_clerk_jwt(token: str) -> dict:
    try:
        signing_key = get_jwks_client().get_signing_key_from_jwt(token)
        decode_kwargs: dict = {
            "algorithms": ["RS256"],
            "options": {"verify_aud": False},
        }
        if CLERK_ISSUER_URL:
            decode_kwargs["issuer"] = CLERK_ISSUER_URL
        return jwt.decode(token, signing_key.key, **decode_kwargs)
    except jwt.ExpiredSignatureError:
        raise HTTPException(status_code=401, detail="Token expired")
    except jwt.InvalidTokenError as e:
        raise HTTPException(status_code=401, detail=f"Invalid token: {str(e)}")


async def verify_token(credentials: HTTPAuthorizationCredentials = Security(bearer)) -> dict:
    return _decode_clerk_jwt(credentials.credentials)


def get_user_id(payload: dict = Depends(verify_token)) -> str:
    """Strict Clerk-only auth — use for web-app routes."""
    user_id = payload.get("sub")
    if not user_id:
        raise HTTPException(status_code=401, detail="No user ID in token")
    return user_id


async def _user_id_from_device_token(token: str) -> str:
    db = await get_db()
    res = (
        await db.table("device_tokens")
        .select("user_id, revoked")
        .eq("token", token)
        .limit(1)
        .execute()
    )
    rows = res.data or []
    if not rows or rows[0].get("revoked"):
        raise HTTPException(status_code=401, detail="Invalid or revoked device token")
    # Best-effort "last seen" touch — never fail the request over it.
    try:
        await db.table("device_tokens").update(
            {"last_used_at": datetime.now(timezone.utc).isoformat()}
        ).eq("token", token).execute()
    except Exception:
        pass
    return rows[0]["user_id"]


async def get_user_id_flexible(
    credentials: HTTPAuthorizationCredentials = Security(bearer),
) -> str:
    """Accept EITHER a Clerk JWT (web app) OR a paired-device token (phone app).

    Use this for ingestion routes the Android tracker posts to.
    """
    token = credentials.credentials
    if token.startswith(DEVICE_TOKEN_PREFIX):
        return await _user_id_from_device_token(token)
    return get_user_id(_decode_clerk_jwt(token))
