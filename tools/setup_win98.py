#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Set up drive C: of NEO98 once, from a supported Windows 98 SE ISO.

`python3 neo98.py setup --iso...` accepts known ISO hashes and other images
whose setup metadata and system files match tools/win98_isos.json. It
installs Windows 98 headless in QEMU onto a raw
disk under build/setup-win98/<ISO-checksum>/, then copies the installation
once into the Win98 folder (build/win98-c, tools/win98_c.py) with the real
8.3 aliases in its sidecars, imports the registry, generates every asset that
comes from Win98 files, records the SHA-256 of every file and writes the
done-marker last. After that nothing reads the disk image again; the folder is
drive C:. The original ISO stays read-only, and all Windows files and the
temporary answer file remain under ignored build/. QEMU has no visible
display; DOS and the installed guest report milestones through its first
serial port. README.md.
"""
from __future__ import annotations

import getpass
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
sys.path.insert(0, str(ROOT / "tools"))
import win98_c    # noqa: E402
import win98_time_zone    # noqa: E402
import win98_glide_driver    # noqa: E402
import win98_cabs    # noqa: E402
import win98_iso    # noqa: E402
import win98_source    # noqa: E402
SECTOR = 512
CD_SECTOR = 2048
DISK_BYTES = 2 * 1024 * 1024 * 1024
PARTITION_START = 2048
PARTITION_OFFSET = PARTITION_START * SECTOR
MILESTONES = {
    "NEO98_DOS_BOOT": "DOS started",
    "NEO98_CD_OK": "installation CD found",
    "NEO98_SYS_OK": "C: made bootable",
    "NEO98_SETUP_START": "Windows Setup started",
    "NEO98_SETUP_COMPLETE": "Windows reached the first desktop",
}


def require_commands():
    use_toolkit()
    missing = [name for name in TOOLS if not shutil.which(name)]
    if missing:
        install = "sudo apt install qemu-system-x86 mtools cabextract tesseract-ocr" if sys.platform.startswith("linux") else BREW_LINE
        raise RuntimeError("Missing " + ", ".join(missing) + ". Install: " + install)


def check_boot_files(boot_image, scratch_root=None):
    with tempfile.TemporaryDirectory(dir=scratch_root or BUILD) as temp:
        floppy = Path(temp) / "boot.img"
        private_write(floppy, boot_image)
        required = ("IO.SYS", "MSDOS.SYS", "COMMAND.COM", "HIMEM.SYS",
                    "OAKCDROM.SYS", "MSCDEX.EXE", "FDISK.EXE", "EBD.CAB")
        for name in required:
            result = subprocess.run(("mdir", "-a", "-i", str(floppy), f"::{name}"),
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if result.returncode:
                raise RuntimeError(f"ISO boot image is missing {name}")


class NoBootImage(RuntimeError):
    """A valid installation CD has no embedded El Torito start image."""


def inspect_iso(iso):
    """Accept a BIOS El Torito floppy image and the standard WIN98 setup tree."""
    win98_iso.setup_files(iso)
    with iso.open("rb") as image:
        volume_bytes, catalog = None, None
        for block in range(16, 64):
            image.seek(block * CD_SECTOR)
            header = image.read(CD_SECTOR)
            if len(header) != CD_SECTOR or header[1:7] != b"CD001\x01":
                raise RuntimeError("Truncated or invalid ISO9660 volume descriptors")
            if header[0] == 1:
                volume_bytes = struct.unpack_from("<I", header, 80)[0] * CD_SECTOR
            if header[:7] == b"\x00CD001\x01" and header[7:39].rstrip(b"\0 ") == b"EL TORITO SPECIFICATION":
                catalog = struct.unpack_from("<I", header, 71)[0]
            if header[0] == 255:
                break
        if volume_bytes is None:
            raise RuntimeError("Expected an ISO9660 Windows 98 CD")
        if catalog is None:
            raise NoBootImage("ISO has no El Torito boot catalog")
        if (catalog + 1) * CD_SECTOR > volume_bytes:
            raise RuntimeError("El Torito boot catalog lies outside the ISO volume")
        image.seek(catalog * CD_SECTOR)
        table = image.read(CD_SECTOR)
        if len(table) != CD_SECTOR:
            raise RuntimeError("Truncated El Torito boot catalog")
        validation, boot = table[:32], table[32:64]
        if (validation[0] != 1 or validation[1] != 0 or validation[30:32] != b"\x55\xaa"
                or sum(struct.unpack("<16H", validation)) & 0xffff):
            raise RuntimeError("Invalid x86 El Torito catalog")
        floppy_bytes = {1: 1200 * 1024, 2: 1440 * 1024, 3: 2880 * 1024}.get(boot[1])
        if boot[0] != 0x88 or not floppy_bytes:
            raise RuntimeError("This ISO needs a BIOS El Torito floppy boot image")
        boot_offset = struct.unpack_from("<I", boot, 8)[0] * CD_SECTOR
        if boot_offset + floppy_bytes > volume_bytes:
            raise RuntimeError("El Torito floppy boot image lies outside the ISO volume")
        image.seek(boot_offset)
        floppy = image.read(floppy_bytes)
        if len(floppy) != floppy_bytes or floppy[510:512] != b"\x55\xaa":
            raise RuntimeError("Truncated El Torito floppy boot image")
        return floppy


def private_write(path, data):
    fd = os.open(path, os.O_CREAT | os.O_WRONLY | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "wb") as output:
        output.write(data)


def run(*args, **kwargs):
    # mtools prompts on a terminal even with -o. Keep all child stdio off the
    # terminal, then relay their finished output to the caller.
    kwargs.setdefault("stdin", subprocess.DEVNULL)
    kwargs.setdefault("stdout", subprocess.PIPE)
    kwargs.setdefault("stderr", subprocess.STDOUT)
    kwargs.setdefault("text", True)
    kwargs.setdefault("start_new_session", True)
    result = subprocess.run(args, **kwargs)
    if result.stdout:
        print(result.stdout, end="" if result.stdout.endswith("\n") else "\n", flush=True)
    result.check_returncode()
    return result


def fat_copy(image, content, destination, scratch):
    source = scratch / destination.rsplit("/", 1)[-1]
    private_write(source, content)
    try:
        run("mcopy", "-o", "-i", str(image), str(source), destination)
    finally:
        source.unlink(missing_ok=True)


def dos_text(text):
    return text.strip().replace("\n", "\r\n").encode("ascii") + b"\r\n"


def prepare_disk(path):
    """Make a sparse, active FAT32 MBR disk that mtools and Setup both read."""
    with path.open("wb") as disk:
        disk.truncate(DISK_BYTES)
    mbr = bytearray(SECTOR)
    count = DISK_BYTES // SECTOR - PARTITION_START
    mbr[446:462] = (bytes((0x80, 0, 1, 0, 0x0c, 0xfe, 0xff, 0xff))
                    + struct.pack("<II", PARTITION_START, count))
    mbr[510:512] = b"\x55\xaa"
    with path.open("r+b") as disk:
        disk.write(mbr)
    run("mformat", "-i", f"{path}@@{PARTITION_OFFSET}", "-F", "-v", "NEO98", "::")


def answer_file(product_key):
    # The layout and keys are Windows 98's Batch 98 format, not NT's unattend.xml.
    return dos_text(f'''
[BatchSetup]
Version=3.0 (32-bit)

[Version]
Signature="$CHICAGO$"
AdvancedINF=2.5
LayoutFile=layout.inf

[Setup]
Express=1
InstallDir="C:\\WINDOWS"
InstallType=3
ProductKey="{product_key}"
EBD=0
ShowEula=0
ChangeDir=0
OptionalComponents=0
Network=0
System=0
CCP=0
CleanBoot=0
Display=0
DevicePath=0
NoDirWarn=1
TimeZone="{win98_time_zone.ZONE}"
Uninstall=0
NoPrompt2Boot=1

[System]
Locale=L0409
SelectedKeyboard=KEYBOARD_00000409

[NameAndOrg]
Name="NEO98"
Org=""
Display=0

[Network]
ComputerName="NEO98"
Workgroup="WORKGROUP"
Display=0

[Install]
AddReg=NEO98.RunOnce

[NEO98.RunOnce]
HKLM,"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce","NEO98Finish",,"C:\\COMMAND.COM /C C:\\NEO98\\FINISH.BAT"
''')


def prepare_floppy(floppy, boot_image, product_key, scratch):
    private_write(floppy, boot_image)
    # JO.SYS is the CD's three-second boot selector. It defaults to the hard
    # disk and would silently skip our AUTOEXEC.BAT in a headless run.
    if subprocess.run(("mdir", "-a", "-i", str(floppy), "::JO.SYS"),
                      stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0:
        run("mdel", "-i", str(floppy), "::JO.SYS")
    boot_cab = scratch / "EBD.CAB"
    run("mcopy", "-i", str(floppy), "::EBD.CAB", str(boot_cab))
    system = subprocess.run(("cabextract", "-p", "-F", "SYS.COM", str(boot_cab)),
                            check=True, stdout=subprocess.PIPE).stdout
    if len(system) < 1000:
        raise RuntimeError("Boot image has no usable SYS.COM in EBD.CAB")
    fat_copy(floppy, system, "::SYS.COM", scratch)
    fat_copy(floppy, dos_text('''
DEVICE=A:\\HIMEM.SYS /TESTMEM:OFF
DEVICE=A:\\OAKCDROM.SYS /D:OEMCD001
FILES=40
BUFFERS=20
LASTDRIVE=Z
'''), "::CONFIG.SYS", scratch)
    fat_copy(floppy, dos_text('''
@ECHO OFF
PATH=A:\\;D:\\WIN98
ECHO NEO98_DOS_BOOT>COM1
A:\\MSCDEX.EXE /D:OEMCD001 /L:D
IF ERRORLEVEL 1 GOTO CDFAIL
ECHO NEO98_CD_OK>COM1
A:\\FDISK.EXE /MBR
IF ERRORLEVEL 1 GOTO MBRFAIL
A:\\SYS.COM C:
IF ERRORLEVEL 1 GOTO SYSFAIL
ECHO NEO98_SYS_OK>COM1
D:\\WIN98\\SMARTDRV.EXE 8192
ECHO NEO98_SETUP_START>COM1
D:\\WIN98\\SETUP.EXE A:\\MSBATCH.INF /IS /IE /IM
ECHO NEO98_SETUP_RETURNED>COM1
GOTO END
:CDFAIL
ECHO NEO98_CD_FAIL>COM1
GOTO END
:SYSFAIL
ECHO NEO98_SYS_FAIL>COM1
GOTO END
:MBRFAIL
ECHO NEO98_MBR_FAIL>COM1
:END
'''), "::AUTOEXEC.BAT", scratch)
    fat_copy(floppy, answer_file(product_key), "::MSBATCH.INF", scratch)


def stage_finish(disk, scratch):
    volume = f"{disk}@@{PARTITION_OFFSET}"
    run("mmd", "-i", volume, "::NEO98")
    fat_copy(volume, dos_text('''
@ECHO OFF
IF EXIST C:\\NEO98\\MSBATCH.INF DEL C:\\NEO98\\MSBATCH.INF
ECHO NEO98_SETUP_COMPLETE>C:\\NEO98.DONE
ECHO NEO98_SETUP_COMPLETE>COM1
RUNDLL32.EXE USER.EXE,ExitWindows
'''), "::/Neo98/FINISH.BAT", scratch)


def qemu_args(iso, disk, floppy, serial, monitor):
    firmware = toolkit() / "share/qemu" if toolkit() else None
    args = ["qemu-system-i386", *(["-L", str(firmware)] if firmware and firmware.is_dir() else []), "-M", "pc", "-accel", "tcg", "-cpu", "pentium2",
            "-m", "128", "-smp", "1", "-vga", "cirrus", "-nic", "none",
            "-display", "none", "-no-reboot", "-serial", f"file:{serial}",
            "-monitor", f"unix:{monitor},server=on,wait=off",
            "-drive", f"file={disk},format=raw,if=ide,index=0",
            "-drive", f"file={iso},format=raw,media=cdrom,if=ide,index=2,readonly=on"]
    if floppy is not None:
        args += ["-drive", f"file={floppy},format=raw,if=floppy,index=0",
                 "-boot", "once=a,order=c"]
    else:
        args += ["-boot", "order=c"]
    return args


def monitor_reply(connection):
    # HMP echoes each keystroke. One recv can contain only its first byte;
    # keep the connection alive until the command's next prompt arrives.
    reply = bytearray()
    while not re.search(rb"(?:^|[\r\n])\(qemu\) $", reply):
        chunk = connection.recv(4096)
        if not chunk:
            raise ConnectionError("QEMU monitor closed before its prompt")
        reply.extend(chunk)
        if len(reply) > 1 << 20:
            raise ConnectionError("QEMU monitor reply exceeds the limit")
    return bytes(reply)


def capture_screen(monitor, screen):
    screen.unlink(missing_ok=True)
    try:
        screen.touch(mode=0o600)
        with socket.socket(socket.AF_UNIX) as connection:
            connection.settimeout(3)
            connection.connect(str(monitor))
            monitor_reply(connection)
            # Escape HMP quoted strings, including paths containing quotes.
            quoted = str(screen).replace("\\", "\\\\").replace('"', '\\"')
            connection.sendall(f'screendump "{quoted}"\n'.encode())
            monitor_reply(connection)
        return screen.is_file()
    except (OSError, TimeoutError):
        screen.unlink(missing_ok=True)
        return False


class OCRProcesses:
    """Serialize child creation with cancellation so no OCR child escapes close."""
    def __init__(self):
        import threading
        self.lock = threading.Lock()
        self.cancelled = False
        self.process = None

    def read(self, screen, options):
        with self.lock:
            if self.cancelled:
                return ""
            process = subprocess.Popen(("tesseract", str(screen), "stdout", *options),
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            self.process = process
        try:
            stdout, _ = process.communicate(timeout=5)
            return stdout.lower()
        finally:
            # communicate's timeout does not kill or reap the child itself.
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=1)
            with self.lock:
                self.process = None

    def cancel(self):
        with self.lock:
            self.cancelled = True
            if self.process is not None and self.process.poll() is None:
                # OCR has no state to flush. Kill immediately, then let its
                # worker reap it before deleting the private input image.
                self.process.kill()


def screen_text(monitor, screen, ocr_processes=None):
    if isinstance(monitor, SetupPreview):
        return monitor.text()
    # OCR runs on a private copy. It never connects to QEMU from the worker.
    try:
        if monitor is not None:
            capture_screen(monitor, screen)
        if not screen.is_file():
            return ""
        reads = []
        ocr_processes = ocr_processes or OCRProcesses()
        for options in ((), ("--psm", "6")):
            reads.append(ocr_processes.read(screen, options))
        return "\n".join(reads)
    except (OSError, subprocess.TimeoutExpired):
        return ""
    finally:
        screen.unlink(missing_ok=True)


class SetupPreview:
    """One monitor owner; one bounded OCR job; one atomically replaced PNG."""
    def __init__(self, monitor, work, preview):
        from concurrent.futures import ThreadPoolExecutor
        self.monitor = monitor
        self.screen = work / "live-screen.ppm"
        self.ocr = work / "ocr-screen.ppm"
        self.preview = Path(preview) if preview is not None else None
        self.pending = self.preview.with_name(self.preview.name + ".tmp") if self.preview else None
        self.ocr_processes = OCRProcesses()
        self.executor = ThreadPoolExecutor(max_workers=1)
        self.future = None
        self.next_ocr = 0
        self.discard_ocr = False

    def __str__(self):
        return str(self.monitor)

    def unlink(self, **kwargs):
        return self.monitor.unlink(**kwargs)

    def frame(self):
        if not capture_screen(self.monitor, self.screen) or self.preview is None:
            return
        try:
            # QEMU writes binary PPM. PNG is readable by both AppKit and GTK.
            import zlib
            data = self.screen.read_bytes()
            match = re.match(rb"P6\s+(\d+)\s+(\d+)\s+255\s", data)
            if not match:
                return
            width, height = map(int, match.groups())
            if not (0 < width <= 4096 and 0 < height <= 4096):
                return
            pixels = data[match.end():]
            if len(pixels) != width * height * 3:
                return
            def chunk(kind, payload):
                return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
            stride = width * 3
            rows = b"".join(b"\0" + pixels[y:y + stride] for y in range(0, len(pixels), stride))
            png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                   + chunk(b"IDAT", zlib.compress(rows, 1)) + chunk(b"IEND", b""))
            with self.pending.open("wb") as stream:
                os.chmod(self.pending, 0o600)
                stream.write(png)
            self.pending.replace(self.preview)
        except OSError:
            # Losing the preview must not interrupt the guest installation.
            pass

    def text(self):
        if self.future is not None:
            if not self.future.done():
                return ""
            result = self.future.result()
            self.future = None
            if self.discard_ocr:
                self.discard_ocr = False
                return ""
            return result
        if time.monotonic() >= self.next_ocr and self.screen.is_file():
            self.ocr.touch(mode=0o600)
            shutil.copyfile(self.screen, self.ocr)
            self.future = self.executor.submit(screen_text, None, self.ocr, self.ocr_processes)
            self.next_ocr = time.monotonic() + 10
        return ""

    def reset(self):
        # Never use an OCR result from the previous guest boot.
        self.discard_ocr = self.future is not None
        self.next_ocr = 0
        ocr = self.ocr if self.future is None or self.future.done() else None
        for path in (self.screen, ocr, self.preview, self.pending):
            if path is not None:
                path.unlink(missing_ok=True)

    def close(self):
        self.ocr_processes.cancel()
        try:
            self.executor.shutdown(wait=True, cancel_futures=True)
        finally:
            self.reset()


def run_setup(iso, disk, floppy, work, timeout, resume=False, preview=None, bootstrap=False):
    live = SetupPreview(work / "monitor.sock", work, preview)
    try:
        return _run_setup(iso, disk, floppy, work, timeout, resume, live, bootstrap)
    finally:
        live.close()


def send_keys(monitor, *keys):
    with socket.socket(socket.AF_UNIX) as connection:
        connection.settimeout(5)
        connection.connect(str(monitor))
        monitor_reply(connection)
        for key in keys:
            connection.sendall(f"sendkey {key}\n".encode())
            monitor_reply(connection)
            time.sleep(0.25)


def handle_setup_dialog(monitor, screen, handled):
    visible = screen_text(monitor, screen)
    if "product key you entered is invalid" in visible:
        raise RuntimeError("Windows Setup rejected the product key")
    dialogs = (
        ("user information", ("ret",), "user information confirmed"),
        ("license agreement", ("tab", "space", "ret"), "license accepted"),
        ("windows product key", ("ret",), "product key confirmed"),
        ("windows 98 saved all information", ("ret",), "start wizard finished"),
        ("automatically adjust clock", ("ret",), "date and time confirmed"),
    )
    for title, keys, label in dialogs:
        if title not in visible:
            continue
        if title in handled:
            last, attempts = handled[title]
            if time.monotonic() - last < 20:
                return
            if keys != ("ret",) or attempts >= 6:
                raise RuntimeError(f"Windows Setup remained on the {title} page")
            print("setup: retrying " + label, flush=True)
            send_keys(monitor, *keys)
            handled[title] = (time.monotonic(), attempts + 1)
            return
        send_keys(monitor, *keys)
        handled[title] = (time.monotonic(), 1)
        print("setup: " + label, flush=True)
        return


def _run_setup(iso, disk, floppy, work, timeout, resume, live, bootstrap=False):
    serial = work / "serial.log"
    qemu_log = work / "qemu.log"
    monitor = live
    screen = work / "last-screen.ppm"
    started = time.monotonic()
    next_report = 60
    seen = (set(MILESTONES) - {"NEO98_SETUP_COMPLETE"}) if resume else set()
    next_screen_check = 10
    completed_at = None
    for boot in range(8):
        live.reset()
        handled_dialogs = {}
        if time.monotonic() - started >= timeout:
            raise RuntimeError(f"Windows Setup timed out after {timeout}s; see {serial}")
        # QEMU can stop delivering interrupts after a Win98 soft reboot.
        # -no-reboot exits at each guest reboot; a fresh process restores the
        # BIOS timer before Windows continues from the persistent disk.
        serial.unlink(missing_ok=True)
        monitor.unlink(missing_ok=True)
        boot_floppy = floppy if boot == 0 and not resume else None
        with qemu_log.open("ab") as log:
            process = subprocess.Popen(qemu_args(iso, disk, boot_floppy, serial, monitor),
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                while time.monotonic() - started < timeout:
                    if serial.exists():
                        events = serial.read_text(errors="replace").splitlines()
                        for event in events:
                            if event in MILESTONES and event not in seen:
                                print("setup: " + MILESTONES[event], flush=True)
                                seen.add(event)
                            if event.endswith("_FAIL") or event == "NEO98_SETUP_RETURNED":
                                raise RuntimeError(f"Guest stopped at {event}; see {qemu_log}")
                        if "NEO98_SETUP_COMPLETE" in seen and completed_at is None:
                            completed_at = time.monotonic()
                    if process.poll() is not None:
                        break
                    live.frame()
                    elapsed = time.monotonic() - started
                    if "NEO98_SETUP_START" in seen and elapsed >= next_screen_check:
                        handle_setup_dialog(monitor, screen, handled_dialogs)
                        next_screen_check = elapsed + 0.5
                    if elapsed >= next_report and "NEO98_SETUP_START" in seen:
                        allocated = disk.stat().st_blocks * SECTOR // (1024 * 1024)
                        print(f"setup: {int(elapsed // 60)} min elapsed, {allocated} MiB written to disk",
                              flush=True)
                        next_report += 60
                    if "NEO98_DOS_BOOT" not in seen and elapsed > 180:
                        raise RuntimeError(f"DOS did not boot; see {qemu_log}")
                    if "NEO98_SETUP_START" not in seen and elapsed > 600:
                        raise RuntimeError(f"Windows Setup did not start; see {serial}")
                    if completed_at and time.monotonic() - completed_at > 30:
                        # Let ExitWindows flush files, then stop if it has not
                        # shut down the virtual PC itself.
                        process.terminate()
                        break
                    time.sleep(0.5)
                else:
                    raise RuntimeError(f"Windows Setup timed out after {timeout}s; see {serial}")
            finally:
                interrupted = sys.exc_info()[0] is not None
                if interrupted:
                    live.ocr_processes.cancel()
                if process.poll() is None:
                    process.terminate()
                try:
                    try:
                        process.wait(timeout=2 if interrupted else 15)
                    except KeyboardInterrupt:
                        # Cancellation can also arrive during an ordinary exit.
                        live.ocr_processes.cancel()
                        try:
                            process.wait(timeout=2)
                        except subprocess.TimeoutExpired:
                            pass
                        raise
                except subprocess.TimeoutExpired:
                    pass
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait(timeout=1)
                    monitor.unlink(missing_ok=True)
        if "NEO98_SETUP_COMPLETE" in seen:
            break
        bootstrap_reboot = bootstrap and boot == 0 and "NEO98_SYS_OK" in seen
        if process.returncode != 0 or ("NEO98_SETUP_START" not in seen and not bootstrap_reboot):
            raise RuntimeError(f"QEMU stopped before Setup completed; see {qemu_log} and {serial}")
        print("setup: guest reboot; restarting QEMU", flush=True)
    else:
        raise RuntimeError("Windows Setup rebooted too many times")
    if "NEO98_SETUP_COMPLETE" not in seen:
        raise RuntimeError(f"Windows Setup did not reach the first desktop; see {qemu_log} and {serial}")


def validate_disk(disk):
    volume = f"{disk}@@{PARTITION_OFFSET}"
    for file in ("::/Neo98.DONE", "::/WINDOWS/SYSTEM.DAT", "::/WINDOWS/USER.DAT",
                 "::/WINDOWS/EXPLORER.EXE", "::/WINDOWS/SYSTEM/KERNEL32.DLL"):
        result = subprocess.run(("mdir", "-a", "-i", volume, file), stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL)
        if result.returncode:
            raise RuntimeError(f"Installation incomplete: missing {file} in {disk}")


def check_iso(iso):
    """Recognize a known image or validate another image's compatible content."""
    iso = Path(iso)
    size = iso.stat().st_size
    supported = win98_c.supported_isos()
    candidates = [entry for entry in supported if entry["size"] == size]
    digest = hashlib.sha256()
    if candidates:
        with iso.open("rb") as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b""):
                digest.update(chunk)
        for entry in candidates:
            if entry["sha256"] == digest.hexdigest():
                return dict(entry, validation="sha256")
    return win98_iso.content_record(iso, digest=digest.hexdigest() if candidates else None)


