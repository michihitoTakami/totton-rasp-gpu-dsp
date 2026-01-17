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

        with open(CONFIG_PATH, "w") as f:
            json.dump(existing, f, indent=2)
        return True
    except IOError:
        return False
