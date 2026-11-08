#!/usr/bin/env python3
"""NEO98 on macOS ARM64 and Linux x64: set up, build and run the host.

`python3 neo98.py` (or `run`) starts Explorer in the host window,
`build` builds that host.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import venv

ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"
# Imported originals and generated assets are shared; host logs and
# test results go here, so a second checkout can keep its own with
# NEO98_OUTPUT_DIR without re-importing anything.
OUT = Path(os.environ.get("NEO98_OUTPUT_DIR") or BUILD)

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
    modules = "pefile, unicorn, capstone, PIL" + (", dmgbuild" if sys.platform == "darwin" else "")
    probe = subprocess.run([str(python), "-c", "import " + modules],
                           capture_output=True) if python.exists() else None
    if not probe or probe.returncode:
        # An existing environment only gets the packages it lacks.
        if not python.exists():
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
        # Always the file of the Win98 folder in use: a copy left from an
        # earlier installation would be imported in its place.
        target = BUILD / "gdi-audit/USER.EXE"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.unlink(missing_ok=True)
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


def source_revision(short=False):
    """The commit of the sources, for reports: NEO98_REVISION when a tool such
    as a workspace runs a copy without a repository, otherwise git."""
    revision = os.environ.get("NEO98_REVISION")
    if not revision:
        try:
            revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True,
                                               stderr=subprocess.DEVNULL).strip()
        except (OSError, subprocess.CalledProcessError):
            return "unknown"
    return revision[:12] if short else revision


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


HOST_COMMANDS = ("run", "host", "build", "cli", "build-cli")


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
               "test: the host test suite (same as test-host).")
    parser.add_argument("command", nargs="?", default="run",
                        choices=[*HOST_COMMANDS, "app", "dmg", "test", "test-host", "setup",
                                 "import-winmine", "import-dibeng", "import-text", "import-user"])
    parser.add_argument("--iso", type=Path, help="setup: local, bootable Windows 98 SE ISO; without it setup only completes a finished build/win98-c")
    parser.add_argument("--setup-timeout", type=int, default=7200,
                        help="setup: maximum installation time in seconds (default 7200)")
    parser.add_argument("--reset", action="store_true",
                        help="setup: discard this ISO's incomplete generated install and start again")
    parser.add_argument("--winmine-exe", type=Path, help="original Windows 95 OSR2 WINMINE.EXE")
    parser.add_argument("--dibeng-dll", type=Path, help="original Windows 98 SE DIBENG.DLL")
    parser.add_argument("--no-build", action="store_true",
                        help="run: use the existing host binary")
    from tools.host_build_options import add_arguments, configure
    add_arguments(parser)
    from tools.app_programs import APPLICATIONS
    parser.add_argument("--app", choices=tuple(APPLICATIONS),
                        help="profile of tools/app_programs.py; run starts explorer")
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
    except (RuntimeError, subprocess.CalledProcessError, OSError) as error:
        print(f"NEO98: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