def check_source_iso(folder, iso):
    """Do not add another Windows variant's cabinets to an installed machine."""
    expected = win98_c.record(folder).get("iso", {})
    if expected.get("sha256") == iso["sha256"]:
        return

    def profile(record):
        if record.get("profile"):
            return record["profile"]
        return next((known.get("profile") for known in win98_c.supported_isos()
                     if known["sha256"] == record.get("sha256")), None)

    if profile(expected) and profile(expected) == profile(iso):
        return
    raise RuntimeError("This ISO does not match the existing Windows installation. "
                       "To add installation files, use the original ISO or "
                       "an image with the same verified content profile.")


def generate_assets(environment):
    """Everything the build derives from Win98 files, generated once from the folder."""
    python = str(ROOT / "build/analysis-venv/bin/python")
    run(python, str(ROOT / "tools/sync_win98_programs.py"), env=environment)
    if not (ROOT / "tools/import_win98.py").is_file():
        return  # Public hosts read resources from the installed tree at runtime.
    run(python, "-c", "import neo98; neo98.win98_assets(); neo98.registry_assets(); "
                "neo98.dibeng_assets(); neo98.text_assets(); neo98.user_assets(); "
                "neo98.frame_control_assets(); neo98.dialog_assets(); neo98.message_table_assets()",
        cwd=str(ROOT), env=environment)


