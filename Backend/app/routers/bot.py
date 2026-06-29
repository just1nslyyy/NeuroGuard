# ═══════════════════════════════════════════════════════════
#  routers/bot.py
#  /bot/* — команды для Telegram бота
# ═══════════════════════════════════════════════════════════

import time
from typing import Optional
from datetime import datetime, timezone, timedelta
from fastapi import APIRouter, Depends, HTTPException, Form, UploadFile, File
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.future import select
from sqlalchemy import update
from sqlalchemy.orm import selectinload
import httpx

from ..database import get_db
from ..models import Agent, Telemetry, DiskTelemetry, User
from ..dependencies import (
    alert_cache, get_user_rank,
    get_user_subscription_end, enforce_device_limit,
    require_internal_token,
)
from ..utils import (
    get_system_status, format_subscription_end,
    format_time_msk, format_date_msk, avg, mx,
    send_telegram_message,
)
from ..config import BOT_TOKEN, RANK_LIMITS

router = APIRouter(prefix="/bot", tags=["bot"])


@router.get("/status")
async def get_bot_status(tg_id: str, db: AsyncSession = Depends(get_db)):
    current_rank = await get_user_rank(tg_id, db)
    sub_end_str  = format_subscription_end(await get_user_subscription_end(tg_id, db))

    res_user = await db.execute(select(User).where(User.tg_id == str(tg_id)))
    user_obj = res_user.scalars().first()
    psu_watts_val = getattr(user_obj, 'psu_watts', None) if user_obj else None

    await enforce_device_limit(tg_id, current_rank, db)

    res = await db.execute(select(Agent).where(Agent.tg_id == str(tg_id)))
    agents = res.scalars().all()
    now_utc = datetime.now(timezone.utc)
    devices_list = []

    for agent in agents:
        tel_res = await db.execute(
            select(Telemetry)
            .where(Telemetry.agent_id == agent.id)
            .options(selectinload(Telemetry.cores), selectinload(Telemetry.disks))
            .order_by(Telemetry.created_at.desc())
            .limit(1)
        )
        last = tel_res.scalars().first()
        is_online = False
        status_text = "🔴 ОФФЛАЙН"
        last_seen = "N/A"

        if last:
            ts = last.created_at
            if ts.tzinfo is None:
                ts = ts.replace(tzinfo=timezone.utc)
            if now_utc - ts < timedelta(seconds=40):
                is_online = True
                status_text = get_system_status(
                    last.cpu_avg_load or 0.0, last.ram_percent or 0,
                    last.cpu_temp or 0.0, last.gpu_temp or 0.0)
            last_seen = format_time_msk(ts)

        cpu_name = (last.cpu_name if last and last.cpu_name else None) or agent.cpu_model or "N/A"
        gpu_name = (last.gpu_name if last and last.gpu_name else None) or agent.gpu_model or "N/A"

        devices_list.append({
            "hwid":          agent.hwid or "N/A",
            "name":          agent.name or "Device",
            "rank":          current_rank,
            "status_label":  status_text,
            "online":        is_online,
            "last_seen":     last_seen,
            "last_seen_time": last_seen,
            "subscription_end": sub_end_str,
            "psu_watts":     psu_watts_val,
            "cpu":           last.cpu_avg_load if last else 0.0,
            "cpu_temp":      last.cpu_temp     if last else 0.0,
            "gpu":           last.gpu_load     if last else 0.0,
            "gpu_temp":      last.gpu_temp     if last else 0.0,
            "ram":           last.ram_percent  if last else 0,
            "ram_used":      last.ram_used     if last else 0.0,
            "ram_total":     last.ram_total    if last else 0.0,
            "net_down":      last.net_down     if last else 0.0,
            "net_up":        last.net_up       if last else 0.0,
            "cpu_name":      cpu_name,
            "gpu_name":      gpu_name,
            "disk_name":     last.disks_info   if last else "N/A",
            "power_source":  last.power_source if last else "AC",
            "battery_pct":   last.battery_pct  if last else 100,
            "uptime":        last.uptime        if last else 0,
            "top_processes": last.top_processes if last else [],
            "cores": [
                c.load for c in sorted(last.cores, key=lambda x: x.core_index)
            ] if last else [],
            "disks": [
                {"name": d.drive_name, "total": d.total_gb,
                 "used": d.used_gb, "percent": d.percent}
                for d in last.disks
            ] if last else [],
        })

    return {
        "user_rank":        current_rank,
        "subscription_end": sub_end_str,
        "psu_watts":        psu_watts_val,
        "devices":          devices_list,
    }


