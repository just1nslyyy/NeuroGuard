# в•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђ
#  test_api.py
#  pytest С‚РµСЃС‚С‹ РґР»СЏ NeuroGuard Backend
# в•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђв•ђ

import pytest
import pytest_asyncio
from httpx import AsyncClient, ASGITransport
from sqlalchemy.ext.asyncio import create_async_engine, AsyncSession
from sqlalchemy.orm import sessionmaker

from app.main import app
from app.database import Base, get_db
from app.config import INTERNAL_API_TOKEN

# в”Ђв”Ђ РўРµСЃС‚РѕРІР°СЏ Р‘Р” (in-memory SQLite) в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
TEST_DATABASE_URL = "sqlite+aiosqlite:///:memory:"

engine_test = create_async_engine(TEST_DATABASE_URL, echo=False)
AsyncSessionTest = sessionmaker(engine_test, class_=AsyncSession, expire_on_commit=False)


async def override_get_db():
    async with AsyncSessionTest() as session:
        yield session

app.dependency_overrides[get_db] = override_get_db


@pytest_asyncio.fixture(autouse=True)
async def setup_db():
    async with engine_test.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
    yield
    async with engine_test.begin() as conn:
        await conn.run_sync(Base.metadata.drop_all)


@pytest_asyncio.fixture
async def client():
    async with AsyncClient(
        transport=ASGITransport(app=app), base_url="http://test"
    ) as c:
        yield c


# в”Ђв”Ђ /auth/generate в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
class TestAuthGenerate:
    @pytest.mark.asyncio
    async def test_generate_code(self, client):
        r = await client.post("/auth/generate/TESTHWID123")
        assert r.status_code == 200
        data = r.json()
        assert "code" in data
        assert len(data["code"]) == 6

    @pytest.mark.asyncio
    async def test_generate_already_linked(self, client):
        # Р“РµРЅРµСЂРёСЂСѓРµРј РєРѕРґ
        r1 = await client.post("/auth/generate/HWID_LINKED")
        assert r1.status_code == 200
        code = r1.json()["code"]
        # Р’РµСЂРёС„РёС†РёСЂСѓРµРј
        await client.post(f"/auth/verify?code={code}&tg_id=111111111")
        await client.post("/auth/update_name?tg_id=111111111&name=TestPC")
        # РџРѕРІС‚РѕСЂРЅР°СЏ РіРµРЅРµСЂР°С†РёСЏ вЂ” СѓР¶Рµ РїСЂРёРІСЏР·Р°РЅ
        r2 = await client.post("/auth/generate/HWID_LINKED")
        assert r2.status_code == 200
        assert r2.json().get("status") == "already_linked"


# в”Ђв”Ђ /auth/verify в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
class TestAuthVerify:
    @pytest.mark.asyncio
    async def test_verify_invalid_code(self, client):
        r = await client.post("/auth/verify?code=000000&tg_id=123456789")
        assert r.status_code == 404

    @pytest.mark.asyncio
    async def test_verify_valid_code(self, client):
        r1 = await client.post("/auth/generate/HWID_VERIFY")
        code = r1.json()["code"]
        r2 = await client.post(f"/auth/verify?code={code}&tg_id=222222222")
        assert r2.status_code == 200
        assert r2.json()["status"] == "success"


# в”Ђв”Ђ /auth/update_name в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
class TestUpdateName:
    @pytest.mark.asyncio
    async def test_update_name(self, client):
        r1 = await client.post("/auth/generate/HWID_NAME")
        code = r1.json()["code"]
        await client.post(f"/auth/verify?code={code}&tg_id=333333333")
        r2 = await client.post("/auth/update_name?tg_id=333333333&name=MyPC")
        assert r2.status_code == 200
        assert r2.json()["name"] == "MyPC"

    @pytest.mark.asyncio
    async def test_update_name_not_found(self, client):
        r = await client.post("/auth/update_name?tg_id=999999999&name=Ghost")
        assert r.status_code == 404