# Copies of Win98 files the tools keep under build/ once they have taken them
# out of the installation. Setup clears them, so nothing of an earlier
# installation stays behind; each tool copies its file again from the new
# folder when it needs it.
CACHES = ("programs", "win98-original", "gdi-audit", "registry-import", "start-menu", "host-window",
          "nls", "crt-locale", "vmm-bitmap", "vmm-internals/VMM.VXD", "vmm-internals/VMM32.VXD",
          "time-family/IFSMGR.VXD", "win87em/WIN87EM.DLL", "dibeng/DIBENG.DLL",
          "nonclient-fidelity/fonts/MARLETT.TTF", "api-inventory/originals")


def clear_caches():
    # The records of the generated assets name the files they came from; without
    # them every generator runs again, out of the new folder. WINMINE is the
    # Windows 95 file the user imports separately.
    generated = BUILD / "generated"
    if generated.is_dir() and not generated.is_symlink():
        for record in generated.rglob("*provenance*.json"):
            if record.name != "winmine-provenance.json":
                record.unlink()
    for name in CACHES:
        path = BUILD / name
        # A worktree links some of these to another checkout; only the link
        # goes, and nothing behind a linked directory is touched.
        if path.is_symlink():
            path.unlink()
            continue
        if not path.resolve().is_relative_to(BUILD.resolve()):
            continue
        if path.is_file():
            path.unlink()
        elif path.is_dir():
            shutil.rmtree(path)


