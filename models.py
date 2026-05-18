from sqlalchemy import Column, Integer, String, Float, DateTime, ForeignKey, JSON
from sqlalchemy.orm import relationship
from sqlalchemy.sql import func
from .database import Base
from sqlalchemy import Boolean

class Agent(Base):
    __tablename__ = "agents"

    id              = Column(Integer, primary_key=True)
    hwid            = Column(String, unique=True, index=True, nullable=False)
    name            = Column(String, default="New Device")
    rank            = Column(String, default="FREE")          # всегда UPPER
    cpu_model       = Column(String, nullable=True)
    gpu_model       = Column(String, nullable=True)
    pending_command = Column(String, nullable=True)
    tg_id           = Column(String, index=True, nullable=True)
    auth_code       = Column(String, nullable=True)

    telemetry = relationship("Telemetry", back_populates="agent",
                             cascade="all, delete-orphan")


class Telemetry(Base):
    __tablename__ = "telemetry"

    id          = Column(Integer, primary_key=True, index=True)
    agent_id    = Column(Integer, ForeignKey("agents.id", ondelete="CASCADE"),
                         nullable=False)

    # Железо
    cpu_name    = Column(String,  nullable=True)
    gpu_name    = Column(String,  nullable=True)
    disks_info  = Column(String,  nullable=True)

    # Нагрузки
    cpu_avg_load = Column(Float,   default=0.0)
    cpu_temp     = Column(Float,   nullable=True)
    gpu_load     = Column(Float,   nullable=True)
    gpu_temp     = Column(Float,   nullable=True)

    # RAM
    ram_total   = Column(Float,   default=0.0)
    ram_used    = Column(Float,   default=0.0)
    ram_percent = Column(Integer, default=0)

    # Сеть
    net_down    = Column(Float,   default=0.0)
    net_up      = Column(Float,   default=0.0)

    # Питание
    power_source = Column(String,  nullable=True)
    battery_pct  = Column(Integer, default=100)
    uptime       = Column(Integer, default=0)

    top_processes = Column(JSON, nullable=True)

    created_at = Column(DateTime(timezone=True), server_default=func.now(), index=True)

    agent  = relationship("Agent", back_populates="telemetry")
    cores  = relationship("CoreTelemetry",  back_populates="parent_telemetry",
                          cascade="all, delete-orphan")
    disks  = relationship("DiskTelemetry",  back_populates="parent_telemetry",
                          cascade="all, delete-orphan")

    session_seconds = Column(Float, default=0.0)
    session_kwh = Column(Float, default=0.0)


class CoreTelemetry(Base):
    __tablename__ = "core_telemetry"

    id           = Column(Integer, primary_key=True)
    telemetry_id = Column(Integer, ForeignKey("telemetry.id", ondelete="CASCADE"),
                          index=True, nullable=False)
    core_index   = Column(Integer, nullable=False)
    load         = Column(Integer, default=0)
    temp         = Column(Float,   nullable=True)

    parent_telemetry = relationship("Telemetry", back_populates="cores")


class DiskTelemetry(Base):
    __tablename__ = "disk_telemetry"

    id           = Column(Integer, primary_key=True)
    telemetry_id = Column(Integer, ForeignKey("telemetry.id", ondelete="CASCADE"),
                          index=True, nullable=False)
    drive_name   = Column(String,  nullable=False)
    total_gb     = Column(Float,   default=0.0)
    used_gb      = Column(Float,   default=0.0)
    percent      = Column(Float,   default=0.0)   # Float, не Integer

    parent_telemetry = relationship("Telemetry", back_populates="disks")


class User(Base):
    __tablename__ = "users"

    id               = Column(Integer, primary_key=True)
    tg_id            = Column(String, unique=True, index=True, nullable=False)
    rank             = Column(String, default="FREE")
    subscription_end = Column(DateTime(timezone=True), nullable=True)
    created_at       = Column(DateTime(timezone=True), server_default=func.now())
    psu_watts = Column(Integer, nullable=True)
    trial_used = Column(Boolean, default=False)
