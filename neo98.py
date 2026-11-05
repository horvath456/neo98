#!/usr/bin/env python3
"""NEO98 on macOS ARM64 and Linux x64: set up, build and run the host.

`python3 neo98.py` (or `run`) starts Explorer in the host window,
`build` builds that host. The Circle/AArch64 kernel under QEMU is deferred and
lives on as pi-build, pi-run, pi-test and pi-test-desktop.
"""
from __future__ import annotations

import argparse
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
import venv

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
# Imported originals and generated assets are shared; kernel images, logs and
# test results go here, so a second checkout can keep its own with
# NEO98_OUTPUT_DIR without re-importing anything.
OUT = Path(os.environ.get("NEO98_OUTPUT_DIR") or BUILD)
CIRCLE = ROOT / "third_party" / "circle"
REVISION = (ROOT / "circle-revision.txt").read_text().strip()

sys.path.append(str(ROOT / "tools"))
import provenance as asset_provenance    # noqa: E402
import win98_c    # noqa: E402


def generated_assets(name, regenerate, message, headers=()):
    """Read build/generated/<name>, regenerating the asset when it is stale.

    Stale means a generator script or one of its local inputs differs from what
    the record was stamped with; tools/provenance.py holds that table. A header
    whose hash differs while the generators are unchanged is a hand edit and
    stays an error, because no regeneration would explain it.
    """
    scripts, _ = asset_provenance.SOURCES[name]
    missing = [script for script in scripts if not (ROOT / script).is_file()]
    if missing:
        raise RuntimeError("This operation needs internal comparison tools unavailable in the public release: "
                           + ", ".join(missing))
    provenance = BUILD / "generated" / name
    info = json.loads(provenance.read_text()) if provenance.exists() else None
    if (info is None or any(not (provenance.parent / header).exists() for header in headers)
            or asset_provenance.changed(info, name)):
        regenerate()
        info = json.loads(provenance.read_text())
    for asset, expected in info.get("generated_sha256", {}).items():
        if hashlib.sha256((provenance.parent / asset).read_bytes()).hexdigest() != expected:
            raise RuntimeError(message.format(asset=asset))
    return info


def analysis_python():
    environment = BUILD / "analysis-venv"
    python = environment / "bin/python"
    probe = subprocess.run([str(python), "-c", "import pefile, unicorn, capstone"],
                           capture_output=True) if python.exists() else None
    if not probe or probe.returncode:
        venv.EnvBuilder(with_pip=True).create(environment)
        subprocess.run([str(python), "-m", "pip", "install", "--no-cache-dir",
                        "-r", str(ROOT / "tools/requirements.txt")], check=True)
    return python


def win98_assets():
    """The USER32 oracle and the system font out of the Win98 folder of setup."""
    def regenerate():
        if not win98_c.ready():
            raise RuntimeError(win98_c.NOT_READY.format(folder=win98_c.FOLDER))
        subprocess.run([str(analysis_python()), str(ROOT / "tools/import_win98.py")], check=True)
    return generated_assets("provenance.json", regenerate,
                            "Modified generated asset {asset} with an unchanged importer; "
                            "repeat python3 tools/import_win98.py")


def registry_assets():
    """Decode the installed Win98 hives into the generated native registry image."""
    def regenerate():
        win98_c.require()
        subprocess.run([sys.executable, str(ROOT / "tools/import_registry.py")], check=True)
    return generated_assets("registry-provenance.json", regenerate,
                            "Modified generated registry asset {asset} with an unchanged importer; "
                            "restore it or repeat the registry import")


def import_winmine(exe):
    subprocess.run([str(analysis_python()), str(ROOT / "tools/import_winmine.py"), str(exe)], check=True)


def message_table_assets():
    """Copy KERNEL32's original message table into the generated header."""
    def regenerate():
        win98_assets()
        subprocess.run([str(analysis_python()), str(ROOT / "tools/message_table_assets.py")], check=True)
    return generated_assets("message-table-provenance.json", regenerate,
                            "Modified generated asset {asset} with an unchanged generator; "
                            "restore it or repeat tools/message_table_assets.py",
                            headers=("win98_message_table.h",))


def winmine_assets():
    def regenerate():
        default = BUILD / "winmine/WINMINE.EXE"
        exe = Path(os.environ.get("NEO98_WINMINE_EXE", str(default)))
        # A regeneration triggered by a changed importer must not need the
        # original source again: the first import kept a copy of the file.
        if not exe.exists() and (BUILD / "winmine/WINMINE.EXE").exists():
            exe = BUILD / "winmine/WINMINE.EXE"
        if not exe.exists():
            raise RuntimeError("Import WINMINE 4.0.0.950 first: python3 neo98/neo98.py "
                               "import-winmine --winmine-exe /path/to/winmine.exe")
        import_winmine(exe)
    return generated_assets("winmine-provenance.json", regenerate,
                            "Modified generated asset {asset} with an unchanged importer; "
                            "repeat import-winmine")


def import_dibeng(dll):
    subprocess.run([str(analysis_python()), str(ROOT / "tools/import_dibeng.py"), str(dll)], check=True)


def dibeng_assets():
    def regenerate():
        local = BUILD / "dibeng/DIBENG.DLL"
        if not local.exists():
            win98_c.copy("::/WINDOWS/SYSTEM/DIBENG.DLL", local)
        import_dibeng(local)
    return generated_assets("dibeng-provenance.json", regenerate,
                            "Modified generated asset {asset} with an unchanged importer; "
                            "repeat import-dibeng")


