"""EQ profile parsing, validation, and file handling."""

import re
from pathlib import Path
from typing import Any

from fastapi import HTTPException, UploadFile

from ..constants import (
    FREQ_MAX_HZ,
    FREQ_MIN_HZ,
    GAIN_MAX_DB,
    GAIN_MIN_DB,
    MAX_EQ_FILE_SIZE,
    MAX_EQ_FILTERS,
    PREAMP_MAX_DB,
    PREAMP_MIN_DB,
    Q_MAX,
    Q_MIN,
    SAFE_FILENAME_PATTERN,
    SAFE_PROFILE_NAME_PATTERN,
)
from .modern_target import is_modern_target_filter

FILTER_TYPE_PARAMS = {
    "PK": {"fc": True, "gain": True, "q": True},
    "MODAL": {"fc": True, "gain": True, "q": True},
    "PEQ": {"fc": True, "gain": True, "q": True},
    "LP": {"fc": True, "gain": False, "q": False},
    "LPQ": {"fc": True, "gain": False, "q": False},
    "HP": {"fc": True, "gain": False, "q": False},
    "HPQ": {"fc": True, "gain": False, "q": False},
    "BP": {"fc": True, "gain": False, "q": False},
    "NO": {"fc": True, "gain": False, "q": False},
    "AP": {"fc": True, "gain": True, "q": True},
    "LS": {"fc": True, "gain": True, "q": True},
    "HS": {"fc": True, "gain": True, "q": True},
    "LSC": {"fc": True, "gain": True, "q": False},
    "HSC": {"fc": True, "gain": True, "q": False},
    "LSQ": {"fc": True, "gain": True, "q": True},
    "HSQ": {"fc": True, "gain": True, "q": True},
    "LS 6DB": {"fc": True, "gain": True, "q": False},
    "LS 12DB": {"fc": True, "gain": True, "q": False},
    "HS 6DB": {"fc": True, "gain": True, "q": False},
    "HS 12DB": {"fc": True, "gain": True, "q": False},
}


def is_safe_profile_name(name: str | None) -> bool:
    """Check if profile name is safe (no path traversal risk)."""
    if not name:
        return True

    if not SAFE_PROFILE_NAME_PATTERN.match(name):
        return False

    if ".." in name or name.startswith("."):
        return False

    return True


def sanitize_filename(filename: str) -> str | None:
    """Sanitize filename for security."""
    if not filename:
        return None

    normalized = filename.replace("\\", "/")
    basename = normalized.split("/")[-1]

    if not SAFE_FILENAME_PATTERN.match(basename):
        return None

    if ".." in basename:
        return None

    return basename


def _parse_filter_line(line: str) -> dict[str, Any] | None:
    """Parse a single filter line with flexible parameter matching."""
    base_pattern = r"Filter\s*(\d+)?\s*:\s+(ON|OFF)\s+(.+?)\s+Fc\s+([\d.]+)\s*(?:Hz)?"

    match = re.match(base_pattern, line, re.IGNORECASE)
    if not match:
        return None

    result = {
        "filter_num": int(match.group(1)) if match.group(1) else None,
        "enabled": match.group(2).upper() == "ON",
        "filter_type": match.group(3).strip().upper(),
        "frequency": float(match.group(4)),
        "gain": None,
        "q": None,
        "bw": None,
        "oct": None,
    }

    remainder = line[match.end() :].strip()

    gain_match = re.search(r"Gain\s+([-+]?\d+\.?\d*)\s*dB", remainder, re.IGNORECASE)
    if gain_match:
        result["gain"] = float(gain_match.group(1))

    q_match = re.search(r"Q\s+([\d.]+)", remainder, re.IGNORECASE)
    if q_match:
        result["q"] = float(q_match.group(1))

    bw_match = re.search(r"BW\s+([\d.]+)\s*(?:Hz)?", remainder, re.IGNORECASE)
    if bw_match:
        result["bw"] = float(bw_match.group(1))

    oct_match = re.search(r"BW\s+oct\s+([\d.]+)", remainder, re.IGNORECASE)
    if oct_match:
        result["oct"] = float(oct_match.group(1))

    return result


