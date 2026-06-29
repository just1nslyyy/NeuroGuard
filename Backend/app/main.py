# ═══════════════════════════════════════════════════════════
#  main.py
#  NeuroGuard Backend — точка входа
# ═══════════════════════════════════════════════════════════

import asyncio
from contextlib import asynccontextmanager
from fastapi import FastAPI
from slowapi import Limiter, _rate_limit_exceeded_handler
from slowapi.util import get_remote_address
from slowapi.errors import RateLimitExceeded

from .database import engine, Base, get_db
from .dependencies import cleanup_expired_codes, auto_clear_sentinel_stats
from .routers import auth, bot, telemetry, payments, admin


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

    t1 = asyncio.create_task(_sentinel_cleanup())
    t2 = asyncio.create_task(cleanup_expired_codes())
    yield
    t1.cancel()
    t2.cancel()


app = FastAPI(title="NeuroGuard API", lifespan=lifespan)

limiter = Limiter(key_func=get_remote_address, default_limits=["200/minute"])
app.state.limiter = limiter
app.add_exception_handler(RateLimitExceeded, _rate_limit_exceeded_handler)

# ── Роутеры ──────────────────────────────────────────────────
app.include_router(auth.router)
app.include_router(bot.router)
app.include_router(telemetry.router)
app.include_router(payments.router)
app.include_router(admin.router)