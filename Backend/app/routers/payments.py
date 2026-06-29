# ═══════════════════════════════════════════════════════════
#  routers/payments.py
#  /create_payment, /yookassa_webhook
# ═══════════════════════════════════════════════════════════

import uuid
import json
import base64
from datetime import datetime, timezone, timedelta
from fastapi import APIRouter, Depends, HTTPException, Request
from sqlalchemy.ext.asyncio import AsyncSession
from sqlalchemy.future import select
from sqlalchemy import update
import httpx

from ..database import get_db
from ..models import Agent, User
from ..config import (
    YOOKASSA_SHOP_ID, YOOKASSA_SECRET_KEY,
    RANK_LIMITS, RANK_PRICES,
)
from ..utils import send_telegram_message, format_subscription_end, is_valid_tg_id

router = APIRouter(tags=["payments"])

YOOKASSA_IPS = {
    "185.71.76.0", "185.71.77.0", "77.75.153.0",
    "77.75.156.11", "77.75.156.35",
    "127.0.0.1",
}


@router.post("/create_payment")
async def create_payment(tg_id: str, rank: str):
    if rank not in RANK_PRICES:
        raise HTTPException(status_code=400, detail="invalid_rank")
    if not YOOKASSA_SHOP_ID or not YOOKASSA_SECRET_KEY:
        raise HTTPException(status_code=503, detail="yookassa_not_configured")

    amount_rub      = str(RANK_PRICES[rank] / 100)
    idempotence_key = str(uuid.uuid4())
    payload = {
        "amount":               {"value": amount_rub, "currency": "RUB"},
        "payment_method_data":  {"type": "bank_card"},
        "confirmation":         {"type": "redirect", "return_url": "https://t.me/NeuroGuardPcBot"},
        "description":          f"NeuroGuard {rank} 30 дней — tg_id {tg_id}",
        "metadata":             {"tg_id": tg_id, "rank": rank},
        "capture":              True,
    }
    auth = base64.b64encode(
        f"{YOOKASSA_SHOP_ID}:{YOOKASSA_SECRET_KEY}".encode()
    ).decode()

    async with httpx.AsyncClient(timeout=15.0) as client:
        r = await client.post(
            "https://api.yookassa.ru/v3/payments",
            json=payload,
            headers={
                "Content-Type":    "application/json",
                "Authorization":   f"Basic {auth}",
                "Idempotence-Key": idempotence_key,
            },
        )
    if r.status_code not in (200, 201):
        print(f"[YOOKASSA ERROR] {r.status_code}: {r.text}")
        raise HTTPException(status_code=502, detail="yookassa_error")

    result = r.json()
    return {
        "payment_url": result["confirmation"]["confirmation_url"],
        "payment_id":  result["id"],
    }


@router.post("/yookassa_webhook")
async def yookassa_webhook(request: Request, db: AsyncSession = Depends(get_db)):
    body      = await request.body()
    client_ip = request.client.host

    if YOOKASSA_SECRET_KEY and YOOKASSA_SECRET_KEY != "test":
        if client_ip not in YOOKASSA_IPS:
            print(f"[YOOKASSA] Запрос с незнакомого IP: {client_ip}")

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
        rank     = metadata.get("rank", "").upper()

        if not tg_id or rank not in RANK_LIMITS:
            return {"status": "ok"}

        res = await db.execute(select(User).where(User.tg_id == str(tg_id)))
        user = res.scalars().first()
        if not user:
            user = User(tg_id=str(tg_id))
            db.add(user)
            await db.flush()

        now  = datetime.now(timezone.utc)
        days = 30
        if user.subscription_end and user.subscription_end > now and user.rank == rank:
            user.subscription_end = user.subscription_end + timedelta(days=days)
        else:
            user.subscription_end = now + timedelta(days=days)

        user.rank = rank
        await db.execute(
            update(Agent).where(Agent.tg_id == str(tg_id)).values(rank=rank)
        )
        await db.commit()

        end_str = format_subscription_end(user.subscription_end)
        print(f"[YOOKASSA] ✅ {tg_id} → {rank} до {end_str}")

        emoji = "👑" if rank == "OVERSEER" else "🛡"
        await send_telegram_message(
            tg_id,
            f"{emoji} <b>Подписка {rank} активирована!</b>\n\n"
            f"📅 Действует до: <b>{end_str}</b>\n\n"
            f"Используй /download чтобы получить агента."
        )

    elif event == "payment.canceled":
        payment  = data.get("object", {})
        metadata = payment.get("metadata", {})
        tg_id    = metadata.get("tg_id", "")
        if tg_id and is_valid_tg_id(tg_id):
            await send_telegram_message(
                tg_id,
                "❌ <b>Платёж отменён.</b>\n\nЕсли возникли проблемы — напишите @TLEET_BLANT"
            )

    return {"status": "ok"}