def validate_eq_profile_content(content: str) -> dict[str, Any]:
    """Validate EQ profile content for correctness and safety."""
    errors: list[str] = []
    warnings: list[str] = []
    preamp_db: float | None = None
    filter_count = 0
    max_positive_gain = 0.0
    recommended_preamp_db = 0.0

    if not content or not content.strip():
        return {
            "valid": False,
            "errors": ["Empty file"],
            "warnings": [],
            "preamp_db": None,
            "filter_count": 0,
            "recommended_preamp_db": 0.0,
        }

    lines = content.strip().split("\n")

    preamp_found = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("Preamp:"):
            preamp_found = True
            preamp_match = re.search(
                r"Preamp:\s*([-+]?\d+\.?\d*)\s*[dD][bB]?", stripped
            )
            if preamp_match:
                try:
                    preamp_db = float(preamp_match.group(1))
                    if preamp_db < PREAMP_MIN_DB or preamp_db > PREAMP_MAX_DB:
                        errors.append(
                            f"Preamp {preamp_db}dB out of range "
                            f"({PREAMP_MIN_DB}dB to {PREAMP_MAX_DB}dB)"
                        )
                except ValueError:
                    errors.append(f"Invalid Preamp value: {stripped}")
            else:
                warnings.append(f"Could not parse Preamp value: {stripped}")
            break

    if not preamp_found:
        errors.append("Missing 'Preamp:' line")

    for line in lines:
        stripped = line.strip()
        lower = stripped.lower()

        if not stripped or stripped.startswith("#"):
            continue

        if lower.startswith("preamp:"):
            continue

        if lower.startswith("filter ") or lower.startswith("filter:"):
            filter_count += 1
            parsed = _parse_filter_line(stripped)

            if not parsed:
                display_line = stripped[:50] + "..." if len(stripped) > 50 else stripped
                warnings.append(f"Could not parse filter line: {display_line}")
                continue

            filter_num = parsed["filter_num"]
            filter_label = filter_num if filter_num is not None else filter_count
            filter_type = parsed["filter_type"]
            freq = parsed["frequency"]
            gain = parsed["gain"]
            q = parsed["q"]

            if filter_type not in FILTER_TYPE_PARAMS:
                warnings.append(f"Filter {filter_label}: Unknown type '{filter_type}'")
            else:
                params = FILTER_TYPE_PARAMS[filter_type]

                if params["gain"] and gain is None:
                    errors.append(
                        f"Filter {filter_label}: Type '{filter_type}' requires Gain parameter"
                    )

                if (
                    params["q"]
                    and q is None
                    and parsed["bw"] is None
                    and parsed["oct"] is None
                ):
                    errors.append(
                        f"Filter {filter_label}: Type '{filter_type}' requires Q (or BW/Oct) parameter"
                    )

            if freq < FREQ_MIN_HZ or freq > FREQ_MAX_HZ:
                errors.append(
                    f"Filter {filter_label}: Frequency {freq}Hz out of range "
                    f"({FREQ_MIN_HZ}Hz to {FREQ_MAX_HZ}Hz)"
                )

            if gain is not None:
                if gain < GAIN_MIN_DB or gain > GAIN_MAX_DB:
                    errors.append(
                        f"Filter {filter_label}: Gain {gain}dB out of range "
                        f"({GAIN_MIN_DB}dB to {GAIN_MAX_DB}dB)"
                    )
                elif parsed["enabled"] and gain > max_positive_gain:
                    max_positive_gain = gain

            if q is not None:
                if q < Q_MIN or q > Q_MAX:
                    errors.append(
                        f"Filter {filter_label}: Q {q} out of range ({Q_MIN} to {Q_MAX})"
                    )

    if filter_count > MAX_EQ_FILTERS:
        errors.append(
            f"Too many filters ({filter_count}). Maximum allowed: {MAX_EQ_FILTERS}"
        )

    if filter_count == 0 and preamp_found:
        warnings.append("No filter lines found (only Preamp)")

    if max_positive_gain > 0:
        recommended_preamp_db = -max_positive_gain
        if preamp_db is not None and preamp_db > recommended_preamp_db:
            warnings.append(
                (
                    f"Preamp {preamp_db}dB may clip (max boost +{max_positive_gain}dB). "
                    f"Recommended Preamp: {recommended_preamp_db}dB or lower."
                )
            )

    return {
        "valid": len(errors) == 0,
        "errors": errors,
        "warnings": warnings,
        "preamp_db": preamp_db,
        "filter_count": filter_count,
        "recommended_preamp_db": recommended_preamp_db,
    }


