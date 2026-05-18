from fastapi import FastAPI, Depends, HTTPException, File, UploadFile, Form, Request, Query
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.future import select
from sqlalchemy import func, update
from sqlalchemy.orm import selectinload
from contextlib import asynccontextmanager
from datetime import datetime, timezone, timedelta
from typing import Optional
import httpx
import time
import json
import os
import hashlib
import uuid
import base64
import asyncio
import hmac

from app.database import engine, Base, get_db
from app.models import Agent, Telemetry, CoreTelemetry, DiskTelemetry, User
from pydantic import BaseModel, validator
import random
from slowapi import Limiter, _rate_limit_exceeded_handler
from slowapi.util import get_remote_address
from slowapi.errors import RateLimitExceeded
from collections import defaultdict

BOT_TOKEN           = os.getenv("NEUROGUARD_BOT_TOKEN", "")
INTERNAL_API_TOKEN  = os.getenv("NEUROGUARD_INTERNAL_API_TOKEN", "")
YOOKASSA_SHOP_ID    = os.getenv("YOOKASSA_SHOP_ID", "")
YOOKASSA_SECRET_KEY = os.getenv("YOOKASSA_SECRET_KEY", "")

RANK_LIMITS: dict[str, int] = {"FREE": 1, "SENTINEL": 3, "OVERSEER": 10}
RANK_PRICES: dict[str, int] = {"SENTINEL": 29900, "OVERSEER": 49900} 

alert_cache:    dict[str, float] = {}
auth_code_meta: dict[str, dict]  = {}
CODE_TTL_SECONDS  = 180
CODE_MAX_ATTEMPTS = 5

auth_attempts_lock = asyncio.Lock()
auth_attempts: dict[str, int] = defaultdict(int)

if not BOT_TOKEN:
    raise RuntimeError("NEUROGUARD_BOT_TOKEN is required")
if not INTERNAL_API_TOKEN:
    raise RuntimeError("NEUROGUARD_INTERNAL_API_TOKEN is required")

@asynccontextmanager
async def lifespan(app: FastAPI):
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    print("--- NeuroGuard: База готова ---")

    async def _sentinel_cleanup():
        while True:
            await asyncio.sleep(86400)
            try:
                async for db in get_db():
                    await auto_clear_sentinel_stats(db)
            except Exception as e:
                print(f"[AUTO_CLEAN ERROR] {e}")

    async def _code_cleanup():
        while True:
            await asyncio.sleep(300)
            now = time.time()
            expired = [c for c, m in auth_code_meta.items()
                       if now - m["created_at"] > CODE_TTL_SECONDS * 2]
            for c in expired:
                auth_code_meta.pop(c, None)
            if expired:
                print(f"[CLEANUP] Удалено {len(expired)} устаревших кодов")

    t1 = asyncio.create_task(_sentinel_cleanup())
    t2 = asyncio.create_task(_code_cleanup())
    yield
    t1.cancel(); t2.cancel()


app = FastAPI(title="NeuroGuard API", lifespan=lifespan)

limiter = Limiter(key_func=get_remote_address, default_limits=["200/minute"])
app.state.limiter = limiter
app.add_exception_handler(RateLimitExceeded, _rate_limit_exceeded_handler)

class DiskInfo(BaseModel):
    drive_name: str
    total_gb:   float
    used_gb:    float
    percent:    float           

class ProcessInfo(BaseModel):
    name: str
    pid:  int
    cpu:  float
    ram:  float

class TelemetryData(BaseModel):
    hwid:         str
    cpu_load:     float
    cpu_temp:     float
    cpu_cores:    list[float]
    cpu_model:    str
    gpu_load:     float
    gpu_temp:     float
    gpu_model:    str
    ram_load:     float
    ram_used:     float
    ram_total:    float
    power_source: str
    battery_pct:  int           
    uptime:       int
    disk_model:   str
    disks:        list[DiskInfo]
    net_down:     float = 0.0
    net_up:       float = 0.0
    top_processes: list[ProcessInfo] = []

    cpu_temp_avg: float = 0.0
    cpu_temp_max: float = 0.0
    gpu_temp_avg: float = 0.0
    gpu_temp_max: float = 0.0
    session_uptime:  float = 0.0
    estimated_kwh:   float = 0.0

    @validator('hwid')
    def hwid_valid(cls, v):
        if not v or len(v) < 5:
            raise ValueError('HWID слишком короткий')
        return v
    
