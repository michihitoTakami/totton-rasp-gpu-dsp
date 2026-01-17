import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))


@pytest.fixture
def coefficients_dir():
    """Path to filter coefficients directory."""
    return ROOT / "data" / "coefficients"
