# ═══════════════════════════════════════════════════════════
#  routers/admin.py
#  /admin/* — эндпоинты для AdminPanel
# ═══════════════════════════════════════════════════════════

from datetime import datetime, timezone, timedelta
from fastapi import APIRouter, Depends, Header, Query
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.future import select
from sqlalchemy import func, update

from ..database import get_db
from ..models import Agent, Telemetry, User
from ..dependencies import require_internal_token

router = APIRouter(prefix="/admin", tags=["admin"])


@router.get("/stats")
async def admin_stats(
    api_token: str | None = Header(None, alias="api-token"),
    db: AsyncSession = Depends(get_db),
):
    require_internal_token(api_token)

    total_users     = (await db.execute(select(func.count(User.id)))).scalar() or 0
    free_count      = (await db.execute(select(func.count(User.id)).where(User.rank == "FREE"))).scalar() or 0
    sentinel_count  = (await db.execute(select(func.count(User.id)).where(User.rank == "SENTINEL"))).scalar() or 0
    overseer_count  = (await db.execute(select(func.count(User.id)).where(User.rank == "OVERSEER"))).scalar() or 0
    cutoff          = datetime.now(timezone.utc) - timedelta(seconds=40)
    online_agents   = (await db.execute(
        select(func.count(func.distinct(Telemetry.agent_id)))
        .where(Telemetry.created_at >= cutoff)
    )).scalar() or 0
    hanging_agents  = (await db.execute(
        select(func.count(Agent.id)).where(Agent.tg_id.is_(None))
    )).scalar() or 0

    return {
        "total_users":    total_users,
        "free_count":     free_count,
        "sentinel_count": sentinel_count,
        "overseer_count": overseer_count,
        "online_agents":  online_agents,
        "hanging_agents": hanging_agents,
    }


@router.get("/users")
async def admin_users(
    page: int = 0,
    api_token: str | None = Header(None, alias="api-token"),
    db: AsyncSession = Depends(get_db),
):
    require_internal_token(api_token)
    PAGE_SIZE = 10
    total = (await db.execute(select(func.count(User.id)))).scalar() or 0
    res   = await db.execute(
        select(User).order_by(User.id.desc())
        .offset(page * PAGE_SIZE).limit(PAGE_SIZE)
    )
    users = res.scalars().all()
    result = []
    for u in users:
        sub_end = None
        if u.subscription_end:
            sub_end = (u.subscription_end + timedelta(hours=3)).strftime("%d.%m.%Y")
        result.append({
            "tg_id":            u.tg_id,
            "rank":             u.rank,
            "subscription_end": sub_end,
            "trial_used":       getattr(u, "trial_used", False),
        })
    return {"users": result, "total": total, "page": page}


@router.get("/agents")
async def admin_agents(
    page: int = 0,
    api_token: str | None = Header(None, alias="api-token"),
    db: AsyncSession = Depends(get_db),
):
    require_internal_token(api_token)
    PAGE_SIZE = 10
    total = (await db.execute(select(func.count(Agent.id)))).scalar() or 0
    res   = await db.execute(
        select(Agent).order_by(Agent.id.desc())
        .offset(page * PAGE_SIZE).limit(PAGE_SIZE)
    )
    agents  = res.scalars().all()
    now_utc = datetime.now(timezone.utc)
    result  = []
    for a in agents:
        tel_res = await db.execute(
            select(Telemetry).where(Telemetry.agent_id == a.id)
            .order_by(Telemetry.created_at.desc()).limit(1)
        )
        last = tel_res.scalars().first()
        online    = False
        last_seen = "никогда"
        if last:
            ts = last.created_at
            if ts.tzinfo is None:
                ts = ts.replace(tzinfo=timezone.utc)
            online    = (now_utc - ts) < timedelta(seconds=40)
            last_seen = (ts + timedelta(hours=3)).strftime("%d.%m %H:%M")
        result.append({
            "hwid":      a.hwid or "?",
            "name":      a.name or "Unknown",
            "tg_id":     a.tg_id or "—",
            "rank":      a.rank  or "FREE",
            "online":    online,
            "last_seen": last_seen,
        })
    return {"agents": result, "total": total, "page": page}


@router.get("/all_user_ids")
async def admin_all_user_ids(
    api_token: str | None = Header(None, alias="api-token"),
    db: AsyncSession = Depends(get_db),
):
    require_internal_token(api_token)
    res = await db.execute(select(User.tg_id))
    ids = [row[0] for row in res.all() if row[0]]
    return {"ids": ids, "count": len(ids)}