async def read_and_validate_upload(file: UploadFile) -> tuple[str, str, dict]:
    """Common validation logic for EQ profile upload."""
    if not file.filename or not file.filename.endswith(".txt"):
        raise HTTPException(status_code=400, detail="Only .txt files are supported")

    safe_filename = sanitize_filename(file.filename)
    if not safe_filename:
        raise HTTPException(
            status_code=400,
            detail="Invalid filename. Use only letters, numbers, underscores, hyphens, and dots.",
        )

    try:
        content_bytes = await file.read()
        if len(content_bytes) > MAX_EQ_FILE_SIZE:
            raise HTTPException(
                status_code=400,
                detail=(
                    "File too large. Maximum size: "
                    f"{MAX_EQ_FILE_SIZE // (1024 * 1024)}MB"
                ),
            )
        content = content_bytes.decode("utf-8")
    except UnicodeDecodeError:
        raise HTTPException(status_code=400, detail="File must be UTF-8 encoded text")

    validation = validate_eq_profile_content(content)
    validation["size_bytes"] = len(content_bytes)

    return content, safe_filename, validation


def parse_eq_profile_content(file_path: Path) -> dict[str, Any]:
    """Parse EQ profile file and return structured content."""
    if not file_path.exists():
        return {"error": "File not found"}

    try:
        content = file_path.read_text(encoding="utf-8")
    except IOError as e:
        return {"error": f"Failed to read file: {e}"}

    lines = content.strip().split("\n")

    is_opra = any(line.startswith("# OPRA:") for line in lines)
    has_modern_target = any("Modern Target" in line for line in lines)

    opra_info: dict[str, str] = {}
    if is_opra:
        for line in lines:
            if line.startswith("# OPRA:"):
                opra_info["product"] = line.replace("# OPRA:", "").strip()
            elif line.startswith("# Author:"):
                opra_info["author"] = line.replace("# Author:", "").strip()
            elif line.startswith("# License:"):
                opra_info["license"] = line.replace("# License:", "").strip()
            elif line.startswith("# Source:"):
                opra_info["source"] = line.replace("# Source:", "").strip()
            elif line.startswith("# Details:"):
                opra_info["details"] = line.replace("# Details:", "").strip()

    filter_lines = []
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("Preamp:") or stripped.startswith("Filter"):
            filter_lines.append(stripped)

    opra_filters: list[str] = []
    original_filters: list[str] = []

    if is_opra and has_modern_target:
        for line in filter_lines:
            parsed_filter = _parse_filter_line(line)
            if is_modern_target_filter(parsed_filter):
                original_filters.append(line)
            else:
                opra_filters.append(line)
    else:
        opra_filters = filter_lines

    return {
        "source_type": "opra" if is_opra else "custom",
        "has_modern_target": has_modern_target,
        "opra_info": opra_info if is_opra else None,
        "opra_filters": opra_filters,
        "original_filters": original_filters,
        "raw_content": content,
    }