@router.get("/status/{hwid}")
async def get_device_health(hwid: str, tg_id: str, db: AsyncSession = Depends(get_db)):
    current_rank = await get_user_rank(tg_id, db)
    sub_end_str  = format_subscription_end(await get_user_subscription_end(tg_id, db))

    res_user = await db.execute(select(User).where(User.tg_id == str(tg_id)))
    user_obj = res_user.scalars().first()
    psu_watts_val = getattr(user_obj, 'psu_watts', None) if user_obj else None

    res_agent = await db.execute(
        select(Agent).where(Agent.hwid == hwid, Agent.tg_id == str(tg_id))
    )
    agent = res_agent.scalars().first()
    if not agent:
        raise HTTPException(status_code=404, detail="agent_not_found")

    since = datetime.now(timezone.utc) - timedelta(days=7)
    res_tel = await db.execute(
        select(Telemetry)
        .where(Telemetry.agent_id == agent.id, Telemetry.created_at >= since)
        .order_by(Telemetry.created_at.asc())
    )
    rows = res_tel.scalars().all()
    if not rows:
        return {"status": "no_data"}

    cpu_temps = [r.cpu_temp    for r in rows if r.cpu_temp    and r.cpu_temp  > 0]
    cpu_loads = [r.cpu_avg_load for r in rows if r.cpu_avg_load is not None]
    ram_loads = [r.ram_percent  for r in rows if r.ram_percent  is not None]
    gpu_loads = [r.gpu_load    for r in rows if r.gpu_load    is not None]
    gpu_temps = [r.gpu_temp    for r in rows if r.gpu_temp    and r.gpu_temp  > 0]
    net_downs = [r.net_down    for r in rows if r.net_down    is not None]
    net_ups   = [r.net_up      for r in rows if r.net_up      is not None]

    peak_row = max(rows, key=lambda r: r.cpu_temp or 0)
    peak_ts  = peak_row.created_at
    if peak_ts.tzinfo is None:
        peak_ts = peak_ts.replace(tzinfo=timezone.utc)

    online_seconds = len(rows) * 3
    oh = online_seconds // 3600
    om = (online_seconds % 3600) // 60
    online_str = f"{oh}ч {om}м" if oh > 0 else f"{om}м"

    last  = rows[-1]
    first = rows[0]
    cpu_name = (last.cpu_name if last.cpu_name else None) or agent.cpu_model or "N/A"
    gpu_name = (last.gpu_name if last.gpu_name else None) or agent.gpu_model or "N/A"

    res_disks = await db.execute(
        select(DiskTelemetry).where(DiskTelemetry.telemetry_id == last.id)
    )
    last_disks = res_disks.scalars().all()
    disk_data = [{
        "name":        d.drive_name,
        "total":       d.total_gb,
        "used":        d.used_gb,
        "percent":     d.percent,
        "wear_status": "🟢 Норма" if d.percent < 85 else "🟡 Заполнен",
    } for d in last_disks]

    top_procs_week = []
    if current_rank == "OVERSEER" and last.top_processes:
        top_procs_week = [
            {"name": p.get("name","?"), "avg_cpu": p.get("cpu",0.0), "ram_usage": p.get("ram",0.0)}
            for p in last.top_processes
        ]

    estimated_kwh = estimated_cost = None
    if psu_watts_val and psu_watts_val > 0:
        kwh = (psu_watts_val * (online_seconds / 3600.0)) / 1000.0
        estimated_kwh  = round(kwh, 2)
        estimated_cost = round(kwh * 5.5, 1)

    return {
        "status":       "ready",
        "agent_name":   agent.name,
        "period_from":  format_date_msk(first.created_at),
        "period_to":    format_date_msk(last.created_at),
        "online_str":   online_str,
        "cpu_name":     cpu_name,
        "gpu_name":     gpu_name,
        "cpu_temp_avg": round(avg(cpu_temps), 1),
        "cpu_temp_max": round(mx(cpu_temps), 1),
        "cpu_load_avg": round(avg(cpu_loads), 1),
        "ram_load_avg": round(avg(ram_loads), 1),
        "gpu_load_avg": round(avg(gpu_loads), 1),
        "gpu_temp_avg": round(avg(gpu_temps), 1),
        "gpu_temp_max": round(mx(gpu_temps), 1),
        "net_down_avg": round(avg(net_downs), 2),
        "net_up_avg":   round(avg(net_ups),   2),
        "peak_temp_time": (peak_ts + timedelta(hours=3)).strftime("%d.%m %H:%M"),
        "disks":              disk_data,
        "top_processes_week": top_procs_week,
        "estimated_kwh":      estimated_kwh,
        "estimated_cost":     estimated_cost,
        "psu_watts":          psu_watts_val,
        "subscription_end":   sub_end_str,
    }


