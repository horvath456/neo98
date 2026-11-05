# SPDX-License-Identifier: GPL-2.0-or-later
"""Host-test fixtures and explicit skips for optional inputs."""
import argparse
from pathlib import Path


def active_caption_pair(left, right):
    """The default Win98 active caption is navy, or navy fading to blue."""
    if left == right == '000080':
        return True
    first, second = bytes.fromhex(left), bytes.fromhex(right)
    def on_gradient(rgb):
        red, green, blue = rgb
        # COLOR_ACTIVECAPTION (0,0,128) to COLOR_GRADIENTACTIVECAPTION
        # (16,132,208). Original USER uses 7-bit fixed-point increments for
        # two-pixel stripes (win98_draw_caption.h). These dialogs are under
        # 512 pixels wide: accumulated truncation is less than three channel
        # values. Both channels contribute to the cross-product error.
        return (0 <= red <= 16 and 0 <= green <= 132 and 128 <= blue <= 208
                and abs(132 * red - 16 * green) <= 3 * (132 + 16)
                and abs(132 * (blue - 128) - 80 * green) <= 3 * (132 + 80))
    return (on_gradient(first) and on_gradient(second)
            and first[1] < second[1] and first[2] < second[2])


def write_plain_desktop_registry(path):
    """Give pixel tests a fresh teal desktop without an imported wallpaper."""
    import struct
    from win98_folder import JOURNAL_SIGNATURE, journal_record
    data = bytearray(JOURNAL_SIGNATURE)
    for key, name, value in (
        (r'HKEY_USERS\.Default\Control Panel\Desktop', 'Wallpaper', ''),
        (r'HKEY_USERS\.Default\Control Panel\Desktop', 'Pattern', ''),
        (r'HKEY_USERS\.Default\Control Panel\Colors', 'Background', '0 128 128'),
    ):
        raw = value.encode('ascii') + b'\0'
        data += journal_record(3, key, name) + struct.pack('<II', 1, len(raw)) + raw
    Path(path).write_bytes(data)


def skip(reason):
    print(f'SKIP: {reason}', flush=True)
    raise SystemExit(77)


def require_file(path):
    path = Path(path)
    if not path.exists():
        skip(f'optional test input is missing: {path}')
    return path


class ExternalInputParser(argparse.ArgumentParser):
    """Missing external fixture arguments skip; invalid arguments still fail."""
    def error(self, message):
        if message.startswith('the following arguments are required:'):
            skip('supply the external test inputs: ' + message.split(':', 1)[1].strip())
        super().error(message)


def compile_native(command, **kwargs):
    """Cache legacy multi-source test builds without changing flags or link order."""
    import os
    import subprocess
    from native_build import compile_objects
    root = Path(__file__).resolve().parents[1]
    sources = [Path(arg) for arg in command[1:] if str(arg).endswith(('.cpp', '.cc', '.S'))]
    if len(sources) < 2:
        return subprocess.run(command, **kwargs)
    flags = []
    skip_next = False
    for arg in command[1:]:
        arg = str(arg)
        if skip_next:
            skip_next = False
            continue
        if arg in ('-o', '-framework', '-L', '-l'):
            skip_next = True
        elif arg.startswith(('-Wl,', '-l', '-L')) or Path(arg) in sources or arg.endswith(('.o', '.a', '.dylib', '.so', '.so.2')):
            continue
        else:
            flags.append(arg)
    env = kwargs.get('env', os.environ)
    objects = compile_objects(root, sources, flags, env, root / 'build/native-app/object-cache')
    replacements = dict(zip(map(str, sources), map(str, objects)))
    return subprocess.run([replacements.get(str(arg), str(arg)) for arg in command], **kwargs)


def printer_inputs_reason():
    """Return the missing printer input, without aborting other test cases."""
    from import_printer_driver import DEFAULT_ISO, FILES, OUT
    if all((OUT / 'files' / name).is_file() for name in FILES):
        return
    if not DEFAULT_ISO.is_file():
        return ('printer tests require extracted build/printer-driver/files or the Win98 SE CD; '
                'run tools/import_printer_driver.py --iso PATH')
    return None


def require_printer_inputs():
    reason = printer_inputs_reason()
    if reason:
        skip(reason)


def available_printer_cases(names, printer_names, only=None):
    """Skip unavailable printer cases while retaining independent results."""
    selected = [name for name in names if not only or name in only]
    blocked = set(selected) & set(printer_names)
    reason = printer_inputs_reason() if blocked else None
    if not reason:
        return selected
    remaining = []
    for name in selected:
        if name in blocked:
            print(f'SKIP {name}: {reason}', flush=True)
        else:
            remaining.append(name)
    if not remaining:
        skip(reason)
    return remaining
