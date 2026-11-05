# SPDX-License-Identifier: GPL-2.0-or-later
"""Locations of user-supplied test inputs. No developer-machine defaults."""
import os
import sys
from pathlib import Path


def external_path(variable):
    options = {'NEO98_COLLECTION': ('--collection', '--drive-d'),
               'NEO98_GAME_CORPUS': ('--home',)}
    for option in options.get(variable, ()):
        for index, arg in enumerate(sys.argv[1:], 1):
            if arg.startswith(option + '='):
                return Path(arg.split('=', 1)[1]).expanduser()
            if arg == option and index + 1 < len(sys.argv):
                return Path(sys.argv[index + 1]).expanduser()
    value = os.environ.get(variable)
    if not value:
        raise RuntimeError(f'Set {variable} to the directory containing your external test inputs')
    return Path(value).expanduser()
