#!/usr/bin/env python3
"""
OPRA Database Parser and EQ Converter.

Reads OPRA headphone EQ database from a local JSONL file and
converts to Equalizer APO format. For development/CI without
OPRA cache, the database path can be provided via OPRA_DATABASE_PATH.
Source: https://github.com/opra-project/OPRA
License: CC BY-SA 4.0
"""

import json
import os
from dataclasses import dataclass, field
from pathlib import Path

from scripts.modern_target import MODERN_TARGET_SPEC


PROJECT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_OPRA_PATH = Path(
    os.environ.get("OPRA_DATABASE_PATH")
    or (PROJECT_ROOT / "data" / "opra" / "database_v1.jsonl")
)


@dataclass
class EqBand:
    """Single EQ band in Equalizer APO format."""

    enabled: bool = True
    filter_type: str = "PK"  # PK, LS, HS, LP, HP
    frequency: float = 1000.0
    gain_db: float = 0.0
    q: float = 1.0


@dataclass
class EqProfile:
    """Complete EQ profile in Equalizer APO format."""

    name: str = ""
    preamp_db: float = 0.0
    bands: list[EqBand] = field(default_factory=list)
    author: str = ""
    source: str = "OPRA"
    details: str = ""

    def to_apo_format(self) -> str:
        """Convert to Equalizer APO text format."""
        lines: list[str] = []

        if self.preamp_db != 0.0:
            lines.append(f"Preamp: {self.preamp_db:.1f} dB")

        filter_num = 0
        for band in self.bands:
            if not band.enabled:
                continue

            filter_num += 1
            status = "ON"
            if band.filter_type in ("LP", "HP"):
                line = (
                    f"Filter {filter_num}: {status} {band.filter_type} "
                    f"Fc {band.frequency:.1f} Hz Q {band.q:.2f}"
                )
            else:
                line = (
                    f"Filter {filter_num}: {status} {band.filter_type} "
                    f"Fc {band.frequency:.1f} Hz Gain {band.gain_db:.1f} dB "
                    f"Q {band.q:.2f}"
                )
            lines.append(line)

        return "\n".join(lines)


def slope_to_q(slope_db_per_oct: int) -> float:
    """
    Convert LP/HP filter slope (dB/oct) to Q value.

    Uses Butterworth approximations for single biquad filters.
    """
    slope_q_map = {
        6: 0.5,
        12: 0.707,
        18: 0.5,
        24: 0.541,
        30: 0.5,
        36: 0.518,
    }
    return slope_q_map.get(slope_db_per_oct, 0.707)


def convert_opra_band(band_data: dict) -> EqBand | None:
    """Convert OPRA EQ band to Equalizer APO format."""
    band_type = band_data.get("type", "")
    frequency = band_data.get("frequency", 1000.0)
    gain_db = band_data.get("gain_db", 0.0)
    q = band_data.get("q")
    slope = band_data.get("slope")

    type_map = {
        "peak_dip": "PK",
        "low_shelf": "LS",
        "high_shelf": "HS",
        "low_pass": "LP",
        "high_pass": "HP",
    }

    apo_type = type_map.get(band_type)
    if apo_type is None:
        return None

    if apo_type in ("LP", "HP"):
        q = slope_to_q(slope) if slope is not None else 0.707
        gain_db = 0.0
    elif q is None:
        q = 1.0

    return EqBand(
        enabled=True,
        filter_type=apo_type,
        frequency=frequency,
        gain_db=gain_db,
        q=q,
    )


def convert_opra_to_apo(eq_data: dict) -> EqProfile:
    """Convert OPRA EQ profile to Equalizer APO format."""
    params = eq_data.get("parameters", {})
    bands_data = params.get("bands", [])

    bands = []
    for band_data in bands_data:
        band = convert_opra_band(band_data)
        if band is not None:
            bands.append(band)

    return EqProfile(
        name=eq_data.get("name", ""),
        preamp_db=params.get("gain_db", 0.0),
        bands=bands,
        author=eq_data.get("author", ""),
        source="OPRA",
        details=eq_data.get("details", ""),
    )


def apply_modern_target_correction(profile: EqProfile) -> EqProfile:
    """
    Apply Modern Target (KB5000_7) correction to an OPRA EQ profile.

    Adds the correction bands at runtime to comply with CC BY-SA 4.0.
    Preamp is reduced by the largest positive correction gain.
    """
    correction_bands = []
    max_positive_gain = 0.0
    for band in MODERN_TARGET_SPEC.filters:
        max_positive_gain = max(max_positive_gain, max(0.0, band["gain_db"]))
        correction_bands.append(
            EqBand(
                enabled=True,
                filter_type=band["filter_type"],
                frequency=band["frequency"],
                gain_db=band["gain_db"],
                q=band["q"],
            )
        )

    adjusted_preamp = profile.preamp_db - max_positive_gain

    return EqProfile(
        name=profile.name,
        preamp_db=adjusted_preamp,
        bands=profile.bands + correction_bands,
        author=profile.author,
        source=profile.source,
        details=(
            f"{profile.details} + Modern Target (KB5000_7)"
            if profile.details
            else "Modern Target (KB5000_7)"
        ),
    )


