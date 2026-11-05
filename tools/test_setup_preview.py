#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Bounded live frames, independent OCR, reboot cleanup, and headless QEMU capture."""
from pathlib import Path
import os
import shutil
import sys
import struct
import subprocess
import tempfile
import threading
import time
from unittest.mock import patch
import zlib

import setup_win98 as setup


def decode_png(path):
    data = path.read_bytes()
    assert data[:8] == b'\x89PNG\r\n\x1a\n'
    offset, compressed = 8, b''
    while offset < len(data):
        size = struct.unpack('>I', data[offset:offset + 4])[0]
        kind = data[offset + 4:offset + 8]
        payload = data[offset + 8:offset + 8 + size]
        assert zlib.crc32(kind + payload) == struct.unpack('>I', data[offset + 8 + size:offset + 12 + size])[0]
        if kind == b'IHDR':
            width, height = struct.unpack('>II', payload[:8])
        if kind == b'IDAT':
            compressed += payload
        offset += size + 12
    rows = zlib.decompress(compressed)
    assert len(rows) == height * (width * 3 + 1)
    return width, height, rows


def main():
    assert shutil.disk_usage('.').free >= 8 * 1024**3, 'Need at least 8 GiB free before testing'
    with tempfile.TemporaryDirectory(prefix='neo98-preview-') as directory:
        work = Path(directory)
        preview = work / 'live.png'
        live = setup.SetupPreview(work / 'monitor.sock', work, preview)
        entered, release = threading.Event(), threading.Event()
        color = [b'\xff\x00\x00']
        def capture(monitor, screen):
            screen.write_bytes(b'P6\n2 1\n255\n' + color[0] * 2)
            return True
        def ocr(*args, **kwargs):
            entered.set()
            assert release.wait(3)
            return 'user information'
        try:
            with patch.object(setup, 'capture_screen', capture), patch.object(live.ocr_processes, 'read', ocr):
                live.frame()
                assert decode_png(preview) == (2, 1, b'\0' + color[0] * 2)
                assert live.text() == '' and entered.wait(1)
                before = time.monotonic()
                color[0] = b'\x00\x00\xff'
                for _ in range(3):
                    live.frame()
                    assert decode_png(preview)[2] == b'\0' + color[0] * 2
                assert time.monotonic() - before < 1, 'OCR blocks live frames'
                assert not live.pending.exists()
                assert len(list(work.iterdir())) == 3, 'Frame history grew'
                assert preview.stat().st_mode & 0o777 == 0o600
                release.set()
                live.future.result(timeout=2)
                assert 'user information' in live.text()
                live.reset()
                assert list(work.iterdir()) == [], 'Reboot retained screenshots'
                live.frame()
                entered.clear(); release.clear(); live.next_ocr = 0
                assert live.text() == '' and entered.wait(1)
                before = time.monotonic()
                live.reset()
                live.frame()
                assert time.monotonic() - before < 1, 'Reboot waits for old OCR'
                release.set()
                live.future.result(timeout=2)
                assert live.text() == '', 'Old boot OCR result was reused'
        finally:
            release.set()
            live.close()
        assert list(work.iterdir()) == [], 'Completion retained screenshots'

        # Run actual children, including a QEMU stand-in that ignores SIGTERM.
        # Cancellation must reap both before Linux's five-second escalation.
        with tempfile.TemporaryDirectory(prefix='neo98-preview-children-') as child_directory:
            child_work = Path(child_directory)
            ocr_ready, qemu_ready = child_work / 'ocr-ready', child_work / 'qemu-ready'
            child_code = ("import signal, time; from pathlib import Path; "
                          "signal.signal(signal.SIGTERM, signal.SIG_IGN); "
                          "Path(__import__('sys').argv[1]).touch(); time.sleep(60)")
            children = []
            popen = subprocess.Popen
            frame = setup.SetupPreview.frame

            def launch(args, **kwargs):
                if args[0] == 'tesseract':
                    args = [sys.executable, '-c', child_code, str(ocr_ready)]
                child = popen(args, **kwargs)
                children.append(child)
                return child

            def cancel_during_frame(live):
                frame(live)
                live.text()
                deadline = time.monotonic() + 3
                while not (ocr_ready.exists() and qemu_ready.exists()):
                    assert time.monotonic() < deadline, 'Children did not start'
                    assert shutil.disk_usage('.').free >= 8 * 1024**3
                    time.sleep(.01)
                assert live.ocr.exists() and preview.exists()
                assert all(child.poll() is None for child in children)
                raise KeyboardInterrupt

            before = time.monotonic()
            try:
                with patch.object(setup.subprocess, 'Popen', launch), \
                     patch.object(setup, 'qemu_args', return_value=[sys.executable, '-c', child_code, str(qemu_ready)]), \
                     patch.object(setup, 'capture_screen', capture), \
                     patch.object(setup.SetupPreview, 'frame', cancel_during_frame):
                    try:
                        setup.run_setup(None, None, None, work, 5, preview=preview)
                    except KeyboardInterrupt:
                        pass
                    else:
                        raise AssertionError('Cancellation swallowed')
                assert time.monotonic() - before < 4, 'Cancellation exceeds cleanup budget'
                assert len(children) == 2, 'Cancellation started another OCR pass'
                for child in children:
                    assert child.returncode is not None, 'Child was not reaped'
                    try:
                        os.waitpid(child.pid, os.WNOHANG)
                    except ChildProcessError:
                        pass
                    else:
                        raise AssertionError('Child still belongs to the parent')
                assert not list(work.glob('*.ppm')), 'Cancellation retained private OCR images'
                assert not preview.exists() and not preview.with_name(preview.name + '.tmp').exists()
            finally:
                for child in children:
                    if child.poll() is None:
                        child.kill()
                    child.wait(timeout=2)
            (work / 'qemu.log').unlink()

            # close must also cancel OCR when unwinding outside the QEMU loop.
            children.clear()
            ocr_ready.unlink()
            before = time.monotonic()
            try:
                with patch.object(setup.subprocess, 'Popen', launch), \
                     patch.object(setup, 'capture_screen', capture), \
                     patch.object(setup, '_run_setup', lambda *args: cancel_during_frame(args[6])):
                    try:
                        setup.run_setup(None, None, None, work, 5, preview=preview)
                    except KeyboardInterrupt:
                        pass
                    else:
                        raise AssertionError('Cancellation swallowed')
                assert time.monotonic() - before < 2, 'close waits for OCR timeout'
                assert len(children) == 1 and children[0].returncode is not None
                assert list(work.iterdir()) == [], 'close retained private screenshots'
                try:
                    os.waitpid(children[0].pid, os.WNOHANG)
                except ChildProcessError:
                    pass
                else:
                    raise AssertionError('close did not reap OCR')
            finally:
                for child in children:
                    if child.poll() is None:
                        child.kill()
                    child.wait(timeout=2)
        assert list(work.iterdir()) == [], 'Cancellation retained screenshots'

        qemu = shutil.which('qemu-system-i386')
        if qemu:
            assert shutil.disk_usage('.').free >= 8 * 1024**3
            monitor = work / 'monitor.sock'
            process = subprocess.Popen([qemu, '-display', 'none', '-m', '16', '-nic', 'none',
                                        '-monitor', f'unix:{monitor},server=on,wait=off'],
                                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            live = setup.SetupPreview(monitor, work, preview)
            try:
                deadline = time.monotonic() + 8
                while time.monotonic() < deadline and not preview.exists():
                    assert shutil.disk_usage('.').free >= 8 * 1024**3
                    assert process.poll() is None, 'Headless QEMU exited'
                    live.frame()
                    time.sleep(.1)
                width, height, _ = decode_png(preview)
                assert width >= 320 and height >= 200
                setup.send_keys(monitor, 'ret')
                live.frame()
                decode_png(preview)
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill(); process.wait()
                live.close()
                monitor.unlink(missing_ok=True)
            print('Real headless QEMU capture: PASS')
        else:
            print('Real headless QEMU capture: SKIP (qemu-system-i386 missing)')
    assert shutil.disk_usage('.').free >= 8 * 1024**3
    print('SETUP PREVIEW PASS: PNG pixels, bounded publication, independent OCR, reboot and cancellation cleanup')


if __name__ == '__main__':
    main()