def get_system_status(cpu: float, ram: float, cpu_temp: float, gpu_temp: float = 0.0) -> str:
    """Только для отображения статуса в боте — без изменений"""
    if cpu == 0.0 and ram == 0 and cpu_temp == 0.0 and gpu_temp == 0.0:
        return "🟢 СИСТЕМА СТАБИЛЬНА"
    if cpu > 80 or ram > 90 or (cpu_temp > 60 and cpu_temp > 0) or (gpu_temp > 60 and gpu_temp > 0):
        return "🔴 КРИТИЧЕСКИЙ УРОВЕНЬ"
    if cpu > 60 or ram > 75 or (cpu_temp > 50 and cpu_temp > 0) or (gpu_temp > 50 and gpu_temp > 0):
        return "🟡 ВЫСОКАЯ НАГРУЗКА"
    return "🟢 СИСТЕМА СТАБИЛЬНА"

def format_subscription_end(dt: datetime | None) -> str | None:
    if dt is None: return None
    return (dt + timedelta(hours=3)).strftime("%d.%m.%Y")

def is_valid_tg_id(tg_id: str) -> bool:
    try: return 0 < int(tg_id) < 10_000_000_000
    except: return False

def require_internal_token(api_token: str | None) -> None:
    if not api_token or api_token != INTERNAL_API_TOKEN:
        raise HTTPException(status_code=401, detail="unauthorized")

async def send_telegram_alert(tg_id: str, text: str,
                               reply_markup: Optional[dict] = None) -> bool:
    if not is_valid_tg_id(tg_id): return False
    url = f"https://api.telegram.org/bot{BOT_TOKEN}/sendMessage"
    payload = {"chat_id": tg_id, "text": text, "parse_mode": "HTML"}
    if reply_markup:
        payload["reply_markup"] = json.dumps(reply_markup)
    async with httpx.AsyncClient(timeout=10.0) as client:
        try:
            r = await client.post(url, json=payload)
            return r.status_code == 200
        except Exception as e:
            print(f"[TG ERROR] {e}"); return False

async def send_telegram_message(tg_id: str, text: str) -> bool:
    return await send_telegram_alert(tg_id, text)

async def get_user_rank(tg_id: str, db: AsyncSession) -> str:
    res = await db.execute(select(User).where(User.tg_id == str(tg_id)))
    user = res.scalars().first()
    if not user:
        user = User(tg_id=str(tg_id), rank="FREE")
        db.add(user); await db.commit(); return "FREE"
    if (user.subscription_end and user.rank != "FREE"
            and user.subscription_end < datetime.now(timezone.utc)):
        user.rank = "FREE"
        await db.execute(update(Agent).where(Agent.tg_id == str(tg_id)).values(rank="FREE"))
        await db.commit()
        print(f"[EXPIRED] {tg_id} → FREE")
    return user.rank.upper()

async def get_user_subscription_end(tg_id: str, db: AsyncSession) -> datetime | None:
    res = await db.execute(select(User).where(User.tg_id == str(tg_id)))
    u = res.scalars().first()
    return u.subscription_end if u else None

async def enforce_device_limit(tg_id: str, rank: str, db: AsyncSession):
    limit = RANK_LIMITS.get(rank.upper(), 1)
    res = await db.execute(
        select(Agent).where(Agent.tg_id == str(tg_id)).order_by(Agent.id.asc())
    )
    agents = res.scalars().all()
    for a in agents[limit:]:
        print(f"[CLEANUP] Удаляем лишнее устройство: {a.hwid}")
        await db.delete(a)
    if len(agents) > limit:
        await db.commit()

async def auto_clear_sentinel_stats(db: AsyncSession):
    cutoff = datetime.now(timezone.utc) - timedelta(days=7)
    res = await db.execute(
        select(Agent).join(User, Agent.tg_id == User.tg_id).where(User.rank == "SENTINEL")
    )
    agents = res.scalars().all()
    from sqlalchemy import delete as sa_delete
    deleted = 0
    for agent in agents:
        r = await db.execute(
            sa_delete(Telemetry).where(
                Telemetry.agent_id == agent.id,
                Telemetry.created_at < cutoff
            )
        )
        deleted += r.rowcount
    await db.commit()
    if deleted: print(f"[AUTO_CLEAN] SENTINEL: удалено {deleted} записей")