@router.post("/set_command")
async def set_command(
    hwid: str, command: str, tg_id: str,
    db: AsyncSession = Depends(get_db)
):
    valid = {"shutdown", "sleep", "screenshot", "cancel_shutdown", "kill_"}
    if not any(command.startswith(c) for c in valid):
        raise HTTPException(status_code=400, detail="invalid_command")
    res = await db.execute(select(Agent).where(Agent.hwid == hwid))
    agent = res.scalars().first()
    if not agent:
        raise HTTPException(status_code=404, detail="agent_not_found")
    if str(agent.tg_id or "") != str(tg_id):
        raise HTTPException(status_code=403, detail="forbidden")
    agent.pending_command = command
    await db.commit()
    return {"status": "ok", "command": command, "hwid": agent.hwid}


@router.post("/mute")
async def mute_alerts(
    hwid: str, tg_id: str, hours: int = 1,
    db: AsyncSession = Depends(get_db)
):
    res = await db.execute(select(Agent).where(Agent.hwid == hwid))
    agent = res.scalars().first()
    if not agent:
        raise HTTPException(status_code=404, detail="agent_not_found")
    if str(agent.tg_id or "") != str(tg_id):
        raise HTTPException(status_code=403, detail="forbidden")
    mute_until = time.time() + hours * 3600
    alert_cache[f"{hwid}_cpu"] = mute_until
    alert_cache[f"{hwid}_gpu"] = mute_until
    return {"status": "ok"}


@router.post("/upgrade")
async def upgrade_rank(
    tg_id:      str = Form(...),
    rank:       str = Form(...),
    days:       int = Form(30),
    api_token: str | None = Form(None),
    db: AsyncSession = Depends(get_db)
):
    require_internal_token(api_token)
    res = await db.execute(select(User).where(User.tg_id == str(tg_id)))
    user = res.scalars().first()
    if not user:
        user = User(tg_id=str(tg_id))
        db.add(user)
        await db.flush()
    now = datetime.now(timezone.utc)
    if (user.subscription_end and user.subscription_end > now
            and user.rank == rank.upper()):
        user.subscription_end = user.subscription_end + timedelta(days=days)
    else:
        user.subscription_end = now + timedelta(days=days)
    user.rank = rank.upper()
    await db.execute(
        update(Agent).where(Agent.tg_id == str(tg_id)).values(rank=rank.upper())
    )
    await db.commit()
    print(f"[UPGRADE] {tg_id} → {rank.upper()} до {user.subscription_end}")
    return {"status": "ok", "rank": rank.upper(), "expires": user.subscription_end.isoformat()}