def foreign_folder(folder):
    """Whether `folder` holds something setup did not put there: it exists,
    is not empty and is not the unfinished copy of an earlier attempt."""
    folder = Path(folder)
    if not folder.exists():
        return False
    if not folder.is_dir():
        return True
    names = [name for name in os.listdir(folder) if name != ".DS_Store"]
    return bool(names) and win98_c.SIDECAR not in names


def import_folder(disk, iso, folder=None, user=False, iso_path=None):
    """Copy the installation into the Win98 folder once and finish it.

    An unfinished folder of an earlier attempt is replaced; a finished one is
    drive C: with the user's own files and is never touched here. `user`
    leaves out what only the development needs: the copies and generated
    headers under build/ and the snapshot for the tests.
    """
    import win98_folder
    folder = Path(folder or win98_c.FOLDER)
    if win98_c.ready(folder):
        raise RuntimeError(f"{folder} is already set up")
    if foreign_folder(folder):
        raise RuntimeError(f"{folder} is not empty; choose another install location")
    if folder.exists():
        shutil.rmtree(folder)
    folder.parent.mkdir(parents=True, exist_ok=True)
    journal = win98_c.journal(folder)
    journal.unlink(missing_ok=True)
    print(f"setup: copying the installation into {folder}", flush=True)
    report = win98_folder.copy_tree(disk, PARTITION_OFFSET, folder, quiet=True)
    if report["truncated"]:
        raise RuntimeError("Truncated files on the installed disk: " + ", ".join(report["truncated"][:10]))
    win98_folder.cleanup(folder, folder, journal)
    # The zone of the answer file, as TIMEDATE.CPL /Z would have set it.
    win98_time_zone.apply(folder, journal)
    # What the setup of the Glide driver writes.
    win98_glide_driver.apply(folder, journal)
    # The cabinets of the CD on C:, as on a preinstalled PC: Windows takes
    # drivers and components from there instead of asking for the CD.
    if iso_path:
        win98_cabs.apply(iso_path, folder, journal)
    if not user:
        clear_caches()
        # The children read the folder before the done-marker exists.
        environment = dict(os.environ, NEO98_WIN98_ROOT=str(folder.resolve()), NEO98_SETUP_RUNNING="1")
        print("setup: importing the registry and generating assets", flush=True)
        generate_assets(environment)
    print("setup: recording checksums", flush=True)
    count = win98_c.write_checksums(folder)
    record = {"iso": {"name": iso["name"], "size": iso["size"], "sha256": iso["sha256"]},
              "created": time.strftime("%Y-%m-%dT%H:%M:%S"),
              "files": report["files"], "directories": report["directories"], "bytes": report["bytes"],
              "checksums": count, "volume": {"label": report["label"], "serial": report["serial"]},
              "registry_journal": str(journal), "time_zone": win98_time_zone.ZONE,
              "glide_driver": win98_glide_driver.VERSION}
    for key in ("profile", "language", "os_version", "product_type", "media_type", "validation"):
        if key in iso:
            record["iso"][key] = iso[key]
    if iso_path:
        record["cabs"] = win98_cabs.VERSION
    (folder / win98_c.MARKER).write_text(json.dumps(record, indent=2) + "\n")
    if user:
        return folder
    # The state the tests start from; the folder itself becomes the user's
    # machine from the first start on.
    print(f"setup: snapshot for the tests {win98_c.snapshot_pristine(folder)}", flush=True)
    return folder