class OpraDatabase:
    """OPRA headphone EQ database reader."""

    def __init__(self, db_path: Path | None = None):
        self.db_path = db_path or DEFAULT_OPRA_PATH
        self._vendors: dict[str, dict] = {}
        self._products: dict[str, dict] = {}
        self._eq_profiles: dict[str, dict] = {}
        self._loaded = False

    def _ensure_loaded(self) -> None:
        if self._loaded:
            return

        if not self.db_path.exists():
            raise FileNotFoundError(
                "OPRA database not found. Set OPRA_DATABASE_PATH "
                "or place database_v1.jsonl under data/opra/."
            )

        with self.db_path.open(encoding="utf-8") as handle:
            for line in handle:
                line = line.strip()
                if not line:
                    continue

                try:
                    entry = json.loads(line)
                except json.JSONDecodeError:
                    continue

                entry_type = entry.get("type")
                entry_id = entry.get("id")
                data = entry.get("data", {})

                if entry_type == "vendor":
                    self._vendors[entry_id] = data
                elif entry_type == "product":
                    self._products[entry_id] = data
                elif entry_type == "eq":
                    self._eq_profiles[entry_id] = data

        self._loaded = True

    @property
    def vendor_count(self) -> int:
        self._ensure_loaded()
        return len(self._vendors)

    @property
    def product_count(self) -> int:
        self._ensure_loaded()
        return len(self._products)

    @property
    def eq_profile_count(self) -> int:
        self._ensure_loaded()
        return len(self._eq_profiles)

    def get_vendors(self) -> list[dict]:
        self._ensure_loaded()
        vendors = [{"id": vid, **vdata} for vid, vdata in self._vendors.items()]
        vendors.sort(key=lambda v: v.get("name", "").lower())
        return vendors

    def _get_eq_profiles_for_product(self, product_id: str) -> list[dict]:
        profiles = []
        for eq_id, eq_data in self._eq_profiles.items():
            if eq_data.get("product_id") == product_id:
                profiles.append({"id": eq_id, **eq_data})
        return profiles

    def get_products_by_vendor(self, vendor_id: str) -> list[dict]:
        self._ensure_loaded()
        products = []
        for pid, pdata in self._products.items():
            if pdata.get("vendor_id") == vendor_id:
                eq_profiles = self._get_eq_profiles_for_product(pid)
                products.append({"id": pid, "eq_profiles": eq_profiles, **pdata})
        products.sort(key=lambda p: p.get("name", "").lower())
        return products

    def search(self, query: str, limit: int = 50) -> list[dict]:
        self._ensure_loaded()
        query_lower = query.lower()
        results = []

        for pid, pdata in self._products.items():
            product_name = pdata.get("name", "")
            vendor_id = pdata.get("vendor_id", "")
            vendor_data = self._vendors.get(vendor_id, {})
            vendor_name = vendor_data.get("name", "")

            if (
                query_lower in product_name.lower()
                or query_lower in vendor_name.lower()
            ):
                eq_profiles = self._get_eq_profiles_for_product(pid)
                if eq_profiles:
                    results.append(
                        {
                            "id": pid,
                            "name": product_name,
                            "type": pdata.get("type", ""),
                            "vendor": {"id": vendor_id, "name": vendor_name},
                            "eq_profiles": eq_profiles,
                        }
                    )

        def sort_key(item: dict) -> tuple:
            name = item.get("name", "").lower()
            vendor = item["vendor"].get("name", "").lower()
            exact_match = query_lower == name
            starts_with = name.startswith(query_lower) or vendor.startswith(query_lower)
            return (not exact_match, not starts_with, vendor, name)

        results.sort(key=sort_key)
        return results[:limit]

    def get_eq_profile(self, eq_id: str) -> dict | None:
        self._ensure_loaded()
        eq_data = self._eq_profiles.get(eq_id)
        if eq_data is None:
            return None
        return {"id": eq_id, **eq_data}

    def get_product(self, product_id: str) -> dict | None:
        self._ensure_loaded()
        pdata = self._products.get(product_id)
        if pdata is None:
            return None

        vendor_id = pdata.get("vendor_id", "")
        vendor_data = self._vendors.get(vendor_id, {})
        eq_profiles = self._get_eq_profiles_for_product(product_id)

        return {
            "id": product_id,
            "name": pdata.get("name", ""),
            "type": pdata.get("type", ""),
            "vendor": {"id": vendor_id, "name": vendor_data.get("name", "")},
            "eq_profiles": eq_profiles,
        }


_db_instance: OpraDatabase | None = None


def get_database() -> OpraDatabase:
    """Get shared database instance."""
    global _db_instance
    if _db_instance is None:
        _db_instance = OpraDatabase()
    return _db_instance


__all__ = [
    "EqBand",
    "EqProfile",
    "OpraDatabase",
    "apply_modern_target_correction",
    "convert_opra_to_apo",
    "get_database",
]