@router.post("/activate_trial")
async def activate_trial(
    tg_id: str, rank: str = "OVERSEER", days: int = 7,
    db: AsyncSession = Depends(get_db)
):
    res = await db.execute(select(User).where(User.tg_id == str(tg_id)))
    user = res.scalars().first()
    if not user:
        user = User(tg_id=str(tg_id), rank="FREE")
        db.add(user)
        await db.flush()
    if getattr(user, 'trial_used', False):
        raise HTTPException(status_code=409, detail="trial_already_used")
    user.rank = rank.upper()
    user.subscription_end = datetime.now(timezone.utc) + timedelta(days=days)
    user.trial_used = True
    await db.execute(
        update(Agent).where(Agent.tg_id == str(tg_id)).values(rank=rank.upper())
    )
    await db.commit()
    return {"status": "ok", "rank": rank.upper(), "days": days}


@router.post("/set_psu")
async def set_psu(tg_id: str, watts: int, db: AsyncSession = Depends(get_db)):
    if not (100 <= watts <= 3000):
        raise HTTPException(status_code=422, detail="watts must be 100-3000")
    res = await db.execute(select(User).where(User.tg_id == str(tg_id)))
    user = res.scalars().first()
    if not user:
        user = User(tg_id=str(tg_id), rank="FREE")
        db.add(user)
    user.psu_watts = watts
    await db.commit()
    return {"status": "ok", "watts": watts}


@router.get("/expiring_subscriptions")
async def expiring_subscriptions(db: AsyncSession = Depends(get_db)):
    now = datetime.now(timezone.utc)
    res = await db.execute(select(User).where(
        User.rank != "FREE",
        User.subscription_end.isnot(None),
        User.subscription_end > now,
        User.subscription_end <= now + timedelta(days=7),
    ))
    expiring = []
    for u in res.scalars().all():
        days_left = (u.subscription_end - now).days
        if days_left in (7, 3, 1):
            try:
                expiring.append({
                    "tg_id":     int(u.tg_id),
                    "end_date":  format_subscription_end(u.subscription_end),
                    "days_left": days_left,
                })
            except:
                pass
    return expiring


@router.post("/clear_stats")
async def clear_stats(hwid: str, tg_id: str, db: AsyncSession = Depends(get_db)):
    res = await db.execute(
        select(Agent).where(Agent.hwid == hwid, Agent.tg_id == str(tg_id))
    )
    agent = res.scalars().first()
    if not agent:
        raise HTTPException(status_code=404, detail="agent_not_found")
    rank = await get_user_rank(tg_id, db)
    if rank != "OVERSEER":
        raise HTTPException(status_code=403, detail="overseer_only")
    from sqlalchemy import delete as sa_delete
    await db.execute(sa_delete(Telemetry).where(Telemetry.agent_id == agent.id))
    await db.commit()
    return {"status": "ok"}


@router.post("/upload_screenshot/{hwid}")
async def upload_screenshot(
    hwid: str,
    file: UploadFile = File(...),
    db: AsyncSession = Depends(get_db)
):
    res = await db.execute(select(Agent).where(Agent.hwid == hwid))
    agent = res.scalars().first()
    if not agent:
        raise HTTPException(status_code=404, detail="agent_not_found")
    if not agent.tg_id:
        raise HTTPException(status_code=400, detail="no_tg_id_linked")

    MAX_SIZE = 50 * 1024 * 1024
    img = await file.read(MAX_SIZE + 1)
    if not img:
        raise HTTPException(status_code=400, detail="empty_file")
    if len(img) > MAX_SIZE:
        raise HTTPException(status_code=413, detail="file_too_large")

    async with httpx.AsyncClient() as client:
        try:
            r = await client.post(
                f"https://api.telegram.org/bot{BOT_TOKEN}/sendPhoto",
                data={"chat_id": agent.tg_id,
                      "caption": f"📸 Скриншот\n🖥 {agent.name}\n🆔 {agent.hwid}"},
                files={"photo": (file.filename, img, file.content_type)},
                timeout=30.0,
            )
            if r.status_code == 200:
                return {"status": "ok"}
            raise HTTPException(status_code=502, detail=f"tg_error: {r.text}")
        except HTTPException:
            raise
        except Exception as e:
            raise HTTPException(status_code=500, detail=str(e))