def complete(folder=None, iso=None):
    """Setup steps a folder of an earlier version of setup still lacks.

    Each step notes itself in the done-marker and runs once; what the guest
    changed since stays in the journal before the new records.
    """
    folder = Path(folder or win98_c.FOLDER)
    if iso:
        check_source_iso(folder, check_iso(Path(iso)))
    journal = win98_c.journal(folder)
    if win98_time_zone.apply(folder, journal):
        print(f"setup: time zone {win98_time_zone.ZONE!r} added ({journal})")
    if win98_glide_driver.apply(folder, journal):
        print(f"setup: Glide driver registered ({journal})")
    # The cabinets need the ISO; `neo98.py setup --iso ISO` on a folder that is
    # set up already brings them in.
    if iso:
        result = win98_cabs.apply(iso, folder, journal)
        if result:
            print(f"setup: Cabinet files copied to {win98_cabs.SOURCE} ({result[0]} files), "
                  f"SourcePath recorded ({journal})")
    elif json.loads((folder / win98_c.MARKER).read_text()).get("cabs") != win98_cabs.VERSION:
        print(f"setup: no --iso supplied; cabinet files missing from {win98_cabs.SOURCE}; Windows will request the CD")
    # Older setup folders get the snapshot of setup's state rebuilt
    # out of the installed disk image; the folder itself is not touched.
    print(f"setup: pristine test copy {win98_c.pristine(folder)}")


