"""Simple i18n helpers for the web UI."""

from __future__ import annotations

import json
from typing import Dict

from fastapi import Request

DEFAULT_LOCALE = "en"
SUPPORTED_LOCALES = {"en"}

TRANSLATIONS: Dict[str, Dict[str, str]] = {
    "en": {
        "app_title": "Totton Audio Control",
        "nav_title": "Navigation",
        "nav_eq_settings": "EQ Settings",
        "nav_alsa_settings": "ALSA Settings",
        "eq_page_title": "Totton Audio Control",
        "eq_page_subtitle": "EQ control, phase switching, and live DSP status.",
        "section_system_status": "System Status",
        "label_state": "State",
        "label_input_rate": "Input Rate",
        "label_output_rate": "Output Rate",
        "label_xrun_total": "XRUN Total",
        "label_uptime": "Uptime",
        "label_phase": "Phase",
        "status_running": "RUNNING",
        "status_stopped": "STOPPED",
        "section_dsp_controls": "DSP Controls",
        "btn_minimum": "Minimum",
        "btn_linear": "Linear",
        "btn_reload": "Reload",
        "btn_soft_reset": "Soft Reset",
        "btn_refresh_status": "Refresh Status",
        "section_active_profile": "Active Profile",
        "label_none": "None",
        "text_profile_active": "Profile is active",
        "text_profile_inactive": "No EQ profile active",
        "badge_on": "ON",
        "badge_off": "OFF",
        "label_source": "Source",
        "label_modern_target": "Modern Target",
        "label_kb5000": "KB5000_7",
        "btn_deactivate_eq": "Deactivate EQ",
        "section_upload_validate": "Upload & Validate",
        "label_eq_profile_file": "EQ Profile (.txt)",
        "btn_validate": "Validate",
        "btn_import": "Import",
        "text_validation_passed": "Validation passed:",
        "text_filters_suffix": "filters.",
        "label_preamp_from_file": "Preamp (from file)",
        "label_recommended_preamp": "Recommended Preamp (Headroom)",
        "section_opra_search": "OPRA Headphone Search",
        "btn_apply_opra": "Apply OPRA EQ",
        "text_eq_data_prefix": "EQ data:",
        "text_opra_project": "OPRA Project",
        "text_opra_license_suffix": "(CC BY-SA 4.0)",
        "section_saved_profiles": "Saved Profiles",
        "text_filters_count": "filters",
        "btn_activate": "Activate",
        "text_no_saved_profiles": "No saved profiles yet.",
        "section_opra_sync": "OPRA Sync",
        "label_status": "Status",
        "label_current_commit": "Current Commit",
        "label_previous_commit": "Previous Commit",
        "label_downloaded_at": "Downloaded At",
        "label_last_error": "Last Error",
        "label_source_target": "Source / Target",
        "placeholder_latest_or_sha": "latest or commit SHA",
        "btn_check_updates": "Check Updates",
        "btn_update": "Update",
        "btn_rollback": "Rollback",
        "opra_status_running": "Running",
        "opra_status_ready": "Ready",
        "opra_status_failed": "Failed",
        "opra_status_idle": "Idle",
        "opra_badge_running": "RUNNING",
        "opra_badge_ready": "READY",
        "opra_badge_failed": "FAILED",
        "opra_badge_idle": "IDLE",
        "text_dash": "—",
        "label_unknown": "unknown",
        "text_latest_available": "Latest available: {value}",
        "confirm_start_opra_update": "Start OPRA update ({target})?",
        "confirm_opra_rollback": "Rollback OPRA database to previous version?",
        "message_opra_update_started": "OPRA update started",
        "message_opra_rollback_started": "OPRA rollback started",
        "error_failed_check_updates": "Failed to check updates",
        "error_failed_start_update": "Failed to start update",
        "error_failed_start_rollback": "Failed to start rollback",
        "error_search_failed": "Search failed",
        "error_no_eq_profile_selected": "No EQ profile selected",
        "error_failed_apply_opra": "Failed to apply OPRA EQ",
        "message_opra_eq_applied": "OPRA EQ applied",
        "error_upload_failed": "Upload failed",
        "error_phase_update_failed": "Phase update failed",
        "message_phase_updated": "Phase updated",
        "message_reload_requested": "Reload requested",
        "message_soft_reset_requested": "Soft reset requested",
        "error_command_failed": "Command failed",
        "unit_hz": "Hz",
        "unit_hour_short": "h",
        "unit_minute_short": "m",
        "unit_second_short": "s",
        "label_search_headphones": "Search Headphones",
        "placeholder_search_headphones": "e.g. HD650, DT770, AirPods...",
        "label_eq_variation": "EQ Variation",
        "label_opra_modern_target": "Modern Target (KB5000_7)",
        "text_opra_modern_target": "Correct to the latest target curve.",
        "alsa_page_title": "ALSA Device Settings",
        "alsa_page_subtitle": "Select capture/playback devices and persist settings.",
        "section_device_selection": "Device Selection",
        "label_input_device": "Input Device",
        "label_output_device": "Output Device",
        "notice_devices_empty": "Device list is empty or unavailable. Ensure the DSP daemon is running.",
        "section_stream_parameters": "Stream Parameters",
        "label_sample_rate": "Sample Rate (Hz)",
        "placeholder_auto": "Auto",
        "label_channels": "Channels",
        "placeholder_channels_default": "2",
        "label_format": "Format",
        "label_period_frames": "Period Frames",
        "label_buffer_frames": "Buffer Frames",
        "section_filter_settings": "Filter Settings",
        "label_upsample_ratio": "Upsample Ratio",
        "label_phase_type": "Phase Type",
        "label_filter_directory": "Filter Directory",
        "placeholder_filter_directory": "/opt/totton-dsp/data/coefficients",
        "section_apply_settings": "Apply Settings",
        "btn_save_apply": "Save & Apply",
        "btn_reload_devices": "Reload Devices",
        "btn_refresh_config": "Refresh Config",
        "notice_reload_devices_restart": "Reload Devices restarts the DSP container to pick up newly connected USB devices.",
        "ratio_1x_bypass": "1x (Bypass)",
        "ratio_2x": "2x",
        "ratio_4x": "4x",
        "ratio_8x": "8x",
        "ratio_16x": "16x",
        "phase_minimum": "Minimum",
        "phase_linear": "Linear",
        "error_failed_load_devices": "Failed to load devices",
        "error_failed_load_config": "Failed to load config",
        "error_failed_save_config": "Failed to save config",
        "error_failed_restart_dsp": "Failed to restart DSP",
        "error_daemon_not_ready": "DSP daemon did not come back online.",
        "message_saved_restart_required": "Saved. Restart required to apply ALSA changes.",
        "message_saved_applied": "Saved and applied.",
        "message_restart_in_progress": "Restarting DSP to reload devices...",
        "message_devices_reloaded": "Devices reloaded.",
    }
}


def normalize_locale(value: str | None) -> str | None:
    if not value:
        return None
    token = value.split(",")[0].split(";")[0].strip().lower()
    if not token:
        return None
    return token.replace("_", "-")


def pick_locale(request: Request) -> str:
    candidates = [
        request.query_params.get("lang"),
        request.cookies.get("lang"),
        request.headers.get("accept-language"),
    ]
    for candidate in candidates:
        normalized = normalize_locale(candidate)
        if not normalized:
            continue
        base = normalized.split("-")[0]
        if base in SUPPORTED_LOCALES:
            return base
    return DEFAULT_LOCALE


def get_translations(locale: str) -> Dict[str, str]:
    return TRANSLATIONS.get(locale, TRANSLATIONS[DEFAULT_LOCALE])


def build_context(request: Request) -> Dict[str, object]:
    locale = pick_locale(request)
    translations = get_translations(locale)

    def t(key: str) -> str:
        return translations.get(key, key)

    translations_json = json.dumps(translations, ensure_ascii=True)
    return {
        "locale": locale,
        "t": t,
        "translations_json": translations_json,
    }