@app.post("/telemetry")
@limiter.limit("30/second")
async def receive_telemetry(request: Request, data: TelemetryData,
                             db: AsyncSession = Depends(get_db)):
    res = await db.execute(select(Agent).where(Agent.hwid == data.hwid))
    agent = res.scalars().first()
    if not agent:
        agent = Agent(hwid=data.hwid, name=f"Node_{data.hwid[:4]}", rank="FREE")
        db.add(agent); await db.flush()

    if data.cpu_model and data.cpu_model.strip():
        agent.cpu_model = data.cpu_model
    if data.gpu_model and data.gpu_model.strip():
        agent.gpu_model = data.gpu_model

    current_rank = "FREE"
    if agent.tg_id:
        current_rank = await get_user_rank(agent.tg_id, db)
        agent.rank = current_rank

    if agent.tg_id and current_rank != "FREE":
        now_t = time.time()
        cpu_temp = data.cpu_temp
        gpu_temp = data.gpu_temp
        hwid = data.hwid

        cpu_key = f"{hwid}_cpu"
        if cpu_temp >= 90.0 and now_t > alert_cache.get(cpu_key, 0):
            alert_text = (
                f"🔥 <b>NeuroGuard: ПЕРЕГРЕВ ПРОЦЕССОРА</b>\n"
                f"───────────────────\n"
                f"🖥 <code>{agent.name}</code>\n"
                f"🌡 CPU: <code>{int(cpu_temp)}°C</code> | <code>{int(data.cpu_load)}%</code>\n"
                f"📟 RAM: <code>{int(data.ram_load)}%</code>\n"
                f"───────────────────\n"
                f"⚠️ <i>Температура процессора превысила критический порог.\n"
                f"ПК может выключиться без сохранения данных.</i>"
            )
            kb = [
                [{"text": "🔌 Выключить", "callback_data": f"off_{hwid}"},
                 {"text": "↩ Отменить выкл", "callback_data": f"revokecmd_{hwid}"}],
                [{"text": "🔇 Игнор (1ч)", "callback_data": f"mute_1h_{hwid}"},
                 {"text": "🗑 Закрыть", "callback_data": "delete_msg"}],
            ]
            if current_rank == "OVERSEER":
                kb.insert(1, [{"text": "📸 Скриншот", "callback_data": f"scr_{hwid}"},
                              {"text": "🌙 Сон", "callback_data": f"sleep_{hwid}"}])
                kb.insert(2, [{"text": "📊 Процессы", "callback_data": f"get_procs_{hwid}"}])
            await send_telegram_alert(agent.tg_id, alert_text,
                                      reply_markup={"inline_keyboard": kb})
            alert_cache[cpu_key] = now_t + 300

        gpu_key = f"{hwid}_gpu"
        if gpu_temp >= 85.0 and now_t > alert_cache.get(gpu_key, 0):
            alert_text = (
                f"🎮 <b>NeuroGuard: ПЕРЕГРЕВ ВИДЕОКАРТЫ</b>\n"
                f"───────────────────\n"
                f"🖥 <code>{agent.name}</code>\n"
                f"🌡 GPU: <code>{int(gpu_temp)}°C</code> | <code>{int(data.gpu_load)}%</code>\n"
                f"───────────────────\n"
                f"⚠️ <i>Видеокарта перегрета.\n"
                f"Возможен вылет драйвера или отключение ПК.</i>"
            )
            kb = [
                [{"text": "🔌 Выключить", "callback_data": f"off_{hwid}"},
                 {"text": "↩ Отменить выкл", "callback_data": f"revokecmd_{hwid}"}],
                [{"text": "🔇 Игнор (1ч)", "callback_data": f"mute_1h_{hwid}"},
                 {"text": "🗑 Закрыть", "callback_data": "delete_msg"}],
            ]
            if current_rank == "OVERSEER":
                kb.insert(1, [{"text": "📸 Скриншот", "callback_data": f"scr_{hwid}"},
                              {"text": "🌙 Сон", "callback_data": f"sleep_{hwid}"}])
                kb.insert(2, [{"text": "📊 Процессы", "callback_data": f"get_procs_{hwid}"}])
            await send_telegram_alert(agent.tg_id, alert_text,
                                      reply_markup={"inline_keyboard": kb})
            alert_cache[gpu_key] = now_t + 300

    new_log = Telemetry(
        agent_id     = agent.id,
        cpu_name     = data.cpu_model,
        gpu_name     = data.gpu_model,
        disks_info   = data.disk_model,
        cpu_avg_load = data.cpu_load,
        cpu_temp     = data.cpu_temp,
        gpu_load     = data.gpu_load,
        gpu_temp     = data.gpu_temp,
        ram_total    = data.ram_total,
        ram_used     = data.ram_used,
        ram_percent  = int(data.ram_load),
        power_source = data.power_source,
        battery_pct  = data.battery_pct,
        uptime       = data.uptime,
        net_down     = data.net_down,
        net_up       = data.net_up,
        top_processes= [p.dict() for p in data.top_processes],
    )
    db.add(new_log); await db.flush()

    for i, val in enumerate(data.cpu_cores):
        db.add(CoreTelemetry(telemetry_id=new_log.id, core_index=i, load=int(val)))
    for d in data.disks:
        db.add(DiskTelemetry(
            telemetry_id=new_log.id, drive_name=d.drive_name,
            total_gb=d.total_gb, used_gb=d.used_gb, percent=d.percent,
        ))

    cmd = agent.pending_command
    if cmd: agent.pending_command = None
    await db.commit()

    return {
        "status": "ok",
        "command": cmd or "none",
        "rank": current_rank.lower(),
        "cores_count": len(data.cpu_cores),
    }

