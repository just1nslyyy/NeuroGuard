# ═══════════════════════════════════════════════════════════
#  routers/telemetry.py
#  /telemetry — приём данных от агента
# ═══════════════════════════════════════════════════════════

import time
from fastapi import APIRouter, Depends, Request
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.future import select
from pydantic import BaseModel, validator

from ..database import get_db
from ..models import Agent, Telemetry, CoreTelemetry, DiskTelemetry
from ..dependencies import alert_cache, get_user_rank
from ..config import CPU_ALERT_TEMP, GPU_ALERT_TEMP, ALERT_COOLDOWN
from ..utils import send_telegram_alert

router = APIRouter(tags=["telemetry"])


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


@router.post("/telemetry")
async def receive_telemetry(
    request: Request,
    data: TelemetryData,
    db: AsyncSession = Depends(get_db)
):
    res = await db.execute(select(Agent).where(Agent.hwid == data.hwid))
    agent = res.scalars().first()
    if not agent:
        agent = Agent(hwid=data.hwid, name=f"Node_{data.hwid[:4]}", rank="FREE")
        db.add(agent)
        await db.flush()

    if data.cpu_model and data.cpu_model.strip():
        agent.cpu_model = data.cpu_model
    if data.gpu_model and data.gpu_model.strip():
        agent.gpu_model = data.gpu_model

    current_rank = "FREE"
    if agent.tg_id:
        current_rank = await get_user_rank(agent.tg_id, db)
        agent.rank = current_rank

    if agent.tg_id and current_rank != "FREE":
        await _check_alerts(agent, data, current_rank)

    new_log = Telemetry(
        agent_id      = agent.id,
        cpu_name      = data.cpu_model,
        gpu_name      = data.gpu_model,
        disks_info    = data.disk_model,
        cpu_avg_load  = data.cpu_load,
        cpu_temp      = data.cpu_temp,
        gpu_load      = data.gpu_load,
        gpu_temp      = data.gpu_temp,
        ram_total     = data.ram_total,
        ram_used      = data.ram_used,
        ram_percent   = int(data.ram_load),
        power_source  = data.power_source,
        battery_pct   = data.battery_pct,
        uptime        = data.uptime,
        net_down      = data.net_down,
        net_up        = data.net_up,
        top_processes = [p.dict() for p in data.top_processes],
    )
    db.add(new_log)
    await db.flush()

    for i, val in enumerate(data.cpu_cores):
        db.add(CoreTelemetry(
            telemetry_id=new_log.id, core_index=i, load=int(val)
        ))
    for d in data.disks:
        db.add(DiskTelemetry(
            telemetry_id=new_log.id, drive_name=d.drive_name,
            total_gb=d.total_gb, used_gb=d.used_gb, percent=d.percent,
        ))

    cmd = agent.pending_command
    if cmd:
        agent.pending_command = None
    await db.commit()

    return {
        "status":      "ok",
        "command":     cmd or "none",
        "rank":        current_rank.lower(),
        "cores_count": len(data.cpu_cores),
    }


async def _check_alerts(agent: Agent, data: TelemetryData, rank: str):
    now_t = time.time()
    hwid  = data.hwid

    # CPU алерт
    cpu_key = f"{hwid}_cpu"
    if data.cpu_temp >= CPU_ALERT_TEMP and now_t > alert_cache.get(cpu_key, 0):
        kb = [
            [{"text": "🔌 Выключить",    "callback_data": f"off_{hwid}"},
             {"text": "↩ Отменить выкл", "callback_data": f"revokecmd_{hwid}"}],
            [{"text": "🔇 Игнор (1ч)",   "callback_data": f"mute_1h_{hwid}"},
             {"text": "🗑 Закрыть",      "callback_data": "delete_msg"}],
        ]
        if rank == "OVERSEER":
            kb.insert(1, [{"text": "📸 Скриншот", "callback_data": f"scr_{hwid}"},
                          {"text": "🌙 Сон",       "callback_data": f"sleep_{hwid}"}])
            kb.insert(2, [{"text": "📊 Процессы", "callback_data": f"get_procs_{hwid}"}])
        await send_telegram_alert(
            agent.tg_id,
            f"🔥 <b>NeuroGuard: ПЕРЕГРЕВ ПРОЦЕССОРА</b>\n"
            f"───────────────────\n"
            f"🖥 <code>{agent.name}</code>\n"
            f"🌡 CPU: <code>{int(data.cpu_temp)}°C</code> | <code>{int(data.cpu_load)}%</code>\n"
            f"📟 RAM: <code>{int(data.ram_load)}%</code>\n"
            f"───────────────────\n"
            f"⚠️ <i>Температура процессора превысила критический порог.</i>",
            reply_markup={"inline_keyboard": kb}
        )
        alert_cache[cpu_key] = now_t + ALERT_COOLDOWN

    # GPU алерт
    gpu_key = f"{hwid}_gpu"
    if data.gpu_temp >= GPU_ALERT_TEMP and now_t > alert_cache.get(gpu_key, 0):
        kb = [
            [{"text": "🔌 Выключить",    "callback_data": f"off_{hwid}"},
             {"text": "↩ Отменить выкл", "callback_data": f"revokecmd_{hwid}"}],
            [{"text": "🔇 Игнор (1ч)",   "callback_data": f"mute_1h_{hwid}"},
             {"text": "🗑 Закрыть",      "callback_data": "delete_msg"}],
        ]
        if rank == "OVERSEER":
            kb.insert(1, [{"text": "📸 Скриншот", "callback_data": f"scr_{hwid}"},
                          {"text": "🌙 Сон",       "callback_data": f"sleep_{hwid}"}])
            kb.insert(2, [{"text": "📊 Процессы", "callback_data": f"get_procs_{hwid}"}])
        await send_telegram_alert(
            agent.tg_id,
            f"🎮 <b>NeuroGuard: ПЕРЕГРЕВ ВИДЕОКАРТЫ</b>\n"
            f"───────────────────\n"
            f"🖥 <code>{agent.name}</code>\n"
            f"🌡 GPU: <code>{int(data.gpu_temp)}°C</code> | <code>{int(data.gpu_load)}%</code>\n"
            f"───────────────────\n"
            f"⚠️ <i>Видеокарта перегрета.</i>",
            reply_markup={"inline_keyboard": kb}
        )
        alert_cache[gpu_key] = now_t + ALERT_COOLDOWN