def test_gdi_host():
    binary = BUILD / "dibeng/test-gdi"
    subprocess.run(["clang++", "-std=c++17", "-g", "-O1", "-fsanitize=address,undefined",
                    str(ROOT / "tools/test_gdi.cpp"), str(ROOT / "src/gdi.cpp"),
                    str(ROOT / "src/dibeng.cpp"), str(ROOT / "src/dib_text.cpp"), str(ROOT / "src/win98_rect.cpp"),
                    "-o", str(binary)], check=True, env=tool_environment())
    subprocess.run([str(binary)], check=True)
    bitmap = BUILD / 'dibeng/test-native-bitmap'
    subprocess.run(['clang++', '-std=c++17', '-g', '-O1', '-fsanitize=address,undefined',
                    str(ROOT/'tools/test_native_bitmap.cpp'),
                    *[str(ROOT/'src'/f'{name}.cpp') for name in ('gdi','dibeng','dib_text','win98_rect')],
                    '-o', str(bitmap)], check=True, env=tool_environment())
    subprocess.run([str(bitmap)], check=True)
    edges = BUILD / 'dibeng/test-bitmap-provider-edges'
    subprocess.run(['clang++', '-std=c++17', '-g', '-O1', '-fsanitize=address,undefined',
                    str(ROOT/'tools/test_bitmap_provider_edges.cpp'),
                    *[str(ROOT/'src'/f'{name}.cpp') for name in ('dibeng','dib_text','win98_rect')],
                    '-o', str(edges)], check=True, env=tool_environment())
    subprocess.run([str(edges)], check=True)
    editor = BUILD / "dibeng/test-text-edit"
    subprocess.run(["clang++", "-std=c++17", "-g", "-O1", "-fsanitize=address,undefined",
                    str(ROOT / "tools/test_text_edit.cpp"), str(ROOT / "src/text_edit.cpp"),
                    "-o", str(editor)], check=True, env=tool_environment())
    subprocess.run([str(editor)], check=True)


def text_assets():
    from tools.extract_font_charset import generate as font_charset_assets
    font_charset_assets()
    from tools.extract_font_charset_info import generate as font_charset_info_assets
    font_charset_info_assets()
    def regenerate():
        win98_assets(); dibeng_assets()
        subprocess.run([str(analysis_python()), str(ROOT / "tools/import_text.py"),
                        str(BUILD / "dibeng/DIBENG.DLL"), str(BUILD / "win98-original/SSERIFE.FON")], check=True)
    return generated_assets("text-provenance.json", regenerate,
                            "Modified generated text asset {asset} with an unchanged importer; "
                            "repeat import-text")


def user_assets(refresh=False):
    marlett_assets()
    user_resource_assets()
    def regenerate():
        text_assets()
        python=analysis_python()
        subprocess.run([str(python),str(ROOT/'tools/gui_inventory.py')],check=True)
        output=BUILD/'user';output.mkdir(exist_ok=True)
        subprocess.run(['clang++','-std=c++17','-O2','-DUSER_DRAW_NO_VECTORS',*shared_library_flags(),
            str(ROOT/'tools/user_draw_bridge.cpp'),*[str(ROOT/'src'/f'{name}.cpp') for name in ('user_draw','gdi','dib_text','dibeng','win98_rect')],
            '-o',str(output/'draw.dylib')],check=True,env=tool_environment())
        subprocess.run([str(python),str(ROOT/'tools/test_user_draw.py')],check=True)
    if refresh:
        regenerate()
    return generated_assets('user-provenance.json', regenerate,
                            'Modified USER reference data {asset} with an unchanged generator; '
                            'restore it or repeat tools/test_user_draw.py')


def marlett_assets():
    """Gridfit the Marlett cells DrawFrameControl selects from the local font."""
    def regenerate():
        font = BUILD / "nonclient-fidelity/fonts/MARLETT.TTF"
        font.parent.mkdir(parents=True, exist_ok=True)
        if not font.exists():
            win98_c.copy("::/WINDOWS/FONTS/MARLETT.TTF", font)
        subprocess.run([str(analysis_python()), str(ROOT / "tools/marlett_glyphs.py")], check=True)
    generated_assets("marlett-provenance.json", regenerate,
                     "Modified Marlett asset {asset} with an unchanged rasterizer; "
                     "restore it or repeat tools/marlett_glyphs.py",
                     headers=("marlett_glyphs.h",))
    return BUILD / "generated/marlett_glyphs.h"


def user_resource_assets():
    """Import the USER.EXE resources and tables the native frame draws from."""
    from tools.gdi_data_fixtures import generate as gdi_comparison_fixtures
    gdi_comparison_fixtures(win98_c.FOLDER, BUILD / "generated")
    def regenerate():
        if not (BUILD / "gdi-audit/USER.EXE").exists():
            target = BUILD / "gdi-audit/USER.EXE"
            target.parent.mkdir(parents=True, exist_ok=True)
            win98_c.copy("::/WINDOWS/SYSTEM/USER.EXE", target)
        subprocess.run([str(analysis_python()), str(ROOT / "tools/import_user_resources.py")],
                       check=True)
    return generated_assets("user-resources-provenance.json", regenerate,
                            "Modified USER resource asset {asset} with an unchanged importer; "
                            "restore it or repeat tools/import_user_resources.py",
                            headers=("user_resources.h",))


def frame_control_assets():
    """Compare the implemented DrawFrameControl body against the original."""
    def regenerate():
        user_assets()
        output = BUILD / "nonclient-fidelity"
        output.mkdir(parents=True, exist_ok=True)
        subprocess.run(["clang++", "-std=c++17", "-O2", "-DUSER_DRAW_NO_VECTORS", *shared_library_flags(),
                        str(ROOT / "tools/frame_control_bridge.cpp"),
                        *[str(ROOT / "src" / f"{name}.cpp")
                          for name in ("user_draw", "gdi", "dib_text", "dibeng", "win98_rect")],
                        "-o", str(output / "frame-control.dylib")], check=True, env=tool_environment())
        subprocess.run([str(analysis_python()), str(ROOT / "tools/test_frame_control.py")], check=True)
    return generated_assets("frame-control-provenance.json", regenerate,
                            "Modified frame control reference data {asset} with an unchanged "
                            "generator; restore it or repeat tools/test_frame_control.py")


def dialog_assets():
    def regenerate():
        user_assets()
        subprocess.run([str(analysis_python()),str(ROOT/'tools/import_dialog.py')],check=True)
    return generated_assets('dialog-provenance.json', regenerate,
                            'Modified dialog reference data {asset} with an unchanged generator; '
                            'restore it or repeat tools/import_dialog.py')


def test_user_drawing_host():
    user_assets()
    binary = BUILD / 'user/test-user-draw-gdi'
    subprocess.run(['clang++', '-std=c++17', '-O1', '-g', '-fsanitize=address,undefined',
        str(ROOT / 'tools/test_user_draw_gdi.cpp'),
        *[str(ROOT / 'src' / f'{name}.cpp') for name in ('user_draw', 'gdi', 'dib_text', 'dibeng', 'win98_rect')],
        '-o', str(binary)], check=True, env=tool_environment())
    subprocess.run([str(binary)], check=True)


