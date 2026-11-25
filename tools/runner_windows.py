# SPDX-License-Identifier: GPL-2.0-or-later
"""The Windows data of a test runner that compiles the sources of src/.

The sources take the tables of KERNEL, USER and GDI (code pages, the case map
of the atom table, the message table, resources and the like) from the files
of an installed Windows 98 when they are compiled with NEO98_RUNTIME_CONTENT,
as the hosts are. Otherwise they include headers such as
build/generated/atom_upper.h, which the import tools of the private
repository make and a clean checkout does not have. A runner compiled with
FLAGS reads the tables at its start, as the hosts do
(tools/runner_windows_data.h), from setup's drive C:, and takes Fixedsys out
of the drive's VGAFIX.FON as its system font.

    environment, font = runner_windows.prepare(OUT)
"""
from pathlib import Path
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import win98_c  # noqa: E402

# The registry starts empty as in the hosts before they read the hives; the
# image of an installation's registry is one of those generated headers.
FLAGS = ['-DNEO98_RUNTIME_CONTENT', '-DNEO98_NO_REGISTRY_IMAGE']


def first_font(fon):
    """The first RT_FONT resource of an NE font file, cut to the size its
    header declares (dfSize), as HostSystemFont::FirstFont takes it."""
    word = lambda at: struct.unpack_from('<H', fon, at)[0]
    if fon[:2] != b'MZ':
        raise ValueError('not an NE font file')
    ne = struct.unpack_from('<I', fon, 60)[0]
    if fon[ne:ne + 2] != b'NE':
        raise ValueError('not an NE font file')
    at = ne + word(ne + 36)
    shift = word(at)
    at += 2
    while word(at):
        kind, count = word(at), word(at + 2)
        at += 8
        for _ in range(count):
            offset, length = word(at) << shift, word(at + 2) << shift
            at += 12
            if kind == 0x8008:
                declared = struct.unpack_from('<I', fon, offset + 2)[0]
                if not 118 <= declared <= length:
                    raise ValueError('a damaged font resource')
                return fon[offset:offset + declared]
    raise ValueError('no font resource')


def prepare(out):
    """The variables of the runner's environment and the file of its system
    font, written into out."""
    drive = win98_c.use_pristine()
    fon = win98_c.path('C:\\WINDOWS\\FONTS\\VGAFIX.FON', drive)
    if not fon:
        raise SystemExit(f'No C:\\WINDOWS\\FONTS\\VGAFIX.FON on {drive}')
    font = Path(out) / 'fixedsys.fnt'
    font.write_bytes(first_font(fon.read_bytes()))
    return {'NEO98_RUNNER_DRIVE': str(drive)}, font
