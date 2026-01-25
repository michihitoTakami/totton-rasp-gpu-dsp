"""Pydantic models for Web API responses."""

from typing import Any, Literal, Optional

from pydantic import BaseModel, Field, StringConstraints
from typing_extensions import Annotated


class AlsaSettings(BaseModel):
    """ALSA configuration settings."""

    input_device: Optional[str] = None
    output_device: Optional[str] = None
    sample_rate: Optional[int] = Field(default=None, ge=0)
    channels: Optional[int] = Field(default=None, ge=1)
    format: Optional[str] = None
    period_frames: Optional[int] = Field(default=None, ge=0)
    buffer_frames: Optional[int] = Field(default=None, ge=0)


class FilterSettings(BaseModel):
    """Filter configuration settings."""

    ratio: Optional[int] = Field(default=None, ge=1)
    phase_type: Optional[str] = None
    directory: Optional[str] = None


class Settings(BaseModel):
    """Minimal config settings used by EQ endpoints."""

    eq_enabled: bool = False
    eq_profile: Optional[str] = None
    eq_profile_path: Optional[str] = None
    alsa: Optional[AlsaSettings] = None
    filter: Optional[FilterSettings] = None


class AlsaDeviceOption(BaseModel):
    """ALSA device option with display label."""

    value: str
    label: str


class AlsaDeviceListResponse(BaseModel):
    """Available ALSA devices."""

    playback: list[AlsaDeviceOption]
    capture: list[AlsaDeviceOption]


class ConfigResponse(BaseModel):
    """Config response including ALSA settings."""

    settings: Settings


class ConfigUpdateRequest(BaseModel):
    """Update request for config.json."""

    alsa: Optional[AlsaSettings] = None
    filter: Optional[FilterSettings] = None


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


class OpraSyncMetadata(BaseModel):
    """Metadata for a synced OPRA database version."""

    commit_sha: str
    source: str
    source_url: str
    downloaded_at: str
    sha256: str
    size_bytes: int
    stats: dict[str, Any] = {}


class OpraSyncStatusResponse(BaseModel):
    """OPRA sync status response model."""

    status: str
    job_id: Optional[str] = None
    current_commit: Optional[str] = None
    previous_commit: Optional[str] = None
    last_updated_at: Optional[str] = None
    last_error: Optional[str] = None
    versions: list[str] = []
    current_metadata: Optional[OpraSyncMetadata] = None


class OpraSyncAvailableResponse(BaseModel):
    """OPRA sync availability response model."""

    source: str
    latest: str
    source_url: str


class OpraSyncUpdateRequest(BaseModel):
    """OPRA sync update request model."""

    target: str = Field(description="latest or commit SHA")
    source: Literal["github_raw", "cloudflare"]


class OpraSyncJobResponse(BaseModel):
    """OPRA sync job response model."""

    job_id: str
    status: str


class OpraStats(BaseModel):
    """OPRA database statistics response model."""

    vendors: int
    products: int
    eq_profiles: int
    license: str = "CC BY-SA 4.0"
    attribution: str = "OPRA Project (https://github.com/opra-project/OPRA)"


class OpraVendorsResponse(BaseModel):
    """OPRA vendors list response model."""

    vendors: list[dict[str, Any]]
    count: int


class OpraVendor(BaseModel):
    """OPRA vendor information model."""

    id: str
    name: str


class OpraEqProfileInfo(BaseModel):
    """OPRA EQ profile information model."""

    id: str
    author: str = ""
    details: str = ""

    model_config = {"extra": "allow"}


class OpraSearchResult(BaseModel):
    """OPRA search result item model."""

    id: str
    name: str
    type: str
    vendor: OpraVendor
    eq_profiles: list[OpraEqProfileInfo]


class OpraSearchResponse(BaseModel):
    """OPRA search results response model."""

    results: list[OpraSearchResult]
    count: int
    query: str


class OpraEqAttribution(BaseModel):
    """OPRA EQ attribution model."""

    license: str = "CC BY-SA 4.0"
    source: str = "OPRA Project"
    author: str


class OpraEqResponse(BaseModel):
    """OPRA EQ profile response model."""

    id: str
    name: str
    author: str
    details: str
    parameters: dict[str, Any] = {}
    apo_format: str
    modern_target_applied: bool
    attribution: OpraEqAttribution


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