@app.get("/bot/status")
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
        is_online = False; status_text = "🔴 ОФФЛАЙН"; last_seen = "N/A"

        if last:
            ts = last.created_at
            if ts.tzinfo is None: ts = ts.replace(tzinfo=timezone.utc)
            if now_utc - ts < timedelta(seconds=40):
                is_online = True
                status_text = get_system_status(
                    last.cpu_avg_load or 0.0, last.ram_percent or 0,
                    last.cpu_temp or 0.0, last.gpu_temp or 0.0)
            last_seen = (ts + timedelta(hours=3)).strftime("%H:%M:%S")

        cpu_name = (last.cpu_name if last and last.cpu_name else None) or agent.cpu_model or "N/A"
        gpu_name = (last.gpu_name if last and last.gpu_name else None) or agent.gpu_model or "N/A"

        devices_list.append({
            "hwid": agent.hwid or "N/A",
            "name": agent.name or "Device",
            "rank": current_rank,
            "status_label": status_text,
            "online": is_online,
            "last_seen": last_seen,
            "last_seen_time": last_seen,
            "subscription_end": sub_end_str,
            "psu_watts": psu_watts_val,
            "cpu":      last.cpu_avg_load if last else 0.0,
            "cpu_temp": last.cpu_temp     if last else 0.0,
            "gpu":      last.gpu_load     if last else 0.0,
            "gpu_temp": last.gpu_temp     if last else 0.0,
            "ram":      last.ram_percent  if last else 0,
            "ram_used": last.ram_used     if last else 0.0,
            "ram_total":last.ram_total    if last else 0.0,
            "net_down": last.net_down     if last else 0.0,
            "net_up":   last.net_up       if last else 0.0,
            "cpu_name":    cpu_name,
            "gpu_name":    gpu_name,
            "disk_name":   last.disks_info   if last else "N/A",
            "power_source":last.power_source if last else "AC",
            "battery_pct": last.battery_pct  if last else 100,
            "uptime":      last.uptime        if last else 0,
            "top_processes": last.top_processes if last else [],
            "cores": [c.load for c in sorted(last.cores, key=lambda x: x.core_index)] if last else [],
            "disks": [
                {"name": d.drive_name, "total": d.total_gb, "used": d.used_gb, "percent": d.percent}
                for d in last.disks
            ] if last else [],
        })

    return {
        "user_rank": current_rank,
        "subscription_end": sub_end_str,
        "psu_watts": psu_watts_val,
        "devices": devices_list,
    }

