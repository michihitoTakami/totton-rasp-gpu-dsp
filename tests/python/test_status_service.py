import json
from pathlib import Path

from web.services.daemon import load_stats


def test_load_stats_reads_rates_and_xrun(tmp_path: Path):
    stats_path = tmp_path / "stats.json"
    payload = {
        "input_rate": 48000,
        "output_rate": 192000,
        "audio": {"xrun": {"total": 3}},
    }
    stats_path.write_text(json.dumps(payload))

    stats = load_stats(stats_path)

    assert stats["input_rate"] == 48000
    assert stats["output_rate"] == 192000
    assert stats["xrun_total"] == 3


def test_load_stats_falls_back_to_xrun_count(tmp_path: Path):
    stats_path = tmp_path / "stats.json"
    payload = {"input_rate": 44100, "output_rate": 88200, "xrun_count": 7}
    stats_path.write_text(json.dumps(payload))

    stats = load_stats(stats_path)

    assert stats["xrun_total"] == 7