# The DLLs a fixture of our own implementations keeps on the native route.
# The same list stands in tools/fixture_native_modules.h for the C++ runners;
# These five libraries use the original route,
# so a fixture linked against our implementations has to ask for them back.
FIXTURE_NATIVE_MODULES = ("MSVCRT.DLL", "SHELL32.DLL", "SHLWAPI.DLL",
                          "COMCTL32.DLL", "COMDLG32.DLL")


def native_module_arguments():
    """The --native-module options a probe runner needs for such a fixture."""
    return [word for dll in FIXTURE_NATIVE_MODULES
            for word in ("--native-module", dll)]


def output_directory(name):
    """A build directory that belongs to this working tree.

    build/ in a worktree carries symlinks into the main build so that the
    imported originals and the generated assets are shared. Test output must
    not be shared: two working trees that check at the same time overwrite
    each other's fixtures and each other's results. A link
    that leaves this tree is therefore replaced by a real directory.
    """
    directory = OUT / name
    if directory.is_symlink():
        target = directory.resolve()
        if ROOT not in target.parents and target != ROOT:
            directory.unlink()
    directory.mkdir(parents=True, exist_ok=True)
    return directory


def shared_library_flags():
    return ['-dynamiclib'] if sys.platform == 'darwin' else ['-shared', '-fPIC']


def tool_environment():
    env = os.environ.copy()
    # Tests and tools play no sound; the user hears only `neo98.py run`.
    env.setdefault("SDL_AUDIODRIVER", "dummy")
    candidates = []
    if env.get("NEO98_LLVM_BIN"):
        candidates.append(env["NEO98_LLVM_BIN"])
    local_llvm = BUILD / 'toolchain/usr/lib/llvm-18/bin'
    if local_llvm.is_dir():
        candidates.append(str(local_llvm))
    candidates += ["/opt/homebrew/opt/llvm/bin", "/opt/homebrew/opt/lld/bin",
                   "/usr/local/opt/llvm/bin", "/usr/local/opt/lld/bin"]
    env["PATH"] = os.pathsep.join(candidates + [env.get("PATH", "")])
    for tool in ["git", "make", "clang", "clang++", "ld.lld", "llvm-ar",
                 "llvm-objcopy", "llvm-objdump", "llvm-cxxfilt"]:
        if not shutil.which(tool, path=env["PATH"]):
            raise RuntimeError(f"Missing {tool}. On Linux install clang, llvm and lld, or set NEO98_LLVM_BIN. On macOS: brew install llvm lld")
    return env


