"""Configuration loading and saving."""

import json
from pathlib import Path
from typing import Any

from ..constants import CONFIG_PATH, EQ_PROFILES_DIR
from ..models import Settings


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
            alsa_input = data.get("alsaInputDevice")
            alsa_output = data.get("alsaOutputDevice")
            alsa_rate = data.get("alsaSampleRate")
            alsa_channels = data.get("alsaChannels")
            alsa_format = data.get("alsaFormat")

            if eq_profile_path is None:
                if eq_enabled is None and eq_profile:
                    eq_profile_path = _build_profile_path(eq_profile)
                else:
                    eq_enabled = False

            if eq_enabled is None:
                eq_enabled = bool(eq_profile_path)

            if eq_profile is None and eq_profile_path:
                eq_profile = Path(eq_profile_path).stem

            return Settings(
                eq_enabled=bool(eq_enabled and eq_profile_path),
                eq_profile=eq_profile,
                eq_profile_path=eq_profile_path,
                alsa_input_device=alsa_input,
                alsa_output_device=alsa_output,
                alsa_sample_rate=alsa_rate,
                alsa_channels=alsa_channels,
                alsa_format=alsa_format,
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
        if settings.alsa_input_device is not None:
            existing["alsaInputDevice"] = settings.alsa_input_device
        if settings.alsa_output_device is not None:
            existing["alsaOutputDevice"] = settings.alsa_output_device
        if settings.alsa_sample_rate is not None:
            existing["alsaSampleRate"] = settings.alsa_sample_rate
        if settings.alsa_channels is not None:
            existing["alsaChannels"] = settings.alsa_channels
        if settings.alsa_format is not None:
            existing["alsaFormat"] = settings.alsa_format

        with open(CONFIG_PATH, "w") as f:
            json.dump(existing, f, indent=2)
        return True
    except IOError:
        return False


def save_config_updates(updates: dict[str, Any]) -> bool:
    """Update config.json with raw key/value pairs."""
    try:
        existing = load_raw_config()
        existing.update(updates)
        with open(CONFIG_PATH, "w") as f:
            json.dump(existing, f, indent=2)
        return True
    except IOError:
        return False
