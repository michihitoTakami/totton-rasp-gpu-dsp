"""Configuration loading and saving."""

import json
from pathlib import Path
from typing import Any

from ..constants import CONFIG_PATH, EQ_PROFILES_DIR
from ..models import AlsaSettings, FilterSettings, Settings

_LEGACY_ALSA_KEYS = {
    "alsaInputDevice",
    "alsaOutputDevice",
    "alsaSampleRate",
    "alsaChannels",
    "alsaFormat",
}


def _build_profile_path(profile_name: str | None) -> str | None:
    """Return full path for the given EQ profile name, or None."""
    if not profile_name:
        return None
    return str(EQ_PROFILES_DIR / f"{profile_name}.txt")


def load_raw_config() -> dict[str, Any]:
    """Load raw config.json as dictionary."""
    if CONFIG_PATH.exists():
        try:
            with open(CONFIG_PATH) as f:
                data = json.load(f)
            if isinstance(data, dict):
                return data
        except (json.JSONDecodeError, IOError):
            pass
    return {}


def load_config() -> Settings:
    """Load configuration from JSON file."""
    if CONFIG_PATH.exists():
        try:
            with open(CONFIG_PATH) as f:
                data = json.load(f)

            eq_profile = data.get("eqProfile")
            eq_profile_path = data.get("eqProfilePath")
            eq_enabled = data.get("eqEnabled")
            alsa_block = data.get("alsa") if isinstance(data.get("alsa"), dict) else {}
            filter_block = (
                data.get("filter") if isinstance(data.get("filter"), dict) else {}
            )

            alsa_input = alsa_block.get("inputDevice", data.get("alsaInputDevice"))
            alsa_output = alsa_block.get("outputDevice", data.get("alsaOutputDevice"))
            alsa_rate = alsa_block.get("sampleRate", data.get("alsaSampleRate"))
            alsa_channels = alsa_block.get("channels", data.get("alsaChannels"))
            alsa_format = alsa_block.get("format", data.get("alsaFormat"))
            alsa_period = alsa_block.get("periodFrames")
            alsa_buffer = alsa_block.get("bufferFrames")

            filter_ratio = filter_block.get("ratio")
            filter_phase = filter_block.get("phaseType")
            filter_dir = filter_block.get("directory")

            if eq_profile_path is None:
                if eq_enabled is None and eq_profile:
                    eq_profile_path = _build_profile_path(eq_profile)
                else:
                    eq_enabled = False

            if eq_enabled is None:
                eq_enabled = bool(eq_profile_path)

            if eq_profile is None and eq_profile_path:
                eq_profile = Path(eq_profile_path).stem

            alsa_settings = None
            if any(
                value is not None
                for value in (
                    alsa_input,
                    alsa_output,
                    alsa_rate,
                    alsa_channels,
                    alsa_format,
                    alsa_period,
                    alsa_buffer,
                )
            ):
                alsa_settings = AlsaSettings(
                    input_device=alsa_input,
                    output_device=alsa_output,
                    sample_rate=alsa_rate,
                    channels=alsa_channels,
                    format=alsa_format,
                    period_frames=alsa_period,
                    buffer_frames=alsa_buffer,
                )

            filter_settings = None
            if any(
                value is not None for value in (filter_ratio, filter_phase, filter_dir)
            ):
                filter_settings = FilterSettings(
                    ratio=filter_ratio,
                    phase_type=filter_phase,
                    directory=filter_dir,
                )

            return Settings(
                eq_enabled=bool(eq_enabled and eq_profile_path),
                eq_profile=eq_profile,
                eq_profile_path=eq_profile_path,
                alsa=alsa_settings,
                filter=filter_settings,
            )
        except (json.JSONDecodeError, ValueError, TypeError):
            pass

    return Settings()


def save_config(settings: Settings) -> bool:
    """Save configuration to JSON file, preserving existing fields."""
    try:
        existing = load_raw_config()

        eq_profile_path = settings.eq_profile_path or _build_profile_path(
            settings.eq_profile
        )
        eq_enabled = settings.eq_enabled and bool(eq_profile_path)

        existing["eqEnabled"] = eq_enabled
        existing["eqProfile"] = settings.eq_profile if eq_enabled else None
        existing["eqProfilePath"] = eq_profile_path if eq_enabled else None
        if settings.alsa is not None:
            alsa_existing = existing.get("alsa")
            if not isinstance(alsa_existing, dict):
                alsa_existing = {}
            alsa_existing.update(
                {
                    "inputDevice": settings.alsa.input_device,
                    "outputDevice": settings.alsa.output_device,
                    "sampleRate": settings.alsa.sample_rate,
                    "channels": settings.alsa.channels,
                    "format": settings.alsa.format,
                    "periodFrames": settings.alsa.period_frames,
                    "bufferFrames": settings.alsa.buffer_frames,
                }
            )
            existing["alsa"] = alsa_existing
            for key in _LEGACY_ALSA_KEYS:
                existing.pop(key, None)
        if settings.filter is not None:
            filter_existing = existing.get("filter")
            if not isinstance(filter_existing, dict):
                filter_existing = {}
            filter_existing.update(
                {
                    "ratio": settings.filter.ratio,
                    "phaseType": settings.filter.phase_type,
                    "directory": settings.filter.directory,
                }
            )
            existing["filter"] = filter_existing

        with open(CONFIG_PATH, "w") as f:
            json.dump(existing, f, indent=2)
        return True
    except IOError:
        return False


def save_config_updates(updates: dict[str, Any]) -> bool:
    """Update config.json with raw key/value pairs."""
    try:
        existing = load_raw_config()
        for key, value in updates.items():
            if key in ("alsa", "filter") and isinstance(value, dict):
                section = existing.get(key)
                if not isinstance(section, dict):
                    section = {}
                section.update(value)
                existing[key] = section
            else:
                existing[key] = value
        if "alsa" in updates:
            for legacy_key in _LEGACY_ALSA_KEYS:
                existing.pop(legacy_key, None)
        with open(CONFIG_PATH, "w") as f:
            json.dump(existing, f, indent=2)
        return True
    except IOError:
        return False
