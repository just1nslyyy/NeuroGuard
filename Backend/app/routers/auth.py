# ═══════════════════════════════════════════════════════════
#  routers/auth.py
#  /auth/* — авторизация устройств
# ═══════════════════════════════════════════════════════════

import random
import time
from typing import Optional
from fastapi import APIRouter, Depends, HTTPException, Request
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.future import select
from sqlalchemy import func

from ..database import get_db
from ..models import Agent
from ..config import RANK_LIMITS, CODE_TTL_SECONDS, CODE_MAX_ATTEMPTS
from ..dependencies import (
    auth_attempts_lock, auth_attempts,
    auth_code_meta, get_user_rank,
)

router = APIRouter(prefix="/auth", tags=["auth"])


@router.post("/generate/{hwid}")
async def generate_code(hwid: str, db: AsyncSession = Depends(get_db)):
    res = await db.execute(select(Agent).where(Agent.hwid == hwid))
    agent = res.scalar_one_or_none()
    if agent and agent.tg_id:
        return {"status": "already_linked", "tg_id": agent.tg_id}
    code = str(random.randint(100000, 999999))
    if not agent:
        agent = Agent(hwid=hwid, name="New Device")
        db.add(agent)
    agent.auth_code = code
    auth_code_meta[code] = {
        "created_at": time.time(),
        "attempts": 0,
        "hwid": hwid,
    }
    await db.commit()
    return {"code": code}


@router.post("/verify")
async def verify_code(
    code: str, tg_id: str,
    request: Request,
    db: AsyncSession = Depends(get_db)
):
    client_ip = request.client.host
    async with auth_attempts_lock:
        if auth_attempts[client_ip] > 10:
            raise HTTPException(status_code=429, detail="too_many_attempts")
        auth_attempts[client_ip] += 1

    meta = auth_code_meta.get(code)
    if not meta:
        raise HTTPException(status_code=404, detail="invalid_code")
    if time.time() - meta["created_at"] > CODE_TTL_SECONDS:
        auth_code_meta.pop(code, None)
        raise HTTPException(status_code=410, detail="code_expired")
    if meta["attempts"] >= CODE_MAX_ATTEMPTS:
        raise HTTPException(status_code=429, detail="too_many_attempts")

    res = await db.execute(select(Agent).where(Agent.auth_code == code))
    agent = res.scalar_one_or_none()
    if not agent:
        meta["attempts"] += 1
        raise HTTPException(status_code=404, detail="invalid_code")

    current_rank = await get_user_rank(tg_id, db)
    count = (await db.execute(
        select(func.count(Agent.id)).where(Agent.tg_id == str(tg_id))
    )).scalar() or 0

    if count >= RANK_LIMITS.get(current_rank, 1):
        raise HTTPException(status_code=403, detail="limit_reached")

    agent.tg_id    = str(tg_id)
    agent.rank     = current_rank
    agent.auth_code = None
    auth_code_meta.pop(code, None)
    await db.commit()
    return {"status": "success", "rank_granted": current_rank, "devices_total": count + 1}


@router.post("/update_name")
async def update_name(
    tg_id: str, name: str,
    hwid: Optional[str] = None,
    db: AsyncSession = Depends(get_db)
):
    q = select(Agent).where(Agent.tg_id == str(tg_id))
    if hwid:
        q = q.where(Agent.hwid == hwid)
    else:
        q = q.order_by(Agent.id.desc())
    res = await db.execute(q)
    agent = res.scalars().first()
    if not agent:
        raise HTTPException(status_code=404, detail="agent_not_found")
    agent.name = name
    await db.commit()
    return {"status": "ok", "name": name, "hwid": agent.hwid}


@router.post("/delete_device")
async def delete_device(
    tg_id: str, hwid: str,
    db: AsyncSession = Depends(get_db)
):
    res = await db.execute(
        select(Agent).where(Agent.tg_id == str(tg_id), Agent.hwid == hwid)
    )
    agent = res.scalars().first()
    if not agent:
        raise HTTPException(status_code=404, detail="device_not_found")
    agent.tg_id = None
    await db.commit()
    return {"status": "ok"}