def build(app="notepad", out=None):
    from tools.app_programs import application
    application(app)  # Reject an unknown name before any asset work.
    out = out or OUT
    env = tool_environment()
    BUILD.mkdir(exist_ok=True)
    out.mkdir(parents=True, exist_ok=True)
    original = win98_assets()
    mines = winmine_assets()
    dibeng = dibeng_assets()
    text = text_assets()
    user = user_assets()
    dialog = dialog_assets()
    frame_control = frame_control_assets()
    registry = registry_assets()
    message_table = message_table_assets()
    # The signature table src/arm64_cpu.cpp includes (docs/ARM64-APPS.md).
    from tools import arm64_sdk
    arm64_sdk.generate_table()
    from tools.unicorn_assets import generate
    generate(app)
    if not CIRCLE.exists():
        CIRCLE.parent.mkdir(exist_ok=True)
        subprocess.run(["git", "init", str(CIRCLE)], check=True, env=env)
        subprocess.run(["git", "-C", str(CIRCLE), "remote", "add", "origin",
                        "https://github.com/rsta2/circle.git"], check=True, env=env)
        subprocess.run(["git", "-C", str(CIRCLE), "fetch", "--depth", "1", "origin",
                        REVISION], check=True, env=env)
        subprocess.run(["git", "-C", str(CIRCLE), "checkout", "--detach", "FETCH_HEAD"],
                       check=True, env=env)
    actual = subprocess.check_output(["git", "-C", str(CIRCLE), "rev-parse", "HEAD"],
                                     text=True, env=env).strip()
    if actual != REVISION:
        raise RuntimeError(f"Circle revision differs from pin: {actual}")
    for name in ('jit-pages.patch', 'newlib-conversions.patch'):
        patch = ROOT / 'platform/circle' / name
        applied = subprocess.run(['git', '-C', str(CIRCLE), 'apply', '--reverse', '--check', str(patch)], capture_output=True)
        if applied.returncode:
            subprocess.run(['git', '-C', str(CIRCLE), 'apply', '--check', str(patch)], check=True)
            subprocess.run(['git', '-C', str(CIRCLE), 'apply', str(patch)], check=True)
    subprocess.run([sys.executable, str(ROOT / 'tools/build_unicorn.py')], check=True, env=env)
    kernel_max_size = 64 * 1024 * 1024
    config = ("AARCH = 64\nRASPPI = 3\nCLANG = 1\nSTDLIB_SUPPORT = 0\n"
              "DEFINE += -DNO_SDHOST -DNO_SCREEN_DMA_BURST_LENGTH -DDEPTH=32\n"
              # Unicorn's newlib also supplies these public C conversions.
              "DEFINE += -DNEO98_NEWLIB_CONVERSIONS\n"
              # Reserve image and BSS below Circle's stack and heap. The
              # runtime's static tables plus embedded DLLs exceed 32 MiB.
              f"DEFINE += -DNEO98_UNICORN -DKERNEL_MAX_SIZE=0x{kernel_max_size:x}\n"
              "LDFLAGS += -fuse-ld=lld -Wl,--gc-sections\n")
    config_path = CIRCLE / "Config.mk"
    changed = not config_path.exists() or config_path.read_text() != config
    if changed:
        config_path.write_text(config)
    directories = [CIRCLE / "lib", CIRCLE / "lib/input", CIRCLE / "lib/fs",
                   CIRCLE / "lib/usb", ROOT / "src"]
    # The asset header moves with the application, so its timestamp cannot tell
    # make that the selection changed. Drop the one object that embeds it.
    marker = ROOT / "src/.application"
    if not marker.exists() or marker.read_text().strip() != app:
        for name in ["app_ui.o", "app_ui.d"]:
            (ROOT / "src" / name).unlink(missing_ok=True)
        marker.write_text(app + "\n")
    assets = f"../build/generated/app-{app}"
    with (out / "build.log").open("w") as log:
        for directory in directories:
            if changed:
                subprocess.run(["make", "clean"], cwd=directory, env=env,
                               stdout=log, stderr=subprocess.STDOUT, check=True)
            print(f"Building {directory.relative_to(ROOT)}", flush=True)
            log.flush()
            start = log.tell()
            result = subprocess.run(["make", "-j", str(min(os.cpu_count() or 2, 8)),
                                     *([f"NEO98_APP_ASSETS={assets}"] if directory == ROOT / "src" else [])],
                                    cwd=directory, env=env, stdout=log,
                                    stderr=subprocess.STDOUT)
            if result.returncode:
                log.flush()
                with (out / "build.log").open("rb") as text:
                    text.seek(start)
                    lines = text.read().decode(errors="replace").splitlines()
                # Everything goes to stderr, next to the NEO98: line, so a
                # failure is never reduced to one line without its cause.
                print("\n".join(lines[-65:]), file=sys.stderr)
                raise RuntimeError(f"make in {directory.relative_to(ROOT)} exited with status "
                                   f"{result.returncode}" + ("" if any(line.strip() for line in lines)
                                                             else " without a message") +
                                   f"; see {out / 'build.log'}")
    for suffix in ["img", "elf", "map", "lst"]:
        shutil.copy2(ROOT / "src" / f"kernel8.{suffix}", out / f"kernel8.{suffix}")
    elf = (out / "kernel8.elf").read_bytes()
    if elf[:6] != b"\x7fELF\x02\x01" or struct.unpack_from("<H", elf, 18)[0] != 183:
        raise RuntimeError("Build did not produce a little-endian AArch64 ELF64 executable")
    symbols = subprocess.check_output(["llvm-nm", "-n", str(out / "kernel8.elf")], env=env, text=True)
    kernel_end = re.search(r"^([0-9a-fA-F]+)\s+\w\s+_end$", symbols, re.M)
    # Circle's raspi3 AArch64 image starts at 80000h. Its stack begins after
    # KERNEL_MAX_SIZE; BSS must fit too, although it is absent from kernel8.img.
    if not kernel_end or int(kernel_end[1], 16) > 0x80000 + kernel_max_size:
        raise RuntimeError("Kernel image and BSS overlap Circle's stack; increase KERNEL_MAX_SIZE")
    manifest = {"circle_revision": REVISION, "architecture": "AArch64 ELF64",
                "machine": "raspi3b", "application": app,
                "application_assets": json.loads((BUILD / "generated" / f"app-{app}/provenance.json").read_text()),
                "image_bytes": (out / "kernel8.img").stat().st_size,
                "image_sha256": hashlib.sha256((out / "kernel8.img").read_bytes()).hexdigest(),
                "win98_user32": original["user32"], "win98_font": original["font"],
                "winmine": mines,
                "dibeng": dibeng, "dibeng_text": text, "user_drawing": user,"dialog":dialog,
                "frame_control": frame_control,
                "registry": {"hives": registry["hives"], "imported": registry["imported"]},
                "cpu": json.loads((BUILD / 'unicorn-circle/manifest.json').read_text()),
                "compiler": subprocess.check_output(["clang", "--version"], env=env,
                                                     text=True).splitlines()[0]}
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"Built AArch64 kernel: {manifest['image_bytes']:,} bytes")


def qemu_args(display, serial, qmp=None, app="lab", image=None):
    from tools.app_programs import application
    keymap = os.environ.get("NEO98_KEYMAP", "DE").upper()
    if keymap not in ("DE", "US"):
        raise RuntimeError("NEO98_KEYMAP must be DE or US")
    args = ["qemu-system-aarch64", "-M", "raspi3b", "-accel", "tcg", "-m", "1G",
            "-kernel", str(image or OUT / "kernel8.img"), "-display", display,
            "-append", f"keymap={keymap} app={app}",
            "-global", "bcm2835-fb.xres=800", "-global", "bcm2835-fb.yres=600",
            "-device", "usb-kbd", "-device", "usb-mouse", "-serial", serial, "-monitor", "none",
            "-name", "Neo98 - Win98 " + application(app)["window"] + " on ARM64"]
    if qmp:
        args += ["-qmp", f"unix:{qmp},server=on,wait=off"]
    return args


class QMP:
    def __init__(self, path, process):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.socket.settimeout(5)
        deadline = time.monotonic() + 10
        while True:
            try:
                self.socket.connect(str(path))
                break
            except (FileNotFoundError, ConnectionRefusedError):
                if process.poll() is not None or time.monotonic() > deadline:
                    self.socket.close()
                    raise RuntimeError("QEMU did not start its control socket")
                time.sleep(0.05)
        self.stream = self.socket.makefile("rwb", buffering=0)
        if "QMP" not in json.loads(self.stream.readline()):
            raise RuntimeError("Invalid QMP greeting")
        self.command("qmp_capabilities")

    def command(self, name, arguments=None):
        self.stream.write(json.dumps({"execute": name, "arguments": arguments or {}}).encode() + b"\n")
        while True:
            line = self.stream.readline()
            if not line:
                raise RuntimeError("QMP disconnected")
            response = json.loads(line)
            if "error" in response:
                raise RuntimeError(f"QMP {name}: {response['error']}")
            if "return" in response:
                return response["return"]

    def close(self):
        self.stream.close()
        self.socket.close()


def wait_for(path, text, process, timeout=25):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        data = path.read_text(errors="replace") if path.exists() else ""
        if text in data:
            return data
        if "NEO98 FAIL" in data or process.poll() is not None:
            raise RuntimeError(f"Guest failed before {text!r}:\n{data[-3000:]}")
        time.sleep(0.05)
    raise RuntimeError(f"Timed out waiting for {text!r}:\n{data[-3000:]}")