class KeyMissing(RuntimeError):
    """The installation has to run and no usable product key was given."""


def read_product_key(given=None):
    """The product key as five groups of five characters: `given` (the line the
    assistant of the app wrote to our standard input), else asked for on the
    terminal with hidden input. It never appears in an argument, a file other
    than the temporary answer file, a log or an error text."""
    if given is None:
        if not sys.stdin.isatty():
            raise KeyMissing("Run setup in a terminal to paste your Windows 98 product key")
        try:
            given = getpass.getpass("Windows 98 product key (input hidden): ")
        except (EOFError, KeyboardInterrupt) as error:
            raise KeyMissing("No Windows 98 product key supplied") from error
    compact_key = given.strip().upper().replace("-", "").replace(" ", "")
    if not compact_key:
        raise KeyMissing("No Windows 98 product key supplied")
    if not re.fullmatch(r"[A-Z0-9]{25}", compact_key):
        raise KeyMissing("Expected a Windows 98 product key with 25 letters and digits")
    return "-".join(compact_key[index:index + 5] for index in range(0, 25, 5))


def setup(iso=None, timeout=7200, reset=False, folder=None, work_root=None, product_key=None,
          user=False, keep_install=True, preview=None):
    """Install from an ISO or one ISO inside a ZIP/7z archive."""
    if iso is None:
        return _setup_iso(None, timeout, reset, folder, work_root, product_key, user, keep_install, preview)
    with win98_source.installation_iso(iso) as image:
        return _setup_iso(image, timeout, reset, folder, work_root, product_key, user, keep_install, preview)


