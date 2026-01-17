"""Constants for the Totton Raspberry Pi GPU DSP Web API."""

import re
from pathlib import Path

WEB_DIR = Path(__file__).parent
PROJECT_ROOT = WEB_DIR.parent
CONFIG_PATH = PROJECT_ROOT / "config.json"
EQ_PROFILES_DIR = PROJECT_ROOT / "data" / "EQ"

ZEROMQ_IPC_PATH = "ipc:///tmp/totton_zmq.sock"

MAX_EQ_FILE_SIZE = 1 * 1024 * 1024  # 1MB
MAX_EQ_FILTERS = 100
PREAMP_MIN_DB = -100.0
PREAMP_MAX_DB = 20.0
FREQ_MIN_HZ = 10.0
FREQ_MAX_HZ = 24000.0
GAIN_MIN_DB = -30.0
GAIN_MAX_DB = 30.0
Q_MIN = 0.01
Q_MAX = 100.0

SAFE_FILENAME_PATTERN = re.compile(r"^[a-zA-Z0-9_\-\.]+\.txt$")
SAFE_PROFILE_NAME_PATTERN = re.compile(r"^[a-zA-Z0-9_\-\.]+$")