def screenshot(qmp, name, directory=None):
    directory = directory or OUT
    path = directory / f"{name}.ppm"
    qmp.command("screendump", {"filename": str(path)})
    with path.open("rb") as f:
        if f.readline().strip() != b"P6":
            raise RuntimeError("Unexpected screenshot format")
        dimensions = f.readline()
        while dimensions.startswith(b"#"):
            dimensions = f.readline()
        width, height = map(int, dimensions.split())
        if f.readline().strip() != b"255" or (width, height) != (800, 600):
            raise RuntimeError("Unexpected framebuffer dimensions/depth")
        pixels = f.read()
    if len(pixels) != width * height * 3:
        raise RuntimeError("Incomplete framebuffer screenshot")
    # Keep a PNG for convenient viewing. QEMU produces it from the same display.
    qmp.command("screendump", {"filename": str(directory / f"{name}.png"), "format": "png"})
    return pixels


def test():
    from tools.native_app import test as test_native_app
    from tools.test_app_ui import test_app_ui
    from tools.native_components import test as test_native_components
    component_report = test_native_components()
    native_host_report = test_native_app()
    native_report = test_app_ui(sys.modules[__name__])
    from tools.test_calculator_ui import test_calculator_ui
    calculator_report=test_calculator_ui(sys.modules[__name__])
    from tools.test_regedit_ui import test_regedit_ui
    regedit_report=test_regedit_ui(sys.modules[__name__])
    from tools.test_taskman_ui import test_taskman_ui
    taskman_report=test_taskman_ui(sys.modules[__name__])
    from tools.test_notepad_file_ui import test_notepad_file_ui
    notepad_file_report=test_notepad_file_ui(sys.modules[__name__])
    from tools.test_notepad_find_ui import test_notepad_find_ui
    notepad_find_report=test_notepad_find_ui(sys.modules[__name__])
    # WordPad needs its own kernel image. It is built into build/wordpad-kernel,
    # so the default image the desktop regression boots stays in place.
    from tools.test_wordpad_ui import test_wordpad_ui
    wordpad_report = test_wordpad_ui(sys.modules[__name__])
    # Explorer likewise carries its own original shell DLLs, so it gets its own
    # image in build/explorer-kernel.
    from tools.test_explorer_ui import test_explorer_ui
    explorer_report = test_explorer_ui(sys.modules[__name__])
    pe_test = BUILD / 'unicorn-circle/test-pe32'
    subprocess.run(['clang++', '-std=c++17', '-g', '-O1', '-fsanitize=address,undefined',
                    str(ROOT / 'tools/test_pe32.cpp'), str(ROOT / 'src/pe32.cpp'), str(ROOT / 'src/win98_exports.cpp'), '-o', str(pe_test)],
                   check=True, env=tool_environment())
    subprocess.run([str(pe_test)], check=True)
    test_desktop({"native_notepad": native_report,
                  "native_notepad_host": native_host_report,
                  "native_components": component_report,
                  "native_calculator": calculator_report,
                  "native_regedit": regedit_report,
                  "native_taskman_startup": taskman_report,
                  "native_notepad_files": notepad_file_report,
                  "native_notepad_find": notepad_find_report,
                  "native_wordpad": wordpad_report,
                  "native_explorer": explorer_report})


