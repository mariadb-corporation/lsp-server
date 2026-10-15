"""Helpers shared by tools/stub.py and tools/lexslice.py.

Usage from a sibling script::

    import sys
    from pathlib import Path
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import common
"""

from __future__ import annotations

import json
from pathlib import Path

# libs/mariadbparser, derived from this file's location (tools/common.py is
# one directory below it). lexslice.py compiles the state-map generator
# against its include/ and src/.
REPO_ROOT: Path = Path(__file__).resolve().parent.parent


def write_json(path: Path, obj) -> None:
    """Write `obj` as JSON to `path`: sorted keys, 2-space indent, trailing
    newline, parent directories created as needed."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(obj, sort_keys=True, indent=2) + "\n")