# в”Ђв”Ђ /bot/status в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
class TestBotStatus:
    @pytest.mark.asyncio
    async def test_status_new_user(self, client):
        r = await client.get("/bot/status?tg_id=444444444")
        assert r.status_code == 200
        data = r.json()
        assert data["user_rank"] == "FREE"
        assert data["devices"] == []

    @pytest.mark.asyncio
    async def test_status_with_device(self, client):
        r1 = await client.post("/auth/generate/HWID_STATUS")
        code = r1.json()["code"]
        await client.post(f"/auth/verify?code={code}&tg_id=555555555")
        r2 = await client.get("/bot/status?tg_id=555555555")
        assert r2.status_code == 200
        assert len(r2.json()["devices"]) == 1


# в”Ђв”Ђ /bot/set_psu в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
class TestSetPsu:
    @pytest.mark.asyncio
    async def test_set_psu_valid(self, client):
        r = await client.post("/bot/set_psu?tg_id=666666666&watts=550")
        assert r.status_code == 200
        assert r.json()["watts"] == 550

    @pytest.mark.asyncio
    async def test_set_psu_too_low(self, client):
        r = await client.post("/bot/set_psu?tg_id=666666666&watts=50")
        assert r.status_code == 422

    @pytest.mark.asyncio
    async def test_set_psu_too_high(self, client):
        r = await client.post("/bot/set_psu?tg_id=666666666&watts=5000")
        assert r.status_code == 422


# в”Ђв”Ђ /bot/set_command в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
class TestSetCommand:
    @pytest.mark.asyncio
    async def test_invalid_command(self, client):
        r1 = await client.post("/auth/generate/HWID_CMD")
        code = r1.json()["code"]
        await client.post(f"/auth/verify?code={code}&tg_id=777777777")
        r2 = await client.post("/bot/set_command?hwid=HWID_CMD&command=hack&tg_id=777777777")
        assert r2.status_code == 400

    @pytest.mark.asyncio
    async def test_valid_command(self, client):
        r1 = await client.post("/auth/generate/HWID_CMD2")
        code = r1.json()["code"]
        await client.post(f"/auth/verify?code={code}&tg_id=888888888")
        r2 = await client.post("/bot/set_command?hwid=HWID_CMD2&command=shutdown&tg_id=888888888")
        assert r2.status_code == 200
        assert r2.json()["command"] == "shutdown"

    @pytest.mark.asyncio
    async def test_forbidden_command(self, client):
        r1 = await client.post("/auth/generate/HWID_CMD3")
        code = r1.json()["code"]
        await client.post(f"/auth/verify?code={code}&tg_id=123123123")
        r2 = await client.post("/bot/set_command?hwid=HWID_CMD3&command=shutdown&tg_id=999999999")
        assert r2.status_code == 403


# в”Ђв”Ђ /bot/activate_trial в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
class TestActivateTrial:
    @pytest.mark.asyncio
    async def test_activate_trial(self, client):
        r = await client.post("/bot/activate_trial?tg_id=101010101&rank=OVERSEER&days=7")
        assert r.status_code == 200
        assert r.json()["rank"] == "OVERSEER"

    @pytest.mark.asyncio
    async def test_activate_trial_twice(self, client):
        await client.post("/bot/activate_trial?tg_id=202020202&rank=OVERSEER&days=7")
        r = await client.post("/bot/activate_trial?tg_id=202020202&rank=OVERSEER&days=7")
        assert r.status_code == 409


# в”Ђв”Ђ /bot/upgrade в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
class TestUpgrade:
    @pytest.mark.asyncio
    async def test_upgrade_no_token(self, client):
        r = await client.post(
            "/bot/upgrade",
            data={"tg_id": "303030303", "rank": "OVERSEER", "days": "30"}
        )
        assert r.status_code == 401

    @pytest.mark.asyncio
    async def test_upgrade_with_token(self, client):
        r = await client.post(
            "/bot/upgrade",
            data={
                "tg_id":      "303030303",
                "rank":       "OVERSEER",
                "days":       "30",
                "api_token":  INTERNAL_API_TOKEN,
            }
        )
        assert r.status_code == 200
        assert r.json()["rank"] == "OVERSEER"