def test_desktop(application_reports=None):
    """Run the graphics/input regression on the current kernel image.

    A targeted run gets its own report and never marks the full suite passed.
    """
    full_suite = application_reports is not None
    test_gdi_host()
    test_user_drawing_host()
    if not full_suite:
        # A targeted run covers the Explorer image too. It boots first, on its
        # own, so only one QEMU at a time competes for the host CPU. An image
        # from an earlier build is reused; test builds a fresh one.
        from tools.test_explorer_ui import test_explorer_ui
        existing = BUILD / "explorer-kernel/kernel8.img"
        application_reports = {"native_explorer": test_explorer_ui(
            sys.modules[__name__], existing if existing.is_file() else None)}
    serial_log = OUT / "test-serial.log"
    serial_log.write_text("")
    report_path = BUILD / ("test-results.json" if full_suite else "desktop-test-results.json")
    report_path.unlink(missing_ok=True)
    checks = []
    with tempfile.TemporaryDirectory(prefix="neo98-") as temp, (OUT / "test-qemu.log").open("w") as log:
        process = subprocess.Popen(qemu_args("none", f"file:{serial_log}", Path(temp) / "qmp"),
                                   stdout=log, stderr=subprocess.STDOUT, env=tool_environment())
        qmp = None
        try:
            qmp = QMP(Path(temp) / "qmp", process)
            # Boot includes the original-code and process self-tests. Match the
            # application runner's allowance for QEMU under host CPU contention.
            wait_for(serial_log, "NEO98 READY", process, timeout=240)
            wait_for(serial_log, "NEO98 SELFTEST raster-clipping=PASS", process)
            checks += ["AArch64 boot", "guest raster clipping and guard words"]
            wait_for(serial_log, "NEO98 SELFTEST unicorn=PASS host=aarch64 guest=x86 backend=tcg", process)
            checks.append("Unicorn x86-to-AArch64 JIT in the kernel: loops, code invalidation, memory permissions, API hook, lifecycle")
            wait_for(serial_log, "NEO98 SELFTEST unicorn-original-user32=PASS functions=9 vectors=1908 pe_loader=native", process)
            checks.append("native PE loader executes nine original USER32.DLL exports through the in-kernel JIT across 1908 cases")
            wait_for(serial_log, "NEO98 SELFTEST original-user32=PASS functions=9", process)
            checks.append("nine native USER32 routines match original x86 oracle vectors in ARM guest")
            wait_for(serial_log, "NEO98 SELFTEST original-user-draw=PASS", process)
            wait_for(serial_log, "NEO98 SELFTEST original-dialog=PASS vectors=2862", process)
            wait_for(serial_log, "NEO98 SELFTEST native-threads=PASS threads=3", process)
            wait_for(serial_log, "NEO98 SELFTEST native-common-controls=PASS", process)
            wait_for(serial_log, "windows=PASS", process)
            wait_for(serial_log, "global-atoms=PASS window-properties=PASS nls-composite=PASS nls-decompose=PASS", process)
            checks.append("original global and registration atom tables, USER property arrays and both SBCS composition directions through x86 imports on ARM")
            wait_for(serial_log, "local-heap-atoms=PASS", process)
            checks.append("original KRNL386 atom table and internal local heap run together as native ARM code, including allocation, exhaustion and reuse")
            wait_for(serial_log, "virtual-memory=PASS", process)
            wait_for(serial_log, "NEO98 SELFTEST native-page-protection=PASS x86-write=PASS api-write=PASS decommit=PASS noaccess-execution=PASS", process)
            checks.append("native virtual memory: reserve/commit/decommit/release and query; x86 and native API writes obey page protection; decommitted reads and noaccess execution fail")
            wait_for(serial_log, "NEO98 SELFTEST native-modules=PASS", process)
            checks.append("native DLL loader: relocated dependencies, DllMain, exports, loader lock, thread notifications and 20 reloads")
            checks.append("original Win98 USER edge and button-frame code: pixels, geometry and draw-call order match on ARM")
            wait_for(serial_log, "NEO98 INPUT usb-keyboard=ready", process)
            checks.append("USB keyboard enumeration")
            from tools.test_editor_ui import test_editor_ui
            editor_report = test_editor_ui(qmp, serial_log, process, BUILD, wait_for, screenshot)
            checks.append("original DIB text on ARM: fonts, reflow, editing, selection, clipboard, undo, scrolling, idle stability")
            from tools.test_gdi_ui import test_gdi_ui
            gdi_report = test_gdi_ui(qmp, serial_log, process, BUILD, wait_for, screenshot)
            checks.append("GDI window composition, clipping, restored occlusion, raster operations, scrolling, idle stability")
            qmp.command("human-monitor-command", {"command-line": "sendkey m"})
            wait_for(serial_log, "NEO98 VIEW minesweeper", process)
            time.sleep(0.2)
            from tools.test_mines import test_mines
            mine_report = test_mines(qmp, serial_log, process, BUILD, wait_for, screenshot)
            checks.append("playable ARM Minesweeper: mouse, flag cycle, first-click safety, loss, restart, win, menu, levels")
            qmp.command("human-monitor-command", {"command-line": "sendkey d"})
            wait_for(serial_log, "NEO98 VIEW diagnostics", process)
            time.sleep(0.2)
            first = screenshot(qmp, "boot")

            def pixel(data, x, y):
                pos = (y * 800 + x) * 3
                return tuple(data[pos:pos + 3])

            for x, y, expected in [(10, 90, (0, 128, 128)), (95, 103, (0, 0, 128)),
                                   (120, 310, (255, 255, 255)), (400, 480, (192, 192, 192))]:
                if pixel(first, x, y) != expected:
                    raise RuntimeError(f"Wrong framebuffer pixel at {x},{y}: {pixel(first, x, y)}")
            checks.append("800x600 framebuffer: desktop, title bar, panel, window colours")

            def key(name):
                qmp.command("human-monitor-command", {"command-line": f"sendkey {name}"})

            key("spc")
            wait_for(serial_log, "action=count counter=1", process)
            time.sleep(0.2)
            counted = screenshot(qmp, "count")
            if counted == first:
                raise RuntimeError("Counter did not change visibly")
            for y in range(600):
                row = y * 800 * 3
                spans = [(0, 126 * 3), (660 * 3, 800 * 3)] if 252 <= y < 270 else [(0, 800 * 3)]
                for start, end in spans:
                    if counted[row + start:row + end] != first[row + start:row + end]:
                        raise RuntimeError(f"Counter update changed pixels outside the status area, row {y}")
            checks.append("counter update changes only the 534x18 status rectangle")
            key("c")
            wait_for(serial_log, "action=colour counter=1 palette=1", process)
            time.sleep(0.2)
            changed = screenshot(qmp, "interaction")
            if pixel(changed, 10, 90) != (96, 64, 128):
                raise RuntimeError("Keyboard colour action did not redraw the framebuffer")
            checks.append("USB keyboard -> native command handler -> framebuffer redraw")
            key("r")
            wait_for(serial_log, "action=reset counter=0 palette=0", process)
            checks.append("reset action")
            wait_for(serial_log, "NEO98 TICK", process)
            checks.append("interrupt-driven timer")
            time.sleep(0.2)
            key("q")
            wait_for(serial_log, "NEO98 HALTED", process)
            checks.append("guest halt")
            frames = [{"kind": kind, "pixels": int(pixels), "elapsed_us": int(elapsed)}
                      for kind, pixels, elapsed in re.findall(
                          r"NEO98 FRAME kind=(\w+) pixels=(\d+) elapsed_us=(\d+)", serial_log.read_text())]
            full = [f for f in frames if f["kind"] == "full"]
            partial = [f for f in frames if f["kind"] == "partial"]
            if len(full) != 3 or not partial or any(f["pixels"] > 14236 for f in partial):
                raise RuntimeError(f"Unexpected full-screen redraw or dirty rectangle size: {frames}")
            checks.append("full redraws restricted to boot and two palette changes")
            report = {"passed": checks,
                      "suite": "full" if full_suite else "desktop",
                      **(application_reports or {}),
                      "frames": frames,
                      "minesweeper": mine_report,
                      "gdi": gdi_report,
                      "text": editor_report,
                      "user_drawing": json.loads((BUILD / "generated/user-provenance.json").read_text()),
                      "win98": json.loads((BUILD / "generated/provenance.json").read_text()),
                      "image_sha256": hashlib.sha256((OUT / "kernel8.img").read_bytes()).hexdigest()}
            report_path.write_text(json.dumps(report, indent=2) + "\n")
            print("PASS: " + "; ".join(checks))
            print(f"Screenshots: {OUT / 'boot.png'} and {OUT / 'interaction.png'}")
        finally:
            if qmp:
                try:
                    qmp.command("quit")
                except (OSError, RuntimeError):
                    pass
                qmp.close()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


def run(background, app="notepad"):
    display = "cocoa" if sys.platform == "darwin" else "gtk"
    if background:
        with (OUT / "interactive.log").open("w") as log:
            process = subprocess.Popen(qemu_args(display, f"file:{OUT / 'interactive-serial.log'}", app=app),
                                       env=tool_environment(), stdout=log, stderr=subprocess.STDOUT,
                                       stdin=subprocess.DEVNULL, start_new_session=True)
        time.sleep(1)
        if process.poll() is not None:
            raise RuntimeError((OUT / "interactive.log").read_text())
        (OUT / "interactive.pid").write_text(str(process.pid) + "\n")
        print(f"QEMU window started (PID {process.pid}); close the window to stop it.")
    else:
        from tools.app_programs import application
        print(application(app)["hint"], flush=True)
        subprocess.run(qemu_args(display, "stdio", app=app), env=tool_environment(), check=True)


