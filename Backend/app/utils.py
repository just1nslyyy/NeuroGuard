# ═══════════════════════════════════════════════════════════
#  utils.py
#  Вспомогательные функции
# ═══════════════════════════════════════════════════════════

import json
import httpx
from datetime import datetime, timedelta, timezone
from typing import Optional
from .config import BOT_TOKEN, TIMEZONE_OFFSET


# ── Telegram ─────────────────────────────────────────────────
async def send_telegram_alert(
    tg_id: str,
    text: str,
    reply_markup: Optional[dict] = None
) -> bool:
    if not is_valid_tg_id(tg_id):
        return False
    url = f"https://api.telegram.org/bot{BOT_TOKEN}/sendMessage"
    payload = {"chat_id": tg_id, "text": text, "parse_mode": "HTML"}
    if reply_markup:
        payload["reply_markup"] = json.dumps(reply_markup)
    async with httpx.AsyncClient(timeout=10.0) as client:
        try:
            r = await client.post(url, json=payload)
            return r.status_code == 200
        except Exception as e:
            print(f"[TG ERROR] {e}")
            return False


async def send_telegram_message(tg_id: str, text: str) -> bool:
    return await send_telegram_alert(tg_id, text)


# ── Валидация ─────────────────────────────────────────────────
def is_valid_tg_id(tg_id: str) -> bool:
    try:
        return 0 < int(tg_id) < 10_000_000_000
    except:
        return False


# ── Форматирование ────────────────────────────────────────────
def format_subscription_end(dt: datetime | None) -> str | None:
    if dt is None:
        return None
    return (dt + timedelta(hours=TIMEZONE_OFFSET)).strftime("%d.%m.%Y")


def format_datetime_msk(dt: datetime) -> str:
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    return (dt + timedelta(hours=TIMEZONE_OFFSET)).strftime("%d.%m.%Y %H:%M")


def format_time_msk(dt: datetime) -> str:
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    return (dt + timedelta(hours=TIMEZONE_OFFSET)).strftime("%H:%M:%S")


def format_date_msk(dt: datetime) -> str:
    if dt.tzinfo is None:
        dt = dt.replace(tzinfo=timezone.utc)
    return (dt + timedelta(hours=TIMEZONE_OFFSET)).strftime("%d.%m.%Y")


# ── Статус системы ────────────────────────────────────────────
def get_system_status(
    cpu: float,
    ram: float,
    cpu_temp: float,
    gpu_temp: float = 0.0
) -> str:
    if cpu == 0.0 and ram == 0 and cpu_temp == 0.0 and gpu_temp == 0.0:
        return "🟢 СИСТЕМА СТАБИЛЬНА"
    if (cpu > 80 or ram > 90
            or (cpu_temp > 60 and cpu_temp > 0)
            or (gpu_temp > 60 and gpu_temp > 0)):
        return "🔴 КРИТИЧЕСКИЙ УРОВЕНЬ"
    if (cpu > 60 or ram > 75
            or (cpu_temp > 50 and cpu_temp > 0)
            or (gpu_temp > 50 and gpu_temp > 0)):
        return "🟡 ВЫСОКАЯ НАГРУЗКА"
    return "🟢 СИСТЕМА СТАБИЛЬНА"


# ── Математика ────────────────────────────────────────────────
def avg(values: list) -> float:
    return sum(values) / len(values) if values else 0.0


def mx(values: list) -> float:
    return max(values) if values else 0.0