# в”Ђв”Ђ /telemetry в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
class TestTelemetry:
    @pytest.mark.asyncio
    async def test_receive_telemetry(self, client):
        r1 = await client.post("/auth/generate/HWID_TEL")
        code = r1.json()["code"]
        await client.post(f"/auth/verify?code={code}&tg_id=404040404")

        payload = {
            "hwid":         "HWID_TEL",
            "cpu_load":     45.0,
            "cpu_temp":     55.0,
            "cpu_cores":    [40.0, 50.0, 45.0, 55.0],
            "cpu_model":    "Intel i7",
            "gpu_load":     30.0,
            "gpu_temp":     50.0,
            "gpu_model":    "RTX 3080",
            "ram_load":     60.0,
            "ram_used":     8.0,
            "ram_total":    16.0,
            "power_source": "AC",
            "battery_pct":  100,
            "uptime":       3600,
            "disk_model":   "Samsung SSD",
            "disks": [
                {"drive_name": "C:", "total_gb": 500.0, "used_gb": 250.0, "percent": 50.0}
            ],
            "net_down":     1.5,
            "net_up":       0.5,
            "top_processes": [
                {"name": "chrome.exe", "pid": 1234, "cpu": 5.0, "ram": 512.0}
            ],
        }
        r2 = await client.post("/telemetry", json=payload)
        assert r2.status_code == 200
        assert r2.json()["status"] == "ok"

    @pytest.mark.asyncio
    async def test_telemetry_short_hwid(self, client):
        payload = {
            "hwid":         "AB",
            "cpu_load":     0.0,
            "cpu_temp":     0.0,
            "cpu_cores":    [],
            "cpu_model":    "",
            "gpu_load":     0.0,
            "gpu_temp":     0.0,
            "gpu_model":    "",
            "ram_load":     0.0,
            "ram_used":     0.0,
            "ram_total":    0.0,
            "power_source": "AC",
            "battery_pct":  100,
            "uptime":       0,
            "disk_model":   "",
            "disks":        [],
        }
        r = await client.post("/telemetry", json=payload)
        assert r.status_code == 422


# в”Ђв”Ђ /admin/stats в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
class TestAdmin:
    @pytest.mark.asyncio
    async def test_stats_no_token(self, client):
        r = await client.get("/admin/stats")
        assert r.status_code == 401

    @pytest.mark.asyncio
    async def test_stats_with_token(self, client):
        r = await client.get(
            "/admin/stats",
            headers={"api-token": INTERNAL_API_TOKEN}
        )
        assert r.status_code == 200
        data = r.json()
        assert "total_users" in data
        assert "online_agents" in data

    @pytest.mark.asyncio
    async def test_users_with_token(self, client):
        r = await client.get(
            "/admin/users",
            headers={"api-token": INTERNAL_API_TOKEN}
        )
        assert r.status_code == 200
        assert "users" in r.json()

    @pytest.mark.asyncio
    async def test_all_user_ids(self, client):
        await client.get("/bot/status?tg_id=505050505")
        r = await client.get(
            "/admin/all_user_ids",
            headers={"api-token": INTERNAL_API_TOKEN}
        )
        assert r.status_code == 200
        assert "ids" in r.json()


# в”Ђв”Ђ utils в”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђв”Ђ
class TestUtils:
    def test_is_valid_tg_id(self):
        from app.utils import is_valid_tg_id
        assert is_valid_tg_id("123456789") is True
        assert is_valid_tg_id("0") is False
        assert is_valid_tg_id("abc") is False
        assert is_valid_tg_id("99999999999") is False

    def test_get_system_status(self):
        from app.utils import get_system_status
        r1 = get_system_status(0.0, 0, 0.0, 0.0)
        r2 = get_system_status(95.0, 0, 0.0, 0.0)
        r3 = get_system_status(65.0, 0, 0.0, 0.0)
        assert r1 != ""
        assert r2 != ""
        assert r3 != ""
        assert r1 != r2
        assert r1 != r3

    def test_format_subscription_end(self):
        from app.utils import format_subscription_end
        from datetime import datetime, timezone
        dt = datetime(2026, 12, 31, tzinfo=timezone.utc)
        result = format_subscription_end(dt)
        assert result == "31.12.2026"
        assert format_subscription_end(None) is None

    def test_avg_mx(self):
        from app.utils import avg, mx
        assert avg([1.0, 2.0, 3.0]) == 2.0
        assert avg([]) == 0.0
        assert mx([1.0, 5.0, 3.0]) == 5.0
        assert mx([]) == 0.0

