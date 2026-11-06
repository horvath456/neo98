# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Writing a generated header that compiles may be reading at the same time."""
import os
from pathlib import Path


def write_text(path, text):
    """Write `text` to `path` only when it differs from what is there, through
    a temporary file and a rename. An unchanged file keeps its time, so the
    object cache of a test running beside the generator does not see a
    dependency change; a changed one is never seen half written."""
    path = Path(path)
    try:
        if path.read_text() == text:
            return False
    except OSError:
        pass
    temporary = path.with_name(f'.{path.name}.{os.getpid()}')
    temporary.write_text(text)
    os.replace(temporary, path)
    return True
