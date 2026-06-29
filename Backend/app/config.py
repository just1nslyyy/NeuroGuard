# ═══════════════════════════════════════════════════════════
#  config.py
#  Константы и переменные окружения
# ═══════════════════════════════════════════════════════════

import os

# ── Токены ──────────────────────────────────────────────────
BOT_TOKEN           = os.getenv("NEUROGUARD_BOT_TOKEN", "")
INTERNAL_API_TOKEN  = os.getenv("NEUROGUARD_INTERNAL_API_TOKEN", "")
YOOKASSA_SHOP_ID    = os.getenv("YOOKASSA_SHOP_ID", "")
YOOKASSA_SECRET_KEY = os.getenv("YOOKASSA_SECRET_KEY", "")

# ── Валидация при старте ─────────────────────────────────────
if not BOT_TOKEN:
    raise RuntimeError("NEUROGUARD_BOT_TOKEN is required")
if not INTERNAL_API_TOKEN:
    raise RuntimeError("NEUROGUARD_INTERNAL_API_TOKEN is required")

# ── Лимиты подписок ─────────────────────────────────────────
RANK_LIMITS: dict[str, int] = {
    "FREE":     1,
    "SENTINEL": 3,
    "OVERSEER": 10,
}

RANK_PRICES: dict[str, int] = {
    "SENTINEL": 29900,
    "OVERSEER": 49900,
}

# ── Авторизация ──────────────────────────────────────────────
CODE_TTL_SECONDS  = 180
CODE_MAX_ATTEMPTS = 5

# ── Алерты ───────────────────────────────────────────────────
CPU_ALERT_TEMP = 90.0
GPU_ALERT_TEMP = 85.0
ALERT_COOLDOWN = 300  # секунд между повторными алертами

# ── Прочее ───────────────────────────────────────────────────
ADMIN_TG_ID   = "8362261813"
TIMEZONE_OFFSET = 3  # МСК