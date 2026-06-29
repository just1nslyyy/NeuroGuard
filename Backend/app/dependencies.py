# ═══════════════════════════════════════════════════════════
#  dependencies.py
#  FastAPI зависимости — БД, токены, rate limit
# ═══════════════════════════════════════════════════════════

import asyncio
import time
from collections import defaultdict
from fastapi import HTTPException, Request
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.future import select
from sqlalchemy import update
from datetime import datetime, timezone

from .database import get_db
from .models import Agent, User
from .config import (
    INTERNAL_API_TOKEN,
    RANK_LIMITS,
    CODE_TTL_SECONDS,
    CODE_MAX_ATTEMPTS,
)

# ── Rate limit для авторизации ───────────────────────────────
auth_attempts_lock = asyncio.Lock()
auth_attempts: dict[str, int] = defaultdict(int)

# ── Кэш алертов ──────────────────────────────────────────────
alert_cache: dict[str, float] = {}

# ── Коды авторизации ─────────────────────────────────────────
auth_code_meta: dict[str, dict] = {}


# ── Проверка внутреннего токена ──────────────────────────────
def require_internal_token(api_token: str | None) -> None:
    if not api_token or api_token != INTERNAL_API_TOKEN:
        raise HTTPException(status_code=401, detail="unauthorized")


# ── Получение ранга пользователя ─────────────────────────────
async def get_user_rank(tg_id: str, db: AsyncSession) -> str:
    res = await db.execute(select(User).where(User.tg_id == str(tg_id)))
    user = res.scalars().first()
    if not user:
        user = User(tg_id=str(tg_id), rank="FREE")
        db.add(user)
        await db.commit()
        return "FREE"
    if (user.subscription_end
            and user.rank != "FREE"
            and user.subscription_end < datetime.now(timezone.utc)):
        user.rank = "FREE"
        await db.execute(
            update(Agent).where(Agent.tg_id == str(tg_id)).values(rank="FREE")
        )
        await db.commit()
        print(f"[EXPIRED] {tg_id} → FREE")
    return user.rank.upper()


# ── Получение даты окончания подписки ────────────────────────
async def get_user_subscription_end(tg_id: str, db: AsyncSession):
    res = await db.execute(select(User).where(User.tg_id == str(tg_id)))
    u = res.scalars().first()
    return u.subscription_end if u else None


# ── Лимит устройств ──────────────────────────────────────────
async def enforce_device_limit(tg_id: str, rank: str, db: AsyncSession):
    limit = RANK_LIMITS.get(rank.upper(), 1)
    res = await db.execute(
        select(Agent)
        .where(Agent.tg_id == str(tg_id))
        .order_by(Agent.id.asc())
    )
    agents = res.scalars().all()
    for a in agents[limit:]:
        print(f"[CLEANUP] Удаляем лишнее устройство: {a.hwid}")
        await db.delete(a)
    if len(agents) > limit:
        await db.commit()


# ── Авто-очистка статистики SENTINEL ─────────────────────────
async def auto_clear_sentinel_stats(db: AsyncSession):
    from datetime import timedelta
    from sqlalchemy import delete as sa_delete
    from .models import Telemetry

    cutoff = datetime.now(timezone.utc) - timedelta(days=7)
    res = await db.execute(
        select(Agent)
        .join(User, Agent.tg_id == User.tg_id)
        .where(User.rank == "SENTINEL")
    )
    agents = res.scalars().all()
    deleted = 0
    for agent in agents:
        r = await db.execute(
            sa_delete(Telemetry).where(
                Telemetry.agent_id == agent.id,
                Telemetry.created_at < cutoff,
            )
        )
        deleted += r.rowcount
    await db.commit()
    if deleted:
        print(f"[AUTO_CLEAN] SENTINEL: удалено {deleted} записей")


# ── Очистка устаревших кодов ─────────────────────────────────
async def cleanup_expired_codes():
    while True:
        await asyncio.sleep(300)
        now = time.time()
        expired = [
            c for c, m in auth_code_meta.items()
            if now - m["created_at"] > CODE_TTL_SECONDS * 2
        ]
        for c in expired:
            auth_code_meta.pop(c, None)
        if expired:
            print(f"[CLEANUP] Удалено {len(expired)} устаревших кодов")