def _setup_iso(iso=None, timeout=7200, reset=False, folder=None, work_root=None, product_key=None,
               user=False, keep_install=True, preview=None):
    """Install from `iso` and make `folder` drive C:.

    The defaults are those of `python3 neo98.py setup`: build/win98-c, the
    installed disk under build/setup-win98, the key from the terminal, and all
    the generated assets of the development. The assistant of the app
    (docs/BUILDING.md) gives its own folder and work directory, the key it read
    from its text field, and `user`, which stops after what a user needs:
    install, copy, cleanup, time zone, checksums, done-marker.
    """
    folder = Path(folder) if folder else win98_c.FOLDER
    work_root = Path(work_root) if work_root else BUILD / "setup-win98"
    work_root.mkdir(parents=True, exist_ok=True)
    if iso is None:
        if not win98_c.ready(folder):
            raise RuntimeError(f"Choose an ISO to set up {folder}")
        print(f"Windows 98 is already installed in {folder}")
        if not user:
            complete(folder)
        return 0
    iso = iso.expanduser().resolve()
    if not iso.is_file():
        raise RuntimeError(f"ISO not found: {iso}")
    if win98_c.ready(folder):
        print(f"Windows 98 is already installed in {folder}")
        if not user:
            complete(folder, iso)
        return 0
    if foreign_folder(folder):
        raise RuntimeError(f"{folder} is not empty; choose another install location")
    print("setup: checking the ISO", flush=True)
    pinned = check_iso(iso)
    iso_hash = pinned["sha256"]
    work = work_root / iso_hash[:16]
    disk = work / "disk.raw"
    state = work / "state.json"

    def finish():
        import_folder(disk, pinned, folder, user, iso_path=iso)
        if not keep_install:
            # The folder is drive C: now; the disk image is not read again.
            shutil.rmtree(work, ignore_errors=True)
        print(f"Windows 98 ready: drive C: is {folder}", flush=True)
        return 0

    if reset and state.is_file():
        raise RuntimeError(f"{work} contains a completed installation")
    if reset and work.exists():
        shutil.rmtree(work)
    if state.is_file() and disk.is_file():
        # The installation is done; only the copy into the folder is left.
        return finish()
    require_commands()
    bootstrap = False
    try:
        boot_image = inspect_iso(iso)
    except NoBootImage:
        boot_image = None
        bootstrap = True
        print("setup: preparing the installation disk", flush=True)
    else:
        check_boot_files(boot_image, work_root)
    if disk.is_file():
        try:
            validate_disk(disk)
        except RuntimeError:
            pass
        else:
            private_write(state, (json.dumps({"iso_sha256": iso_hash, "status": "complete"}, indent=2)
                                  + "\n").encode())
            return finish()
    if work.exists():
        if not user:
            raise RuntimeError(f"Incomplete setup at {work}; repeat with setup --reset to start fresh")
        # The assistant has no --reset; an unfinished attempt starts over.
        shutil.rmtree(work)
    product_key = read_product_key(product_key)
    work.mkdir(parents=True, mode=0o700)
    floppy = work / "setup-floppy.img"
    try:
        with tempfile.TemporaryDirectory(dir=work) as temp:
            scratch = Path(temp)
            prepare_disk(disk)
            stage_finish(disk, scratch)
            if bootstrap:
                import win98_boot
                win98_boot.prepare_bootstrap(iso, disk, floppy, answer_file(product_key), scratch,
                                             cache_dir=work_root / "boot-cache",
                                             partition_offset=PARTITION_OFFSET)
            else:
                prepare_floppy(floppy, boot_image, product_key, scratch)
        del product_key
        print("setup: starting headless Windows 98 installation", flush=True)
        run_setup(iso, disk, floppy, work, timeout, preview=preview, bootstrap=bootstrap)
        validate_disk(disk)
        private_write(state, (json.dumps({"iso_sha256": iso_hash, "status": "complete"}, indent=2)
                              + "\n").encode())
        return finish()
    finally:
        floppy.unlink(missing_ok=True)


# Where Homebrew puts QEMU, mtools, cabextract and tesseract. The assistant of
# the app starts us without the PATH of a login shell.
TOOL_DIRECTORIES = ("/opt/homebrew/bin", "/usr/local/bin")


def toolkit():
    """The setup toolkit a release carries (tools/setup_toolkit.py), or None."""
    path = os.environ.get("NEO98_TOOLKIT")
    return Path(path) if path and (Path(path) / "bin").is_dir() else None


def use_toolkit():
    """Put the bundled tools first, so a release needs nothing installed."""
    kit = toolkit()
    if kit is None:
        return
    os.environ["PATH"] = os.pathsep.join([str(kit / "bin"), os.environ.get("PATH", "")])
    if (kit / "share/tessdata").is_dir():
        os.environ["TESSDATA_PREFIX"] = str(kit / "share/tessdata")
TOOLS = ("qemu-system-i386", "mformat", "mcopy", "mdel", "mmd", "mdir", "cabextract", "tesseract")
BREW_LINE = "brew install qemu mtools cabextract tesseract"