@app.get("/bot/status/{hwid}")
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
    if not agent: raise HTTPException(status_code=404, detail="agent_not_found")

    since = datetime.now(timezone.utc) - timedelta(days=7)
    
    res_tel = await db.execute(
        select(
            Telemetry.id,
            Telemetry.cpu_temp,
            Telemetry.cpu_avg_load,
            Telemetry.ram_percent,
            Telemetry.gpu_load,
            Telemetry.gpu_temp,
            Telemetry.net_down,
            Telemetry.net_up,
            Telemetry.created_at,
            Telemetry.cpu_name,
            Telemetry.gpu_name,
            Telemetry.top_processes,
        )
        .where(Telemetry.agent_id == agent.id, Telemetry.created_at >= since)
        .order_by(Telemetry.created_at.asc())
    )
    rows = res_tel.all()
    if not rows: return {"status": "no_data"}

    cpu_temps = [r.cpu_temp  for r in rows if r.cpu_temp  and r.cpu_temp  > 0]
    cpu_loads = [r.cpu_avg_load for r in rows if r.cpu_avg_load is not None]
    ram_loads = [r.ram_percent  for r in rows if r.ram_percent  is not None]
    gpu_loads = [r.gpu_load  for r in rows if r.gpu_load  is not None]
    gpu_temps = [r.gpu_temp  for r in rows if r.gpu_temp  and r.gpu_temp  > 0]
    net_downs = [r.net_down  for r in rows if r.net_down  is not None]
    net_ups   = [r.net_up    for r in rows if r.net_up    is not None]

    def avg(v): return sum(v)/len(v) if v else 0.0
    def mx(v):  return max(v) if v else 0.0

    peak_row = max(rows, key=lambda r: r.cpu_temp or 0)
    peak_ts  = peak_row.created_at
    if peak_ts.tzinfo is None: peak_ts = peak_ts.replace(tzinfo=timezone.utc)

    online_seconds = len(rows) * 3
    oh, om = online_seconds // 3600, (online_seconds % 3600) // 60
    online_str = f"{oh}ч {om}м" if oh > 0 else f"{om}м"

    last  = rows[-1]
    first = rows[0]
    cpu_name = (last.cpu_name if last.cpu_name else None) or agent.cpu_model or "N/A"
    gpu_name = (last.gpu_name if last.gpu_name else None) or agent.gpu_model or "N/A"

    last_tel_id = last.id
    res_disks = await db.execute(
        select(DiskTelemetry).where(DiskTelemetry.telemetry_id == last_tel_id)
    )
    last_disks = res_disks.scalars().all()

    disk_data = [{
        "name": d.drive_name, "total": d.total_gb, "used": d.used_gb, "percent": d.percent,
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

    def fmt_date(dt): return (dt + timedelta(hours=3)).strftime("%d.%m.%Y") if dt else "N/A"

    return {
        "status": "ready",
        "agent_name":   agent.name,
        "period_from":  fmt_date(first.created_at),
        "period_to":    fmt_date(last.created_at),
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
        "net_up_avg":   round(avg(net_ups), 2),
        "peak_temp_time": (peak_ts + timedelta(hours=3)).strftime("%d.%m %H:%M"),
        "disks": disk_data,
        "top_processes_week": top_procs_week,
        "estimated_kwh":  estimated_kwh,
        "estimated_cost": estimated_cost,
        "psu_watts":      psu_watts_val,
        "subscription_end": sub_end_str,
    }
@app.post("/auth/generate/{hwid}")
async def generate_code(hwid: str, db: AsyncSession = Depends(get_db)):
    res = await db.execute(select(Agent).where(Agent.hwid == hwid))
    agent = res.scalar_one_or_none()
    if agent and agent.tg_id:
        return {"status": "already_linked", "tg_id": agent.tg_id}
    code = str(random.randint(100000, 999999))
    if not agent:
        agent = Agent(hwid=hwid, name="New Device"); db.add(agent)
    agent.auth_code = code
    auth_code_meta[code] = {"created_at": time.time(), "attempts": 0, "hwid": hwid}
    await db.commit()
    return {"code": code}

@app.post("/auth/verify")
async def verify_code(code: str, tg_id: str, request: Request,
                       db: AsyncSession = Depends(get_db)):
    client_ip = request.client.host
    async with auth_attempts_lock:
        if auth_attempts[client_ip] > 10:
            raise HTTPException(status_code=429, detail="too_many_attempts")
        auth_attempts[client_ip] += 1

    meta = auth_code_meta.get(code)
    if not meta: raise HTTPException(status_code=404, detail="invalid_code")
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

    agent.tg_id = str(tg_id); agent.rank = current_rank; agent.auth_code = None
    auth_code_meta.pop(code, None)
    await db.commit()
    return {"status": "success", "rank_granted": current_rank, "devices_total": count + 1}

@app.post("/auth/update_name")
async def update_name(tg_id: str, name: str, hwid: Optional[str] = None,
                       db: AsyncSession = Depends(get_db)):
    q = select(Agent).where(Agent.tg_id == str(tg_id))
    if hwid: q = q.where(Agent.hwid == hwid)
    else:    q = q.order_by(Agent.id.desc())
    res = await db.execute(q)
    agent = res.scalars().first()
    if not agent: raise HTTPException(status_code=404, detail="agent_not_found")
    agent.name = name; await db.commit()
    return {"status": "ok", "name": name, "hwid": agent.hwid}

@app.post("/auth/delete_device")
async def delete_device(tg_id: str, hwid: str, db: AsyncSession = Depends(get_db)):
    res = await db.execute(select(Agent).where(Agent.tg_id == str(tg_id), Agent.hwid == hwid))
    agent = res.scalars().first()
    if not agent: raise HTTPException(status_code=404, detail="device_not_found")
    agent.tg_id = None; await db.commit()
    return {"status": "ok"}

@app.post("/bot/set_command")
async def set_command(hwid: str, command: str, tg_id: str,
                       db: AsyncSession = Depends(get_db)):
    valid = {"shutdown","sleep","screenshot","cancel_shutdown","kill_"}
    if not any(command.startswith(c) for c in valid):
        raise HTTPException(status_code=400, detail="invalid_command")
    res = await db.execute(select(Agent).where(Agent.hwid == hwid))
    agent = res.scalars().first()
    if not agent: raise HTTPException(status_code=404, detail="agent_not_found")
    if str(agent.tg_id or "") != str(tg_id):
        raise HTTPException(status_code=403, detail="forbidden")
    agent.pending_command = command; await db.commit()
    return {"status": "ok", "command": command, "hwid": agent.hwid}

@app.post("/bot/mute")
async def mute_alerts(hwid: str, tg_id: str, hours: int = 1,
                       db: AsyncSession = Depends(get_db)):
    res = await db.execute(select(Agent).where(Agent.hwid == hwid))
    agent = res.scalars().first()
    if not agent: raise HTTPException(status_code=404, detail="agent_not_found")
    if str(agent.tg_id or "") != str(tg_id):
        raise HTTPException(status_code=403, detail="forbidden")
    mute_until = time.time() + hours * 3600
    alert_cache[f"{hwid}_cpu"] = mute_until
    alert_cache[f"{hwid}_gpu"] = mute_until
    return {"status": "ok"}

@app.post("/bot/upgrade")
async def upgrade_rank(tg_id: str = Form(...), rank: str = Form(...),
                        days: int = Form(30), api_token: str | None = Form(None),
                        db: AsyncSession = Depends(get_db)):
    require_internal_token(api_token)
    res = await db.execute(select(User).where(User.tg_id == str(tg_id)))
    user = res.scalars().first()
    if not user:
        user = User(tg_id=str(tg_id)); db.add(user); await db.flush()

    now = datetime.now(timezone.utc)
    if user.subscription_end and user.subscription_end > now and user.rank == rank.upper():
        user.subscription_end = user.subscription_end + timedelta(days=days)
    else:
        user.subscription_end = now + timedelta(days=days)

    user.rank = rank.upper()
    await db.execute(update(Agent).where(Agent.tg_id == str(tg_id)).values(rank=rank.upper()))
    await db.commit()
    print(f"[UPGRADE] {tg_id} → {rank.upper()} до {user.subscription_end}")
    return {"status": "ok", "rank": rank.upper(), "expires": user.subscription_end.isoformat()}

@app.post("/bot/activate_trial")
async def activate_trial(tg_id: str, rank: str = "OVERSEER", days: int = 7,
                          db: AsyncSession = Depends(get_db)):
    res = await db.execute(select(User).where(User.tg_id == str(tg_id)))
    user = res.scalars().first()
    if not user:
        user = User(tg_id=str(tg_id), rank="FREE"); db.add(user); await db.flush()
    if getattr(user, 'trial_used', False):
        raise HTTPException(status_code=409, detail="trial_already_used")
    user.rank = rank.upper()
    user.subscription_end = datetime.now(timezone.utc) + timedelta(days=days)
    user.trial_used = True
    await db.execute(update(Agent).where(Agent.tg_id == str(tg_id)).values(rank=rank.upper()))
    await db.commit()
    return {"status": "ok", "rank": rank.upper(), "days": days}

@app.post("/bot/set_psu")
async def set_psu(tg_id: str, watts: int, db: AsyncSession = Depends(get_db)):
    if not (100 <= watts <= 3000):
        raise HTTPException(status_code=422, detail="watts must be 100-3000")
    res = await db.execute(select(User).where(User.tg_id == str(tg_id)))
    user = res.scalars().first()
    if not user:
        user = User(tg_id=str(tg_id), rank="FREE"); db.add(user)
    user.psu_watts = watts; await db.commit()
    return {"status": "ok", "watts": watts}

@app.get("/bot/expiring_subscriptions")
async def expiring_subscriptions(db: AsyncSession = Depends(get_db)):
    now = datetime.now(timezone.utc)
    res = await db.execute(select(User).where(
        User.rank != "FREE", User.subscription_end.isnot(None),
        User.subscription_end > now, User.subscription_end <= now + timedelta(days=7)
    ))
    expiring = []
    for u in res.scalars().all():
        days_left = (u.subscription_end - now).days
        if days_left in (7, 3, 1):
            try: expiring.append({"tg_id": int(u.tg_id),
                                   "end_date": format_subscription_end(u.subscription_end),
                                   "days_left": days_left})
            except: pass
    return expiring

@app.post("/bot/clear_stats")
async def clear_stats(hwid: str, tg_id: str, db: AsyncSession = Depends(get_db)):
    res = await db.execute(select(Agent).where(Agent.hwid == hwid, Agent.tg_id == str(tg_id)))
    agent = res.scalars().first()
    if not agent: raise HTTPException(status_code=404, detail="agent_not_found")
    rank = await get_user_rank(tg_id, db)
    if rank != "OVERSEER": raise HTTPException(status_code=403, detail="overseer_only")
    from sqlalchemy import delete as sa_delete
    await db.execute(sa_delete(Telemetry).where(Telemetry.agent_id == agent.id))
    await db.commit()
    return {"status": "ok"}

@app.post("/upload_screenshot/{hwid}")
async def upload_screenshot(hwid: str, file: UploadFile = File(...),
                             db: AsyncSession = Depends(get_db)):
    res = await db.execute(select(Agent).where(Agent.hwid == hwid))
    agent = res.scalars().first()
    if not agent: raise HTTPException(status_code=404, detail="agent_not_found")
    if not agent.tg_id: raise HTTPException(status_code=400, detail="no_tg_id_linked")

    MAX_SIZE = 50 * 1024 * 1024
    img = await file.read(MAX_SIZE + 1)
    if not img: raise HTTPException(status_code=400, detail="empty_file")
    if len(img) > MAX_SIZE: raise HTTPException(status_code=413, detail="file_too_large")

    async with httpx.AsyncClient() as client:
        try:
            r = await client.post(
                f"https://api.telegram.org/bot{BOT_TOKEN}/sendPhoto",
                data={"chat_id": agent.tg_id,
                      "caption": f"📸 Скриншот\n🖥 {agent.name}\n🆔 {agent.hwid}"},
                files={"photo": (file.filename, img, file.content_type)},
                timeout=30.0)
            if r.status_code == 200: return {"status": "ok"}
            raise HTTPException(status_code=502, detail=f"tg_error: {r.text}")
        except HTTPException: raise
        except Exception as e: raise HTTPException(status_code=500, detail=str(e))

@app.post("/create_payment")
async def create_payment(tg_id: str, rank: str):
    if rank not in RANK_PRICES:
        raise HTTPException(status_code=400, detail="invalid_rank")
    if not YOOKASSA_SHOP_ID or not YOOKASSA_SECRET_KEY:
        raise HTTPException(status_code=503, detail="yookassa_not_configured")

    amount_rub = str(RANK_PRICES[rank] / 100)
    idempotence_key = str(uuid.uuid4())

    payload = {
        "amount":      {"value": amount_rub, "currency": "RUB"},
        "payment_method_data": {"type": "bank_card"},
        "confirmation":{"type": "redirect", "return_url": "https://t.me/NeuroGuardPcBot"},
        "description": f"NeuroGuard {rank} 30 дней — tg_id {tg_id}",
        "metadata":    {"tg_id": tg_id, "rank": rank},
        "capture":     True,
    }
    auth = base64.b64encode(f"{YOOKASSA_SHOP_ID}:{YOOKASSA_SECRET_KEY}".encode()).decode()

    async with httpx.AsyncClient(timeout=15.0) as client:
        r = await client.post(
            "https://api.yookassa.ru/v3/payments",
            json=payload,
            headers={"Content-Type": "application/json",
                     "Authorization": f"Basic {auth}",
                     "Idempotence-Key": idempotence_key},
        )
    if r.status_code not in (200, 201):
        print(f"[YOOKASSA ERROR] {r.status_code}: {r.text}")
        raise HTTPException(status_code=502, detail="yookassa_error")

    result = r.json()
    return {
        "payment_url": result["confirmation"]["confirmation_url"],
        "payment_id":  result["id"],
    }

@app.post("/yookassa_webhook")
async def yookassa_webhook(request: Request, db: AsyncSession = Depends(get_db)):
    body = await request.body()
    YOOKASSA_IPS = {
        "185.71.76.0", "185.71.77.0", "77.75.153.0",
        "77.75.156.11", "77.75.156.35",
        "127.0.0.1", "89.127.211.218",
    }

    client_ip = request.client.host

    if YOOKASSA_SECRET_KEY and YOOKASSA_SECRET_KEY != "test":
        if client_ip not in YOOKASSA_IPS:
            print(f"[YOOKASSA] Запрос с незнакомого IP: {client_ip} — пропускаем, но логируем")

    try:
        data = json.loads(body)
    except Exception:
        raise HTTPException(status_code=400, detail="invalid_json")

    event = data.get("event", "")
    print(f"[YOOKASSA] IP={client_ip} Событие: {event}")

    if event == "payment.succeeded":
        payment  = data.get("object", {})
        metadata = payment.get("metadata", {})
        tg_id    = metadata.get("tg_id", "")
        rank     = metadata.get("rank", "")

        if not tg_id or not rank:
            print(f"[YOOKASSA] Нет tg_id или rank в metadata: {metadata}")
            return {"status": "ok"}

        rank = rank.upper()
        if rank not in RANK_LIMITS:
            print(f"[YOOKASSA] Неизвестный ранг: {rank}")
            return {"status": "ok"}

        res = await db.execute(select(User).where(User.tg_id == str(tg_id)))
        user = res.scalars().first()
        if not user:
            user = User(tg_id=str(tg_id)); db.add(user); await db.flush()

        now = datetime.now(timezone.utc)
        days = 30

        if user.subscription_end and user.subscription_end > now and user.rank == rank:
            user.subscription_end = user.subscription_end + timedelta(days=days)
        else:
            user.subscription_end = now + timedelta(days=days)

        user.rank = rank
        await db.execute(update(Agent).where(Agent.tg_id == str(tg_id)).values(rank=rank))
        await db.commit()

        end_str = format_subscription_end(user.subscription_end)
        print(f"[YOOKASSA] ✅ {tg_id} → {rank} до {end_str}")

        emoji = "👑" if rank == "OVERSEER" else "🛡"
        await send_telegram_message(tg_id,
            f"{emoji} <b>Подписка {rank} активирована!</b>\n\n"
            f"📅 Действует до: <b>{end_str}</b>\n\n"
            f"Используй /download чтобы получить агента."
        )

    elif event == "payment.canceled":
        payment  = data.get("object", {})
        metadata = payment.get("metadata", {})
        tg_id    = metadata.get("tg_id", "")
        if tg_id and is_valid_tg_id(tg_id):
            await send_telegram_message(tg_id,
                "❌ <b>Платёж отменён.</b>\n\nЕсли возникли проблемы — напишите @TLEET_BLANT"
            )

    return {"status": "ok"}

@app.post("/register/{hwid}")
async def register_agent(hwid: str, name: str = "Unknown",
                          db: AsyncSession = Depends(get_db)):
    res = await db.execute(select(Agent).where(Agent.hwid == hwid))
    agent = res.scalar_one_or_none()
    if agent: return {"status": "exists", "agent_id": agent.id}
    new_agent = Agent(hwid=hwid, name=name)
    db.add(new_agent); await db.commit(); await db.refresh(new_agent)
    return {"status": "created", "agent_id": new_agent.id}


from datetime import datetime, timezone, timedelta
from fastapi import Header

ADMIN_TG_ID = "8362261813"

def require_admin_or_internal(api_token: str | None = None) -> None:
    """Проверка токена для админских эндпоинтов"""
    if not api_token or api_token != INTERNAL_API_TOKEN:
        raise HTTPException(status_code=401, detail="unauthorized")

@app.get("/admin/stats")
async def admin_stats(
    api_token: str | None = Header(None, alias="api-token"),
    db: AsyncSession = Depends(get_db)
):
    require_admin_or_internal(api_token)

    total_users = (await db.execute(select(func.count(User.id)))).scalar() or 0

    free_count = (await db.execute(
        select(func.count(User.id)).where(User.rank == "FREE")
    )).scalar() or 0

    sentinel_count = (await db.execute(
        select(func.count(User.id)).where(User.rank == "SENTINEL")
    )).scalar() or 0

    overseer_count = (await db.execute(
        select(func.count(User.id)).where(User.rank == "OVERSEER")
    )).scalar() or 0

    cutoff = datetime.now(timezone.utc) - timedelta(seconds=40)
    online_agents = (await db.execute(
        select(func.count(func.distinct(Telemetry.agent_id)))
        .where(Telemetry.created_at >= cutoff)
    )).scalar() or 0

    hanging_agents = (await db.execute(
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

@app.get("/admin/users")
async def admin_users(
    page: int = 0,
    api_token: str | None = Header(None, alias="api-token"),
    token_query: str | None = Query(None, alias="api_token"),
    db: AsyncSession = Depends(get_db)
):
    require_admin_or_internal(api_token)

    PAGE_SIZE = 10
    offset = page * PAGE_SIZE

    total = (await db.execute(select(func.count(User.id)))).scalar() or 0

    res = await db.execute(
        select(User)
        .order_by(User.id.desc())
        .offset(offset)
        .limit(PAGE_SIZE)
    )
    users = res.scalars().all()

    result = []
    for u in users:
        sub_end = None
        if u.subscription_end:
            sub_end = (u.subscription_end + timedelta(hours=3)).strftime("%d.%m.%Y")

        result.append({
            "tg_id":             u.tg_id,
            "rank":              u.rank,
            "subscription_end":  sub_end,
            "trial_used":        getattr(u, "trial_used", False),
        })

    return {"users": result, "total": total, "page": page}



@app.get("/admin/agents")
async def admin_agents(
    page: int = 0,
    api_token: str | None = Header(None, alias="api-token"),
    token_query: str | None = Query(None, alias="api_token"),
    db: AsyncSession = Depends(get_db)
):
    require_admin_or_internal(api_token)

    PAGE_SIZE = 10
    offset = page * PAGE_SIZE

    total = (await db.execute(select(func.count(Agent.id)))).scalar() or 0

    res = await db.execute(
        select(Agent)
        .order_by(Agent.id.desc())
        .offset(offset)
        .limit(PAGE_SIZE)
    )
    agents = res.scalars().all()

    now_utc = datetime.now(timezone.utc)
    result = []

    for a in agents:
        tel_res = await db.execute(
            select(Telemetry)
            .where(Telemetry.agent_id == a.id)
            .order_by(Telemetry.created_at.desc())
            .limit(1)
        )
        last = tel_res.scalars().first()

        online = False
        last_seen = "никогда"
        if last:
            ts = last.created_at
            if ts.tzinfo is None:
                ts = ts.replace(tzinfo=timezone.utc)
            online = (now_utc - ts) < timedelta(seconds=40)
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

@app.get("/admin/all_user_ids")
async def admin_all_user_ids(
    api_token: str | None = Header(None, alias="api-token"),
    token_query: str | None = Query(None, alias="api_token"),
    db: AsyncSession = Depends(get_db)
):
    require_admin_or_internal(api_token)

    res = await db.execute(select(User.tg_id))
    ids = [row[0] for row in res.all() if row[0]]
    return {"ids": ids, "count": len(ids)}