HOST_COMMANDS = ("run", "host", "build", "cli", "build-cli")
PI_COMMANDS = ("pi-build", "pi-run", "pi-test", "pi-test-desktop")


def host_command(args, build_only=False):
    """The tools/host_run.py command line for `run` and `build`.

    `run` starts Explorer: host_run.py has it as its default, so --app only
    travels when the caller names a profile.
    """
    command = [str(analysis_python()), str(ROOT / "tools/host_run.py")]
    command += ["--target", args.target, "--gpu", args.gpu]
    if build_only:
        return command + ["--build-only"]
    command.append("--quiet")
    if args.exe:
        command += ["--exe", args.exe]
    elif args.app:
        command += ["--app", args.app]
    if args.command_line:
        command += ["--command", args.command_line]
    if args.net_listen:
        command += ["--net-listen", args.net_listen]
    if args.scale:
        command += ["--scale", str(args.scale)]
    if args.desktop:
        command += ["--desktop", args.desktop]
    if args.system_tree:
        command.append("--system-tree")
    if args.no_build:
        command.append("--no-build")
    if args.drive_c:
        command += ["--drive-c", args.drive_c]
    for mount in args.drive:
        command += ["--drive", mount]
    # Serial ports with an endpoint on the Mac.
    for wiring in args.com:
        port, separator, endpoint = wiring.partition("=")
        if not separator or port.upper() not in ("COM1", "COM2", "COM3", "COM4") or not endpoint:
            raise SystemExit(f"--com {wiring}: expected COM1..COM4=tcp:HOST:PORT or COMn=/dev/cu.NAME")
        os.environ["NEO98_" + port.upper()] = endpoint
    if args.cdrom:
        command += ["--cdrom", args.cdrom]
    if args.ram_drive:
        command.append("--ram-drive")
    if args.registry:
        command += ["--registry", args.registry]
    if args.no_registry_file:
        command.append("--no-registry-file")
    if args.no_programs_drive:
        command.append("--no-programs-drive")
    if args.programs_drive_writable:
        command.append("--programs-drive-writable")
    if args.programs_drive_readonly:
        command.append("--programs-drive-readonly")
    if args.seamless:
        command.append("--seamless")
    return command


def cli_command(args):
    from tools import native_app
    if not native_app.native_x64():
        raise RuntimeError("cli and build-cli currently require Linux x86_64")
    binary = ROOT / "build/native-app/neo98-cli"
    if not args.no_build:
        # Asset generators import pefile and capstone. Build in the same
        # pinned analysis environment as the GUI host, then run the ELF.
        subprocess.run([str(analysis_python()), "-c",
                        "from tools.native_app import build_fast; build_fast('neo98_cli.cpp', 'neo98-cli')"],
                       cwd=ROOT, check=True)
    if args.command == "build-cli":
        print(f"CLI: {binary}")
        return 0
    command = [str(binary)]
    if args.exe:
        command += ["--exe", args.exe]
    elif args.app:
        from tools.app_programs import application
        command += ["--exe", "C:\\" + application(args.app)["source"].replace("/", "\\")]
    else:
        raise RuntimeError("cli requires --exe or --app")
    if args.command_line:
        command += ["--command", args.command_line]
    if args.net_listen:
        command += ["--net-listen", args.net_listen]
    if args.drive_c:
        command += ["--drive-c", args.drive_c]
    for drive in args.drive:
        command += ["--drive", drive]
    if args.cdrom:
        command += ["--cdrom", args.cdrom]
    if args.registry:
        command += ["--registry", args.registry]
    if args.ram_drive:
        command.append("--ram-drive")
    if args.no_registry_file:
        command.append("--no-registry-file")
    for wiring in args.com:
        port, separator, endpoint = wiring.partition("=")
        if not separator or port.upper() not in ("COM1", "COM2", "COM3", "COM4") or not endpoint:
            raise RuntimeError("--com requires COM1..COM4=ENDPOINT")
        os.environ["NEO98_" + port.upper()] = endpoint
    return subprocess.call(command, cwd=ROOT)


