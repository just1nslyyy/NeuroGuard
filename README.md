# 🛡️ NeuroGuard

> **Real-time PC monitoring system with Telegram control**  
> **Система мониторинга ПК в реальном времени с управлением через Telegram**

![C++](https://img.shields.io/badge/C++-20-blue?logo=c%2B%2B)
![Python](https://img.shields.io/badge/Python-3.12-yellow?logo=python)
![PostgreSQL](https://img.shields.io/badge/PostgreSQL-15-blue?logo=postgresql)
![Tests](https://img.shields.io/badge/Tests-54%2F54-brightgreen)
![License](https://img.shields.io/badge/License-MIT-green)

---

## 🇬🇧 English

### What is NeuroGuard?

NeuroGuard is a lightweight PC monitoring agent written in C++ that runs silently in the background and sends real-time hardware telemetry to a backend server. You control everything through a Telegram bot — from anywhere in the world.

### ✨ Features

- 🌡️ **CPU & GPU monitoring** — temperature, load per core, model name
- 💾 **RAM monitoring** — used, total, percentage
- 💿 **Disk monitoring** — all drives, used/total space, model via WMI
- 🌐 **Network speed** — download and upload in MB/s
- ⚡ **Power status** — AC/Battery, battery percentage, uptime
- 📸 **Remote screenshot** — take and receive screenshot via Telegram
- 💤 **Remote sleep / shutdown** — control your PC from anywhere
- 🔪 **Process manager** — view and kill top CPU processes remotely
- 🔔 **Overheat alerts** — automatic notifications when temps spike
- 💳 **Subscription system** — Free / Sentinel / Overseer tiers
- 🔒 **Secure auth** — HWID-based device binding via Telegram bot
- 🛡️ **Windows Defender exclusion** — auto-configures on first run
- 📊 **Session stats** — uptime tracking, estimated power consumption (kWh)
- 🔐 **PIN protection** — optional PIN for sensitive commands
- ⏰ **Auto-sleep scheduler** — scheduled sleep by MSK time
- 👑 **Admin panel** — broadcast, rank management, user stats

### 🏗️ Architecture

```
┌─────────────────┐     HTTP/JSON      ┌──────────────────────┐
│   C++ Agent     │ ──────────────────▶ │  Python Backend      │
│  (runs on PC)   │ ◀────────────────── │  FastAPI + PostgreSQL │
└─────────────────┘   commands/rank     └──────────┬───────────┘
                                                    │ Telegram API
                                         ┌──────────▼───────────┐
                                         │   C++ Telegram Bot   │
                                         │      TgBot-cpp       │
                                         └──────────────────────┘
```

### 📁 Project Structure

```
NeuroGuard/
├── agent/src/
│   ├── hardware/
│   │   ├── CpuMonitor.hpp         # CPU temp, load, cores via WMI
│   │   ├── GpuMonitor.hpp         # GPU temp, load via WMI
│   │   ├── RamMonitor.hpp         # RAM used/total/percent
│   │   ├── DiskMonitor.hpp        # All drives + WMI model
│   │   ├── NetworkMonitor.hpp     # Net speed via GetIfTable2
│   │   └── OhmProvider.hpp        # OpenHardwareMonitor WMI bridge
│   ├── system/
│   │   ├── DeviceIdentity.hpp     # HWID, autorun, registry
│   │   ├── PowerManager.hpp       # Sleep, shutdown, power source
│   │   ├── ProcessManager.hpp     # Top processes, kill by PID
│   │   └── ScreenCapture.hpp      # Screenshot via pure WinAPI BMP
│   ├── network/
│   │   ├── TelemetrySender.hpp    # POST telemetry to backend
│   │   └── AuthManager.hpp        # Device auth flow via bot
│   ├── security/
│   │   └── DefenderGuard.hpp      # Windows Defender exclusions
│   ├── ui/
│   │   └── TrayIcon.hpp           # System tray with menu
│   ├── tests/
│   │   └── test_main.cpp          # Google Test 6/6 ✅
│   ├── pch.hpp                    # Precompiled headers
│   └── main.cpp                   # Entry point
│
├── TgBot-cpp/
│   ├── admin/
│   │   └── AdminPanel.hpp         # Broadcast, rank mgmt, stats
│   ├── config/
│   │   └── BotConfig.hpp          # Constants, texts, URLs
│   ├── handlers/
│   │   ├── CommandHandlers.hpp    # /start /auth /psu /setpin...
│   │   ├── CallbackHandlers.hpp   # All inline keyboard callbacks
│   │   └── MessageHandlers.hpp    # Text messages, PIN, auto-sleep
│   ├── state/
│   │   └── BotState.hpp           # Global maps, mutex, atomic
│   ├── subscription/
│   │   ├── ISubscription.hpp      # Subscription interface
│   │   ├── SubscriptionBase.hpp   # Base class + displayProfile
│   │   ├── FreeSub.hpp            # Free tier (1 PC)
│   │   ├── SentinelSub.hpp        # Sentinel tier (3 PCs)
│   │   ├── OverseerSub.hpp        # Overseer tier (10 PCs)
│   │   └── SubFactory.hpp         # Factory by rank string
│   ├── threads/
│   │   └── BotThreads.hpp         # AutoSleep, SubscriptionAlert
│   ├── ui/
│   │   └── Keyboards.hpp          # All inline/reply keyboards
│   ├── utils/
│   │   └── BotHelpers.hpp         # Logging, rate limit, sendPhoto
│   ├── tests/
│   │   └── test_main.cpp          # Google Test 20/20 ✅
│   └── main.cpp                   # Bot entry + showMesStatus/Profile
│
├── Backend/
│   ├── app/
│   │   ├── routers/
│   │   │   ├── auth.py            # /auth/* device binding
│   │   │   ├── bot.py             # /bot/* commands & status
│   │   │   ├── telemetry.py       # /telemetry data ingestion
│   │   │   ├── payments.py        # /create_payment, /yookassa_webhook
│   │   │   └── admin.py           # /admin/* panel endpoints
│   │   ├── config.py              # Env vars, rank limits, prices
│   │   ├── database.py            # PostgreSQL async engine
│   │   ├── dependencies.py        # Auth, rate limit, rank helpers
│   │   ├── models.py              # SQLAlchemy ORM models
│   │   ├── utils.py               # TG alerts, formatting, helpers
│   │   └── main.py                # FastAPI app + lifespan
│   ├── tests/
│   │   └── test_api.py            # pytest 28/28 ✅
│   └── requirements.txt
│
├── .github/
│   └── workflows/
│       └── ci.yml                 # GitHub Actions CI
├── .gitignore
└── docker-compose.yml
```

### 🛠️ Tech Stack

| Component | Technology |
|-----------|------------|
| Agent | C++20, WinAPI, WMI, cpr, nlohmann/json |
| Backend | Python 3.12, FastAPI, PostgreSQL, SQLAlchemy |
| Telegram Bot | C++20, TgBot-cpp, cpr, nlohmann/json |
| Hardware data | OpenHardwareMonitor (WMI bridge) |
| HTTP client | cpr (libcurl) |
| Auth | HWID-based, 6-digit code, TTL 180s |
| Payments | YooKassa API |
| Tests | Google Test (C++), pytest (Python) |
| CI/CD | GitHub Actions |
| Deploy | Docker, docker-compose |

### 🚀 Getting Started

**Requirements:**
- Windows 10/11 x64
- vcpkg
- PostgreSQL 15+
- Python 3.12+

**Build agent:**
```bash
cd agent/src
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build . --config Release
```

**Build bot:**
```bash
cd TgBot-cpp
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build . --config Release
```

**Run backend:**
```bash
cd Backend
pip install -r requirements.txt
uvicorn app.main:app --host 0.0.0.0 --port 8000
```

**Docker:**
```bash
docker-compose up -d
```

### 🧪 Tests

```bash
# Agent (C++)
cd agent/src/build
.\Debug\NeuroGuard_Tests.exe --gtest_filter=RamMonitorTest.*:PowerManagerTest.*

# Bot (C++)
cd TgBot-cpp/build
.\Debug\NeuroGuard_Bot_Tests.exe

# Backend (Python)
cd Backend
python -m pytest tests/test_api.py -v
```

**Results: 54/54 tests passing ✅**

### 📋 Subscription Tiers

| Feature | Free | Sentinel | Overseer |
|---------|------|----------|----------|
| Devices | 1 | 3 | 10 |
| Telemetry | ✅ | ✅ | ✅ |
| Overheat alerts | ✅ | ✅ | ✅ |
| Remote sleep | ❌ | ✅ | ✅ |
| Remote shutdown | ❌ | ✅ | ✅ |
| Screenshots | ❌ | ❌ | ✅ |
| Kill processes | ❌ | ❌ | ✅ |
| Auto-sleep scheduler | ❌ | ❌ | ✅ |
| Network monitoring | ❌ | ❌ | ✅ |
| Weekly PC health | ❌ | ✅ | ✅ |
| Price | Free | 299₽/mo | 499₽/mo |

---

## 🇷🇺 Русский

### Что такое NeuroGuard?

NeuroGuard — это лёгкий агент мониторинга ПК на C++, который работает в фоне и отправляет телеметрию железа на сервер в реальном времени. Управление — через Telegram бот, из любой точки мира.

### ✨ Возможности

- 🌡️ **Мониторинг CPU и GPU** — температура, нагрузка по ядрам, название
- 💾 **Мониторинг RAM** — использовано, всего, процент
- 💿 **Мониторинг дисков** — все диски, занятое/общее место, модель через WMI
- 🌐 **Скорость сети** — загрузка и отдача в МБ/с
- ⚡ **Статус питания** — AC/Батарея, заряд, время работы
- 📸 **Удалённый скриншот** — сделать и получить через Telegram
- 💤 **Сон / выключение** — управляй ПК из любого места
- 🔪 **Менеджер процессов** — топ процессов, завершение по PID
- 🔔 **Оповещения о перегреве** — автоматические уведомления при CPU ≥90°C / GPU ≥85°C
- 💳 **Система подписок** — Free / Sentinel / Overseer
- 🔒 **Безопасная авторизация** — привязка устройства по HWID, 6-значный код, TTL 180с
- 🛡️ **Исключение Windows Defender** — автонастройка при первом запуске
- 📊 **Статистика сессии** — время работы, расход электроэнергии (кВт·ч)
- 🔐 **PIN-код** — защита чувствительных команд
- ⏰ **Авто-сон по расписанию** — сон по МСК времени каждый день
- 👑 **Админ-панель** — рассылка, выдача рангов, статистика пользователей

### 🚀 Быстрый старт

**Сборка агента:**
```bash
cd agent/src
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build . --config Release
```

**Сборка бота:**
```bash
cd TgBot-cpp
mkdir build && cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows
cmake --build . --config Release
```

**Запуск бэкенда:**
```bash
cd Backend
pip install -r requirements.txt
uvicorn app.main:app --host 0.0.0.0 --port 8000
```

**Docker:**
```bash
docker-compose up -d
```

### 🧪 Тесты

**Результат: 54/54 тестов ✅**

```bash
# Агент
.\Debug\NeuroGuard_Tests.exe --gtest_filter=RamMonitorTest.*:PowerManagerTest.*

# Бот
.\Debug\NeuroGuard_Bot_Tests.exe

# Бэкенд
python -m pytest tests/test_api.py -v
```

### 📋 Тарифы

| Функция | Free | Sentinel | Overseer |
|---------|------|----------|----------|
| Устройств | 1 | 3 | 10 |
| Телеметрия | ✅ | ✅ | ✅ |
| Алерты перегрева | ✅ | ✅ | ✅ |
| Удалённый сон | ❌ | ✅ | ✅ |
| Удалённое выключение | ❌ | ✅ | ✅ |
| Скриншоты | ❌ | ❌ | ✅ |
| Kill процессов | ❌ | ❌ | ✅ |
| Авто-сон по расписанию | ❌ | ❌ | ✅ |
| Мониторинг сети | ❌ | ❌ | ✅ |
| Диагностика ПК за 7 дней | ❌ | ✅ | ✅ |
| Цена | Бесплатно | 299₽/мес | 499₽/мес |

---

## 📄 License

MIT License — feel free to use, modify and distribute.

---

<div align="center">
Built with ❤️ and C++
</div>