def preserve_failure_logs(work):
    """Keep diagnostic logs outside the installation before removing its disk."""
    if sys.platform == 'darwin':
        root = Path.home() / 'Library/Logs/Neo98'
    else:
        root = Path(os.environ.get('XDG_STATE_HOME') or Path.home() / '.local/state') / 'neo98/logs'
    logs = [work / name for name in ('qemu.log', 'serial.log') if (work / name).is_file()]
    if not logs:
        return {}
    root.mkdir(parents=True, exist_ok=True, mode=0o700)
    destination = Path(tempfile.mkdtemp(prefix=time.strftime('setup-%Y%m%d-%H%M%S-'), dir=root))
    saved = {}
    for log in logs:
        target = destination / log.name
        shutil.copyfile(log, target)
        target.chmod(0o600)
        saved[log] = target
    return saved


def failure_message(error, logs):
    """Include the QEMU cause and point at the surviving diagnostic files."""
    message = str(error)
    details = []
    for original, saved in logs.items():
        message = message.replace(str(original), str(saved))
        details.append(str(saved))
    if details:
        message += '; logs: ' + ', '.join(details)
    tails = []
    for original, saved in logs.items():
        if original.name == 'qemu.log':
            try:
                with saved.open('rb') as stream:
                    stream.seek(0, 2)
                    stream.seek(max(0, stream.tell() - 65536))
                    tail = stream.read().decode('utf-8', errors='replace').splitlines()[-40:]
                tails.extend(tail)
            except OSError as read_error:
                tails.append(f'Could not read QEMU log: {read_error}')
    # The hosts show the final line in the failure label and keep all lines
    # in the setup log. Keep the paths and the last QEMU diagnostic there.
    if tails:
        return ('QEMU log (last 40 lines):\n' + '\n'.join(tails) +
                '\nsetup: ERROR: ' + message + '; QEMU: ' + tails[-1])
    return message


def main(argv=None):
    """The entry of the app's assistant: no terminal, the key on standard input.

        /usr/bin/python3 setup_win98.py --user --iso ISO --target FOLDER --work FOLDER --key-stdin

    Every step prints one `setup:...` line. Exit code 0 when drive C: is
    ready, 3 when the product key is missing or malformed (nothing was
    started), 1 for every other failure. SIGTERM (Cancel) stops QEMU and
    removes what the run created: the unfinished installation and the
    unfinished folder.
    """
    import argparse
    import signal
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--iso", type=Path)
    parser.add_argument("--preview", type=Path, help="temporary live guest PNG, removed when setup stops")
    parser.add_argument("--target", type=Path, help="the Win98 folder to create, default build/win98-c")
    parser.add_argument("--work", type=Path, help="where the installed disk image lives, default build/setup-win98")
    parser.add_argument("--key-stdin", action="store_true", help="read the product key from standard input")
    parser.add_argument("--user", action="store_true",
                        help="only what a user needs; no generated assets, no snapshot for the tests")
    parser.add_argument("--keep-install", action="store_true",
                        help="with --user: keep the installed disk image after the copy")
    parser.add_argument("--timeout", type=int, default=7200)
    parser.add_argument("--reset", action="store_true")
    parser.add_argument("--check-tools", action="store_true", help="print the missing tools and stop")
    args = parser.parse_args(argv)
    if args.user:
        os.environ["PATH"] = os.pathsep.join([*TOOL_DIRECTORIES, "/usr/bin", "/bin", "/usr/sbin", "/sbin"])
    use_toolkit()
    if args.check_tools:
        missing = [name for name in TOOLS if not shutil.which(name)]
        print("missing: " + " ".join(missing) if missing else "tools: ok")
        return 1 if missing else 0

    def stop(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, stop)
    key = None
    if args.key_stdin:
        key = sys.stdin.readline()
    folder = Path(args.target) if args.target else win98_c.FOLDER
    work_root = Path(args.work) if args.work else BUILD / "setup-win98"
    was_ready = win98_c.ready(folder)
    try:
        return setup(args.iso, args.timeout, args.reset, folder, work_root, key, args.user,
                     keep_install=args.keep_install or not args.user, preview=args.preview)
    except KeyMissing as error:
        print(f"setup: ERROR: {error}", flush=True)
        return 3
    except (KeyboardInterrupt, RuntimeError, subprocess.CalledProcessError, OSError) as error:
        cancelled = isinstance(error, KeyboardInterrupt)
        logs = {}
        log_errors = []
        if args.user and not was_ready and not win98_c.ready(folder):
            # Nothing half done stays behind: the copy and its journal, and
            # an installation that did not reach its end.
            if folder.is_dir() and not foreign_folder(folder):
                shutil.rmtree(folder, ignore_errors=True)
                win98_c.journal(folder).unlink(missing_ok=True)
            for work in work_root.glob("*/"):
                if work.is_dir() and not (work / "state.json").is_file():
                    try:
                        logs.update(preserve_failure_logs(work))
                    except OSError as log_error:
                        # Keep the original logs if copying them is impossible.
                        logs.update({work / name: work / name for name in ('qemu.log', 'serial.log')
                                     if (work / name).is_file()})
                        log_errors.append(f'Logs kept at {work}: {log_error}')
                        continue
                    shutil.rmtree(work, ignore_errors=True)
        message = failure_message(error, logs)
        if log_errors:
            message += '; ' + '; '.join(log_errors)
        print("setup: cancelled" if cancelled else f"setup: ERROR: {message}", flush=True)
        return 2 if cancelled else 1


if __name__ == "__main__":
    sys.exit(main())