def main():
    # The SDK for ARM64 applications has arguments of its own:
    # neo98.py sdk-build source.c -o program.exe [--gui]. docs/ARM64-APPS.md.
    if len(sys.argv) > 1 and sys.argv[1] == "sdk-build":
        from tools import arm64_sdk
        sys.argv = ["neo98.py sdk-build", *sys.argv[2:]]
        return arm64_sdk.main()
    parser = argparse.ArgumentParser(
        description="NEO98: Windows 98 programs on macOS ARM64 or Linux x64. Without a command: run.",
        epilog="run/host: start Explorer in the host window; build: build the host; "
               "cli/build-cli: Linux terminal runtime; app: platform app package; dmg: macOS disk image; "
               "test: the host test suite (same as test-host). "
               "pi-build/pi-run/pi-test/pi-test-desktop: the deferred Circle/QEMU kernel.")
    parser.add_argument("command", nargs="?", default="run",
                        choices=[*HOST_COMMANDS, "app", "dmg", "test", "test-host", *PI_COMMANDS, "setup",
                                 "import-winmine", "import-dibeng", "import-text", "import-user"])
    parser.add_argument("--iso", type=Path, help="setup: local, bootable Windows 98 SE ISO; without it setup only completes a finished build/win98-c")
    parser.add_argument("--setup-timeout", type=int, default=7200,
                        help="setup: maximum installation time in seconds (default 7200)")
    parser.add_argument("--reset", action="store_true",
                        help="setup: discard this ISO's incomplete generated install and start again")
    parser.add_argument("--winmine-exe", type=Path, help="original Windows 95 OSR2 WINMINE.EXE")
    parser.add_argument("--dibeng-dll", type=Path, help="original Windows 98 SE DIBENG.DLL")
    parser.add_argument("--background", action="store_true", help="pi-run: leave a graphical QEMU window running")
    parser.add_argument("--no-build", action="store_true",
                        help="run: use the existing host binary; pi-*: use the existing kernel image")
    from tools.host_build_options import add_arguments, configure
    add_arguments(parser)
    from tools.app_programs import APPLICATIONS
    parser.add_argument("--app", choices=tuple(APPLICATIONS),
                        help="profile of tools/app_programs.py; run starts explorer, the pi-* commands "
                             "default to notepad")
    parser.add_argument("--exe", help="run: start any program without a profile, by guest path "
                                      "(\"C:\\\\WINDOWS\\\\SOL.EXE\") out of the Win98 folder "
                                      "or by host path")
    parser.add_argument("--command-line", help="run/cli: full guest command line")
    parser.add_argument("--net-listen", metavar="ADDRESS",
                        help="run/cli: guest listener address, default 127.0.0.1; any allows LAN peers")
    parser.add_argument("--scale", type=int, help="run: window scale, 1 or 2")
    parser.add_argument("--desktop", metavar="WIDTHxHEIGHT",
                        help="run: size of the Windows desktop instead of the one the registry names")
    parser.add_argument("--system-tree", action="store_true",
                        help="run: mount the Win98 folder read-only under the RAM volume")
    # The drives of the host window.
    parser.add_argument("--drive-c", help="run: directory behind drive C:, default the Win98 folder "
                                          "of setup, build/win98-c")
    parser.add_argument("--drive", action="append", default=[],
                        help="run: mount another host directory, as in D=/pfad")
    parser.add_argument("--com", action="append", default=[], metavar="COMn=ENDPOINT",
                        help="run: connect a serial port to the Mac, as in COM2=tcp:127.0.0.1:7898 "
                             "or COM1=/dev/cu.usbmodem1; repeatable")
    parser.add_argument("--cdrom", help="run: mount a host directory, a CUE sheet or an ISO file as a read-only CD-ROM drive")
    parser.add_argument("--ram-drive", action="store_true",
                        help="run: use the volatile RAM volume instead of a host directory")
    parser.add_argument("--registry", help="run: file the registry changes are written to")
    parser.add_argument("--no-registry-file", action="store_true",
                        help="run: do not store registry changes")
    # D: is a persistent APFS clone of the reference system program collection.
    # tools/programs_drive.py.
    parser.add_argument("--no-programs-drive", action="store_true",
                        help="run: leave D: empty instead of mounting the program collection")
    parser.add_argument("--programs-drive-writable", action="store_true",
                        help="run: deprecated; the default uses a persistent writable APFS clone")
    parser.add_argument("--programs-drive-readonly", action="store_true",
                        help="run: mount the source collection read only")
    parser.add_argument("--seamless", action="store_true",
                        help="run: start in the seamless view ")
    args = parser.parse_args()
    if args.host_media:
        os.environ["NEO98_WITH_HOST_MEDIA"] = "1"
    try:
        internal_commands = {
            "import-winmine": "import_winmine.py", "import-dibeng": "import_dibeng.py",
            "import-text": "import_text.py", "import-user": "import_user.py",
            "test": "test_host.py", "test-host": "test_host.py",
            **{command: "import_win98.py" for command in PI_COMMANDS},
        }
        required = internal_commands.get(args.command)
        if required and not (ROOT / "tools" / required).is_file():
            parser.error(f"{args.command} is unavailable in the public release; it requires internal comparison tools")
        if args.command in HOST_COMMANDS or (args.command == "app" and sys.platform.startswith("linux")):
            configure(args.target, args.gpu, cli=args.command in ("cli", "build-cli"))
        elif args.target != "auto" or args.gpu != "auto":
            parser.error("--target and --gpu apply to build, run, host, build-cli, cli and the Linux app")
        if args.command == "setup":
            # Without --iso setup only completes a finished folder with the
            # steps a newer setup adds (tools/setup_win98.py, complete).
            from tools.setup_win98 import setup
            return setup(args.iso, args.setup_timeout, args.reset)
        if args.command == "import-user":
            user_assets(refresh=True)
            return 0
        if args.command == "import-text":
            win98_assets(); dibeng_assets()
            subprocess.run([str(analysis_python()), str(ROOT / "tools/import_text.py"),
                            str(BUILD / "dibeng/DIBENG.DLL"), str(BUILD / "win98-original/SSERIFE.FON")], check=True)
            return 0
        if args.command == "import-dibeng":
            if not args.dibeng_dll: parser.error("import-dibeng requires --dibeng-dll")
            import_dibeng(args.dibeng_dll)
            return 0
        if args.command == "import-winmine":
            if not args.winmine_exe: parser.error("import-winmine requires --winmine-exe")
            import_winmine(args.winmine_exe)
            return 0
        if args.command in ("test", "test-host"):
            # The host suite builds what it needs itself and never boots QEMU.
            from tools.test_host import main as test_host
            return test_host([])
        if args.command in ("app", "dmg"):
            if sys.platform.startswith("linux"):
                if args.command == "dmg": raise RuntimeError("DMG is a macOS format. On Linux, use neo98.py app.")
                return subprocess.call([str(analysis_python()), str(ROOT / "tools/linux_app.py"),
                                        *(["--no-build"] if args.no_build else [])])
            # Runtime resources are imported only when the user runs setup.
            return subprocess.call([str(analysis_python()), str(ROOT / "tools/mac_app.py"), args.command,
                                    *(["--no-build"] if args.no_build else [])])
        if args.command in HOST_COMMANDS:
            # build: the host runtime and build/native-app/neo98-host; run and
            # its old name host: the same, then the window with Explorer.
            if args.command not in ("build", "build-cli"):
                win98_c.require()
            if args.command in ("cli", "build-cli"):
                return cli_command(args)
            return subprocess.call(host_command(args, build_only=args.command == "build"))
        # The deferred Circle/QEMU kernel keeps Notepad as its default.
        app = args.app or "notepad"
        if args.command == "pi-build" or not args.no_build or not (OUT / "kernel8.img").exists():
            build(app)
        if args.command == "pi-test":
            test()
        elif args.command == "pi-test-desktop":
            test_desktop()
        elif args.command == "pi-run":
            run(args.background, app)
    except (RuntimeError, subprocess.CalledProcessError, OSError) as error:
        print(f"NEO98: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
