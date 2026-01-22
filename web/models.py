"""Pydantic models for Web API responses."""

from typing import Any, Literal, Optional

from pydantic import BaseModel, Field, StringConstraints
from typing_extensions import Annotated


class Settings(BaseModel):
    """Minimal config settings used by EQ endpoints."""

    eq_enabled: bool = False
    eq_profile: Optional[str] = None
    eq_profile_path: Optional[str] = None
    alsa_input_device: Optional[str] = None
    alsa_output_device: Optional[str] = None
    alsa_sample_rate: Optional[int] = None
    alsa_channels: Optional[int] = None
    alsa_format: Optional[str] = None


class AlsaDeviceListResponse(BaseModel):
    """Available ALSA devices."""

    playback: list[str]
    capture: list[str]


class ConfigResponse(BaseModel):
    """Config response including ALSA settings."""

    settings: Settings


class ConfigUpdateRequest(BaseModel):
    """Update request for config.json."""

    alsa_input_device: Optional[str] = None
    alsa_output_device: Optional[str] = None
    alsa_sample_rate: Optional[int] = Field(default=None, ge=0)
    alsa_channels: Optional[int] = Field(default=None, ge=1)
    alsa_format: Optional[str] = None


class EqProfileInfo(BaseModel):
    """EQ profile info model."""

    name: str
    filename: str
    path: str
    size: int
    modified: float
    type: str = Field(description="Profile type: 'opra' or 'custom'")
    filter_count: int


class EqProfilesResponse(BaseModel):
    """EQ profiles list response model."""

    profiles: list[EqProfileInfo]


class EqValidationResponse(BaseModel):
    """EQ profile validation response model."""

    valid: bool
    errors: list[str] = []
    warnings: list[str] = []
    preamp_db: Optional[float] = None
    filter_count: int = 0
    filename: str
    file_exists: bool
    size_bytes: int
    recommended_preamp_db: float = 0.0


class EqTextImportRequest(BaseModel):
    """Request body for text-based EQ profile import."""

    name: Annotated[
        str, StringConstraints(strip_whitespace=True, min_length=1, max_length=128)
    ]
    content: str = Field(description="Raw EQ profile text content")


class EqActiveResponse(BaseModel):
    """Active EQ profile response model."""

    active: bool
    name: Optional[str] = None
    error: Optional[str] = None
    source_type: Optional[str] = None
    has_modern_target: bool = False
    opra_info: Optional[dict[str, Any]] = None
    opra_filters: list[str] = []
    original_filters: list[str] = []


class ApiResponse(BaseModel):
    """Standard API response model for mutations."""

    success: bool
    message: str
    data: Optional[dict[str, Any]] = None
    restart_required: bool = False


class StatusResponse(BaseModel):
    """Minimal daemon status for UI."""

    daemon_running: bool
    state: str
    input_rate: int
    output_rate: int
    xrun_total: int
    phase_type: Optional[str] = None
    uptime_ms: Optional[int] = None
    reloads: Optional[int] = None
    soft_resets: Optional[int] = None


class PhaseTypeResponse(BaseModel):
    """Current phase type response."""

    phase_type: str


class PhaseTypeUpdateRequest(BaseModel):
    """Phase type update request."""

    phase_type: Literal["minimum", "linear"]
