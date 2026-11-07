#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Build and exercise the native Windows process runtime on the ARM Mac."""
import hashlib
import json
import os
import platform
import shutil
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
sys.path.insert(0, str(ROOT / 'tools'))
import neo98

sources = ['x86_vga', 'x86_state', 'x86_state_fields', 'x86_state_machine', 'x86_state_resume', 'x86_dx_driver', 'x86_dx_d3d', 'x86_dx_sound', 'arm64_cpu', 'arm64_unwind', 'x86_screen_lock', 'glide_context', 'x86_glide', 'x86_desktop', 'x86_screen_saver', 'x86_winhelp', 'x86_host_open', 'win16_config_manager', 'win16_common_controls', 'win16_property_sheet', 'x86_dinput_vxd', 'x86_dsound_vxd', 'x86_device_change', 'x86_inet16', 'x86_tsp3216s', 'x86_crypto_provider', 'x86_wsock2', 'win16_toolhelp', 'x86_mscdex', 'cd_audio', 'win16_mmthread', 'win16_mmio', 'win16_driver', 'win16_capture', 'x86_thunk_hosted', 'x86_mm_driver', 'x86_print_setup', 'x86_print_draw', 'x86_print_document', 'config_manager', 'x86_kernel_lpt', 'x86_print_spool', 'win16_print', 'gdi_truetype', 'truetype_machine', 'opengl_context', 'x86_opengl', 'x86_print_driver', 'x86_win16_hosted', 'x86_user32_gates', 'x86_gdi32_gates', 'x86_comdlg32_completion', 'x86_win_event', 'x86_registry_hives', 'x86_mmio', 'x86_winmm_completion', 'x86_gdi32_completion', 'x86_user32_completion', 'dos_vm_truetype', 'x86_static_control', 'x86_pif_pages', 'x86_pif_options', 'x86_ddhal', 'x86_pifmgr', 'x86_mixer', 'dos_vm', 'dos_voodoo', 'x86_dos_vm', 'x86_census_imports', 'x86_scroll_dc', 'x86_mci', 'sound_synth', 'x86_midi_out', 'x86_dmusic16', 'win16_sound', 'win16_commdlg', 'x86_session_arena', 'x86_interrupts', 'sound_mixer', 'sound_microphone', 'pc_speaker', 'x86_wave_out', 'x86_wave_in', 'ne_loader', 'win16_user', 'win16_metafile', 'win16_shell', 'x86_module_image', 'x86_ddraw_driver', 'x86_mm_timer', 'x86_mm_devices', 'x86_advapi_registry', 'x86_advapi_crypto', 'x86_mdi', 'x86_gdi_fonts', 'x86_gdi_device', 'x86_gdi_paths', 'x86_gdi_metafile', 'x86_user_arrange', 'x86_window_exports', 'x86_window_icon', 'x86_window_info', 'x86_dlgdir', 'x86_user_draw_state', 'x86_user_session', 'x86_gdi_imports', 'x86_accelerators', 'x86_keyboard_translate', 'x86_user_regions', 'x86_dde_params', 'x86_user32_imports', 'x86_cursor', 'x86_frame_track', 'x86_kernel_ordinals_b', 'x86_kernel_pipes', 'x86_kernel_ordinals_a', 'x86_dde', 'x86_change_notification', 'x86_process_dword', 'x86_local_handles', 'x86_flat_thunk', 'x86_version16', 'x86_ddraw16', 'x86_dciman16', 'x86_dskapi16', 'x86_tooltip', 'x86_updown', 'x86_property_sheet', 'x86_tab', 'x86_message_box', 'x86_crt_output', 'x86_coordinates', 'x86_monitors', 'x86_multiwait', 'x86_font_create', 'x86_caret', 'x86_font_charset', 'x86_font_width', 'x86_font_query', 'x86_shell_search', 'x86_shell_path_syntax', 'x86_keyboard_layout', 'x86_profile', 'profile_files', 'x86_menu_tracking', 'x86_common_init', 'x86_shell_registry', 'x86_shell_icons', 'x86_environment', 'x86_pointer_probe', 'x86_clipboard', 'x86_thread_control', 'x86_time', 'x86_kernel_paths', 'x86_thread_crt', 'x86_properties', 'x86_mapping', 'x86_kernel_objects', 'x86_semaphore', 'win98_crt_args', 'x86_args', 'win98_crt_locale', 'x86_locale', 'x86_hooks', 'virtual_memory', 'x86_memory', 'x86_mutex', 'x86_resources', 'x86_datafile', 'x86_critical', 'x86_activation', 'x86_atoms', 'x86_windows', 'x86_shell', 'x86_listbox', 'x86_combobox', 'x86_window_text', 'x86_user', 'win98_parameters', 'x86_gdi', 'x86_modules', 'win98_nls', 'x86_nls', 'win98_find', 'x86_find', 'x86_heap', 'win98_kernel', 'files', 'system_tree', 'volume', 'x86_files', 'win98_paths', 'win98_printf', 'x86_timers', 'win98_strings', 'x86_position', 'registry', 'registry_persist', 'x86_registry', 'x86_status', 'x86_list', 'win98_tree', 'tree_model', 'x86_tree', 'win_image', 'x86_image', 'win98_arrays', 'x86_common', 'win98_crt', 'fiber', 'win98_dialog', 'dialog_template', 'x86_dialog', 'x86_static_tls', 'x86_threads', 'x86_format', 'x86_crt', 'gdi_font_catalog', 'x86_font_catalog', 'x86_cpu_unicorn', 'x86_cpu_dual', 'x86_address_space', 'x86_app', 'crash_report', 'crash_collect', 'win98_exports', 'pe32', 'user', 'user_draw', 'gdi', 'dib_text', 'dibeng', 'win98_rect', 'text_edit', 'x86_cpu']

# A case of an API switch that does not return must say break; falling into the
# next API's handler would be silent otherwise.
FLAGS = ['-std=c++17', '-O1', '-g', '-fsanitize=address,undefined', '-Werror=implicit-fallthrough']


def export_contract_assets():
    """Public builds use the checked-in API contracts, without Windows media."""
    for name in ('export_contract', 'native_module_exports', 'kernel_unsupported'):
        if not (ROOT / 'src' / ('interface_' + name + '.h')).is_file():
            raise RuntimeError('Missing public API contract: ' + name)


def comparison_assets():
    """Load optional fixtures for internal comparison runners."""
    import importlib
    if not (ROOT / 'tools/unicorn_assets.py').is_file():
        raise RuntimeError('Comparison fixtures are unavailable in the public release; use a runtime-content host')
    importlib.import_module('unicorn_assets').generate()


def unicorn_library():
    """The Unicorn the C++ runners link against: our own build of the pinned tree.

    The pip package's library cannot carry the patches the host needs, so
    tools/build_unicorn_host.py builds third_party/unicorn itself. It returns
    the existing file when its stamp still matches, so this costs nothing in a
    warm tree and builds itself in a fresh checkout. Python tools that
    `import unicorn` keep using the pip package.
    """
    if native_x64():
        return None
    from build_unicorn_host import library
    return library()


def native_x64():
    return sys.platform == 'linux' and platform.machine() == 'x86_64'


def fiber_source():
    return ROOT / ('src/fiber_x86_64.S' if native_x64() else 'src/fiber_aarch64.S')


def runtime_sources():
    return runtime_names(sources)


def dos_vm_objects(flags, cache):
    """The DOS machine: tools/host_dos_vm.cpp, the provider
    over the libretro API, which opens the DOSBox Pure core as a dynamic
    library, one image per session.
    The interactive host always links them; a headless runner asks for them
    with build_test(..., dos_vm=True). The core is built by
    tools/build_dosbox_pure.py and reused while its stamp matches."""
    from build_dosbox_pure import core_dylib, state_identity
    from native_build import compile_objects
    # The core is a dynamic library the provider opens once per session;
    # the provider knows where it lies.
    core = core_dylib()
    objects = compile_objects(ROOT, [ROOT / 'tools/host_dos_vm.cpp'], [*flags, f'-DNEO98_DOSBOX_CORE="{core.relative_to(ROOT)}"', f'-DNEO98_DOS_CORE_ID="{state_identity()}"'],
                              neo98.tool_environment(), cache)
    return objects



def link_runner(flags, compile_flags, source, inputs, link_flags, temporary, env):
    """Compile tools/<source> and link it with the given inputs into temporary.

    Compiling and linking in two steps keeps the clang driver from running
    dsymutil: with -g and a source file on the link line it writes a dSYM
    bundle for every runner, costing about a gigabyte of memory and several
    cores per test, and the bundle was deleted right after the link anyway.
    """
    obj = Path(str(temporary) + '.o')
    try:
        subprocess.run(['clang++', *flags, *compile_flags, '-c', str(ROOT / 'tools' / source), '-o', str(obj)],
                       cwd=ROOT, env=env, check=True)
        subprocess.run(['clang++', *flags, str(obj), *map(str, (i for i in inputs if i is not None)), *link_flags, '-o', str(temporary)],
                       cwd=ROOT, env=env, check=True)
        sign_for_hypervisor(temporary)
    finally:
        obj.unlink(missing_ok=True)


def sign_for_hypervisor(binary):
    """Ad-hoc signature with the entitlement Hypervisor.framework asks for.
    ARM64 programs run in a machine of that framework
    (tools/host_arm64_cpu.cpp, docs/ARM64-APPS.md); without the entitlement
    hv_vm_create refuses and such a program does not start. x86 programs do
    not need it."""
    if sys.platform != 'darwin':
        return
    subprocess.run(['codesign', '--force', '--sign', '-', '--entitlements',
                    str(ROOT / 'tools/arm64_hypervisor.entitlements'), str(binary)], check=True, capture_output=True)


def arm64_objects(flags, cache):
    """The machine of ARM64 processes on macOS and its framework."""
    if sys.platform != 'darwin':
        return [], []
    from native_build import compile_objects
    objects = compile_objects(ROOT, [ROOT / 'tools/host_arm64_cpu.cpp'], flags, neo98.tool_environment(), cache)
    # The ARM64 forms of DDRAW.DLL and DSOUND.DLL travel in the runner
    # (tools/host_arm64_modules.cpp).
    import arm64_dx
    arm64_dx.system_modules()
    objects += compile_objects(ROOT, [ROOT / 'tools/host_arm64_modules.cpp'], flags, neo98.tool_environment(), cache)
    return objects, ['-framework', 'Hypervisor']


def arm64_table():
    """build/generated/arm64_api_table.inc, which src/arm64_cpu.cpp includes."""
    import arm64_sdk
    arm64_sdk.generate_table()

def stamp_flags(flags):
    """Git revision and build flags for the crash report, defined for the tool source only.

    They are appended to the final compile of tools/<runner>.cpp, never to the
    shared runtime objects, so a new commit does not invalidate the object cache.

    """
    try:
        revision = subprocess.check_output(['git', 'rev-parse', '--short', 'HEAD'], cwd=ROOT,
                                           text=True, stderr=subprocess.DEVNULL).strip()
        if subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True,
                                   stderr=subprocess.DEVNULL).strip():
            revision += '+local changes'
    except Exception:
        revision = 'unknown'
    return [f'-DNEO98_REVISION="{revision}"', '-DNEO98_BUILD_FLAGS="' + ' '.join(flags) + '"']


def host_metal_objects(flags, cache):
    """The Metal rasterizer behind the Direct3D HAL, tools/host_metal.mm, and
    the frameworks it links against. Only the macOS host has it; the runners
    of other platforms keep the display driver without a HAL. """
    if sys.platform != 'darwin':
        return [], []
    from native_build import compile_objects
    objects = compile_objects(ROOT, [ROOT / 'tools/host_metal.mm'], [*flags, '-fobjc-arc'],
                              neo98.tool_environment(), cache)
    # The shader translation of the backend, plain C++ of the host.
    objects += compile_objects(ROOT, [ROOT / 'src/d3d_shader_msl.cpp'], flags, neo98.tool_environment(), cache)
    return objects, ['-framework', 'Metal', '-framework', 'Foundation']


def with_host_media(host_media=None):
    """Host media (Kodi on the host, Media Center 98, the add-on service.neo98)
    is a gimmick for the phone and the tablet and stays out of the usual
    build: NEO98_WITH_HOST_MEDIA=1 (or host_media=True) builds it in."""
    return host_media if host_media is not None else os.environ.get('NEO98_WITH_HOST_MEDIA') == '1'


def host_menu_objects(flags, cache, host_media=False):
    """The macOS menu bar of the host window, tools/host_menu.mm: restart,
    crash report, CD-ROM, drive D:, scale and full screen, and the setup
    assistant and settings window of the app, tools/host_setup.mm. Only the
    SDL host links them. docs/BUILDING.md."""
    if sys.platform != 'darwin':
        return [], []
    from native_build import compile_objects
    objects = compile_objects(ROOT, [ROOT / 'tools/host_menu.mm', ROOT / 'tools/host_setup.mm',
                                     ROOT / 'tools/host_seamless.mm', ROOT / 'tools/host_scroll_watch.mm',
                                     ROOT / 'tools/host_dock.mm',
                                     ROOT / 'tools/host_pasteboard.mm', ROOT / 'tools/host_game_menu.mm',
                                     ROOT / 'tools/host_state_window.mm', ROOT / 'tools/host_devices/macos.mm',
                                     ROOT / 'tools/host_microphone_mac.mm',
                                     *([ROOT / 'tools/host_media/picture_macos.mm', ROOT / 'tools/host_media/capture_macos.mm']
                                       if host_media else [])],
                              [*flags, '-fobjc-arc'], neo98.tool_environment(), cache)
    frameworks = ['-framework', 'AppKit', '-framework', 'QuartzCore', '-framework', 'UniformTypeIdentifiers', '-framework', 'CoreWLAN', '-framework', 'CoreLocation', '-framework', 'IOKit', '-framework', 'AVFoundation', '-framework', 'CoreMedia', '-framework', 'CoreVideo']
    if host_media:
        frameworks += ['-framework', 'ImageIO', '-framework', 'ScreenCaptureKit']
    return objects, frameworks


def fex_objects(flags, cache):
    """FEXCore behind X86::Cpu, the engine of 32-bit processes on the macOS
    host (tools/host_fex_cpu.cpp). Always linked on
    macOS; tools/build_fex_host.py builds the patched libraries the first time.
    NEO98_CPU=unicorn goes back to Unicorn at run time, and NEO98_WITH_FEX=0
    leaves FEXCore out of the link altogether."""
    if native_x64():
        from native_build import compile_objects
        return compile_objects(ROOT, [ROOT / 'tools/host_native_cpu.cpp', ROOT / 'tools/direct_bridge.S'], flags, neo98.tool_environment(), cache)
    if sys.platform != 'darwin' or os.environ.get('NEO98_WITH_FEX') == '0':
        return []
    import build_fex_host
    from native_build import compile_objects
    libraries = build_fex_host.libraries()
    objects = compile_objects(ROOT, [ROOT / 'tools/host_fex_cpu.cpp'], [*flags, *build_fex_host.compile_flags()],
                              neo98.tool_environment(), cache)
    return [*objects, *libraries]


def test_engines():
    """The x86 engines a test runs its checks on, one after the other: FEX
    and Unicorn on macOS, the native CPU on Linux x64, Unicorn elsewhere. The
    names are values of NEO98_CPU (src/x86_cpu.cpp, ProcessCpuBackend)."""
    if native_x64():
        return ('native',)
    if sys.platform == 'darwin':
        return ('fex', 'unicorn')
    return ('unicorn',)


# Every engine name a test accepts on its command line (--cpu, --engine).
ENGINE_NAMES = ('fex', 'unicorn', 'native')


def process_engine():
    """The engine a runner built by this module gives a 32-bit process:
    'fex' on macOS unless NEO98_CPU=unicorn or NEO98_WITH_FEX=0 say
    otherwise, 'native' on Linux x64 and 'unicorn' on other platforms
    (src/x86_cpu.h, ProcessCpuBackend)."""
    if native_x64():
        return os.environ.get('NEO98_CPU', 'native')
    if os.environ.get('NEO98_CPU') == 'fex':
        return 'fex'
    if sys.platform != 'darwin' or os.environ.get('NEO98_CPU') == 'unicorn' or os.environ.get('NEO98_WITH_FEX') == '0':
        return 'unicorn'
    return 'fex'


def engine_link_extra():
    """What a test that links its own runner against the sanitized runtime
    objects adds to its link line for the second engine: the FEXCore
    provider and its libraries when fex_objects builds them, else nothing."""
    return fex_objects(FLAGS, ROOT / 'build/native-app/object-cache')


def runtime_names(names):
    """The runtime sources of names a test links on this platform: all of
    them, but on Linux x64, where the runtime runs x86 code on the native CPU,
    without the Unicorn provider."""
    return [name for name in names if not (native_x64() and name == 'x86_cpu_unicorn')]


def engine_flags():
    """The compile flags a test adds when it compiles runtime sources of its
    own: on Linux x64 the runtime has no Unicorn."""
    return ['-DNEO98_NO_UNICORN'] if native_x64() else []


def unicorn_link(library):
    """Unicorn on the link line of a test runner: the library and its rpath.
    The runtime on Linux x64 has no Unicorn; its runner links the system
    libraries of the native engine instead, which engine_link_extra adds."""
    if native_x64():
        return ['-ldl', '-pthread']
    return [str(library), '-Wl,-rpath,' + str(Path(library).parent)]


def runtime_objects():
    """Compile the shared runtime once; objects follow `sources`, fiber first."""
    from native_build import compile_objects
    arm64_table()
    out = ROOT / 'build/native-app'
    out.mkdir(exist_ok=True)
    return compile_objects(ROOT,
        [fiber_source(), *[ROOT / 'src' / f'{s}.cpp' for s in runtime_sources()]],
        FLAGS, neo98.tool_environment(), out / 'object-cache')


def object_directory():
    """The runtime objects under the <source>.cpp.o names the guests link against."""
    objects = runtime_objects()
    directory = ROOT / 'build/native-app/objects'
    directory.mkdir(parents=True, exist_ok=True)
    for name, artifact in zip([fiber_source().name + '.o'] + [f'{s}.cpp.o' for s in runtime_sources()], objects):
        link = directory / name
        if link.is_symlink() or link.exists():
            link.unlink()
        link.symlink_to(artifact)
    return directory


def build_test(source, name, *, prepare_assets=True, dos_vm=False, vulkan=None):
    # Sources include generated headers. Materialise the cheap ones even when
    # the caller asked to skip the generators, otherwise the compile fails and
    # the caller silently keeps whatever runner was built last.
    from serialui_assets import generate as serialui_assets
    serialui_assets()
    export_contract_assets()
    neo98.marlett_assets()
    neo98.user_resource_assets()
    if prepare_assets:
        neo98.win98_assets();neo98.text_assets();neo98.user_assets();neo98.frame_control_assets()
        neo98.dialog_assets();neo98.registry_assets()
        comparison_assets()
        neo98.message_table_assets()
    out = ROOT / 'build/native-app'
    out.mkdir(exist_ok=True)
    binary = out / name
    library = unicorn_library()
    env = neo98.tool_environment()
    objects = runtime_objects()
    metal, frameworks = host_metal_objects(FLAGS, out / 'object-cache')
    extra = dos_vm_objects(FLAGS, out / 'object-cache') if dos_vm else []
    # DOS probes use the same GPU selection as the desktop host. Without
    # this, NEO98_PROBE_GPU silently installs no backend on Linux.
    with_vulkan = native_x64() and (vulkan if vulkan is not None else
                                  dos_vm and os.environ.get('NEO98_WITH_VULKAN', '1') == '1')
    gpu_flags = ['-DNEO98_WITH_VULKAN'] if with_vulkan else []
    if with_vulkan:
        extra += vulkan_objects()
        frameworks += ['-lvulkan']
    extra += fex_objects(FLAGS, out / 'object-cache')
    machine, machine_frameworks = arm64_objects(FLAGS, out / 'object-cache')
    extra += machine
    frameworks = [*frameworks, *machine_frameworks]
    # Two tests may want the same runner at the same time: link to a private
    # name and move it into place, so neither reads a half-written binary.
    temporary = out / f'.{name}.{os.getpid()}'
    link_runner(FLAGS, [*stamp_flags(FLAGS), *gpu_flags], source, [*objects, *metal, *extra, library],
                [*(['-Wl,-rpath,' + str(library.parent)] if library else ['-ldl', '-pthread']), *frameworks], temporary, env)
    os.replace(temporary, binary)
    return binary


FAST_FLAGS = ['-std=c++17', '-O2', '-g', '-Werror=implicit-fallthrough']
if native_x64():
    FLAGS += ['-DNEO98_NO_UNICORN']
    FAST_FLAGS += ['-DNEO98_NO_UNICORN']
    # The chain of a waiting thread (MachineState::Chains, the step
    # wstate=chains) follows the frame pointers of its host stack, which
    # ARM64 always keeps and x86-64 drops at -O2.
    FAST_FLAGS += ['-fno-omit-frame-pointer', '-mno-omit-leaf-frame-pointer']
# The sources of src/ in the optimised builds keep every variable with static
# storage whole and at one address: the layout table of a saved state reads
# them out of the debug information (tools/state_layout.py), and the pass
# that splits a static structure into its members, or shrinks a static to a
# flag, leaves pieces the debug information no longer describes. The table's
# check takes the table away should the compiler stop honouring this.
# Measured 2026-10-15 (tools/bench_dispatch.py, two rounds each): with the
# pass the fixtures moved by -5 to +1 % and Pinball by about -4 % CPU per API
# call, within the noise of a loaded machine; the flag stays.
STATE_FLAGS = ['-mllvm', '-opt-disable=globalopt', '-mllvm', '-opt-bisect-verbose=0']
if native_x64():
    # LLVM 18 runs O2 through an explicit pipeline without globalopt.
    # The globals then retain the objects described by their DWARF types.
    STATE_FLAGS = ['-fstandalone-debug', '--neo98-state-layout']


def state_layout(binary, cache_name='object-cache-fast'):
    """The table of the fields of a saved state beside the host
    (tools/state_layout.py). It belongs to this
    very link of the program.

    Saved states are an extra, so their guards never stop a build. A pointer
    the table can neither follow nor find a rule for, or a function that
    runs the guest without its line in src/state_rules/frames.calls, leaves
    the host without a table: it runs as ever and refuses to save Windows
    programs, with the reason. What is missing goes to
    <binary>.layout-problems; the state tests ask for the table and fail."""
    env = dict(os.environ, NEO98_STATE_OBJECT_CACHE=str(ROOT / 'build/native-app' / cache_name))
    # Of the optimised build only for the frames: who is inlined into whom
    # is the compiler's decision, and the list is that of the build people run.
    found = state_guards(binary, env, frames=cache_name == 'object-cache-fast')
    state_problems(binary, found)


def state_guards(binary, env, frames=True, output=None):
    """Run the two guards of saved states; what they miss, as lines.

    The table (tools/state_layout.py) is written beside the binary or to
    output. The second guard: every function that runs the guest, calls into
    it or suspends a thread has its line in src/state_rules/frames.calls,
    which says what a saved state does with a thread inside it
    (tools/state_frames.py)."""
    found = []
    # readelf, nm and objdump in English: a German locale writes "Build-ID".
    env = dict(env, LC_ALL='C')
    command = [sys.executable, str(ROOT / 'tools/state_layout.py'), '--binary', str(binary)]
    result = subprocess.run([*command, *(['--output', str(output)] if output else [])],
                            env=env, capture_output=True, text=True)
    if result.returncode:
        found.append('state layout: add a rule in src/state_rules/ or type the pointer')
        found += (result.stdout + result.stderr).splitlines()
    if frames:
        result = subprocess.run([sys.executable, str(ROOT / 'tools/state_frames.py'), '--binary', str(binary)],
                                env=env, capture_output=True, text=True)
        if result.returncode:
            found.append('state frames: give the function its line in src/state_rules/frames.calls '
                         '')
            found += (result.stdout + result.stderr).splitlines()
    return found


def state_problems(binary, found):
    """Without a complete table the host keeps no Windows states and says why."""
    problems = Path(str(binary) + '.layout-problems')
    problems.unlink(missing_ok=True)
    if not found:
        return
    Path(str(binary) + '.layout').unlink(missing_ok=True)
    problems.write_text('\n'.join(found) + '\n')
    print(f'warning: {Path(binary).name} cannot save Windows programs; see {problems}', file=sys.stderr)
    for line in found[:12]:
        print('  ' + line, file=sys.stderr)


def release_dependencies(objects, cache, binary):
    """Reject Windows fixture headers in the objects selected for this link."""
    from check_release_embedded import check_dependencies
    from native_build import SELECTED_ENTRIES
    dependencies = set()
    selected = {Path(obj).stem for obj in objects}
    matched = set()
    # Objects compiled in this process: exactly the entries of this build.
    unknown = set()
    for obj in objects:
        entries = SELECTED_ENTRIES.get(str(obj))
        if not entries:
            unknown.add(Path(obj).stem)
            continue
        for entry in entries:
            record = json.loads(Path(entry).read_text())
            matched.add(record['object_sha256'])
            dependencies.update(record['dependencies'])
    # Other objects: every cache entry that produced those bytes.
    if unknown:
        for entry in (cache / 'entries').glob('*.json'):
            record = json.loads(entry.read_text())
            if record['object_sha256'] in unknown:
                matched.add(record['object_sha256'])
                dependencies.update(record['dependencies'])
    if matched != selected:
        raise RuntimeError('Missing compiler dependency records for release objects')
    problems = check_dependencies(dependencies)
    if problems:
        raise RuntimeError('\n'.join(problems))
    Path(str(binary) + '.dependencies.json').write_text(json.dumps(sorted(dependencies), indent=2) + '\n')


def build_fast(source, name, *, compile_flags=(), link_flags=(), extra_inputs=(), replaced=None):
    """Link a headless runner against the optimised, non-sanitized runtime.

    Benchmarks (tools/bench_dispatch.py) use it; tests keep the sanitizer build.
    The objects are shared with build_host through object-cache-fast.
    replaced: {name of a source of src/: a changed copy of it}, as in build_host.
    """
    replaced = replaced or {}
    hives_only = source in ('neo98_linux.cpp', 'neo98_cli.cpp', 'neo98_kms.cpp')
    fast_flags = [*FAST_FLAGS, *(['-DNEO98_RUNTIME_CONTENT'] if hives_only else [])]
    if native_x64():
        os.environ['PATH'] = neo98.tool_environment()['PATH']
    if not hives_only:
        if native_x64():
            neo98.win98_assets(); neo98.text_assets(); neo98.user_assets(); neo98.frame_control_assets()
            neo98.dialog_assets(); neo98.registry_assets(); neo98.message_table_assets()
            comparison_assets()
        from serialui_assets import generate as serialui_assets
        serialui_assets()
        neo98.marlett_assets(); neo98.user_resource_assets()
    from native_build import compile_objects
    export_contract_assets()
    out = ROOT / 'build/native-app'
    out.mkdir(exist_ok=True)
    env = neo98.tool_environment()
    arm64_table()
    objects = compile_objects(ROOT,
        [fiber_source(), *[ROOT / 'src' / f'{s}.cpp' for s in runtime_sources()
                           if not (hives_only and s == 'registry') and f'{s}.cpp' not in replaced]],
        [*fast_flags, *STATE_FLAGS], env, out / 'object-cache-fast')
    if replaced:
        objects += compile_objects(ROOT, list(replaced.values()), [*fast_flags, *STATE_FLAGS, '-I', str(ROOT / 'src')],
                                   env, out / 'object-cache-fast')
    if hives_only:
        objects += compile_objects(ROOT, [ROOT / 'src/registry.cpp'], [*fast_flags, *STATE_FLAGS, '-DNEO98_NO_REGISTRY_IMAGE'], env, out / 'object-cache-fast')
    library = unicorn_library()
    metal, frameworks = host_metal_objects(fast_flags, out / 'object-cache-fast')
    metal = [*metal, *fex_objects(fast_flags, out / 'object-cache-fast')]
    if native_x64():
        metal += compile_objects(ROOT, [ROOT / 'tools/host_state_alloc_linux.cpp'], fast_flags, env, out / 'object-cache-fast')
    machine, machine_frameworks = arm64_objects(fast_flags, out / 'object-cache-fast')
    metal += machine
    frameworks = [*frameworks, *machine_frameworks]
    binary = out / name
    if hives_only:
        release_dependencies(objects, out / 'object-cache-fast', binary)
    temporary = out / f'.{name}.{os.getpid()}'
    link_runner(fast_flags, [*stamp_flags(fast_flags), *compile_flags], source, [*metal, library, *extra_inputs],
                [*(['-Wl,-rpath,' + str(library.parent)] if library else ['-ldl', '-pthread']), *frameworks,
                 *map(str, objects), *link_flags], temporary, env)
    if native_x64():
        records = []
        linked = {str(obj) for obj in objects}
        for entry in (out / 'object-cache-fast/entries').glob('*.json'):
            record = json.loads(entry.read_text())
            obj = out / 'object-cache-fast/objects' / (record['object_sha256'] + '.o')
            if str(obj) in linked: records.append({'source': record['source'], 'object': str(obj)})
        manifest = Path(str(temporary) + '.objects.json')
        manifest.write_text(json.dumps(records))
        layout = Path(str(temporary) + '.layout')
        try:
            found = state_guards(temporary, env, output=layout) if source == 'neo98_linux.cpp' else []
            os.replace(temporary, binary)
            os.replace(manifest, Path(str(binary) + '.objects.json'))
            if layout.exists() and not found: os.replace(layout, Path(str(binary) + '.layout'))
            if source == 'neo98_linux.cpp': state_problems(binary, found)
        finally:
            temporary.unlink(missing_ok=True); manifest.unlink(missing_ok=True); layout.unlink(missing_ok=True)
    else:
        os.replace(temporary, binary)
    return binary


def build_host(source='neo98_host.cpp', name='neo98-host', replaced=None, sanitized=False, *, vulkan=None, host_media=None, runtime_content=None):
    """Link the interactive SDL2 runner against a non-sanitized, optimised runtime.

    The sanitizer build stays the one every test uses; this variant exists so a
    person can drive the same code at full speed in a window on the Mac.

    sanitized: the same host with the address and undefined behaviour
    sanitizers, with objects of its own (`neo98-host-asan`). The tests of the
    saved states run it to find what a load leaves dangling
    (tools/test_state_host.py --asan).

    host_media: build host media in (with_host_media); by default
    NEO98_WITH_HOST_MEDIA=1 decides.
    runtime_content: load DLL/font resources from C:, always enabled for the
    Mac host; tests can also select it to exercise the release resource path.
    """
    from build_host_devices import build as build_hardware_manager
    build_hardware_manager()
    host_media = with_host_media(host_media)
    media_flags = ['-DNEO98_WITH_HOST_MEDIA'] if host_media else []
    if host_media:
        from build_host_media import build as build_media_center
        build_media_center()
    fast = FLAGS if sanitized else FAST_FLAGS
    cache_name = 'object-cache-state-asan' if sanitized else 'object-cache-fast'
    if sanitized and name == 'neo98-host':
        name = 'neo98-host-asan'
    if native_x64() and source == 'neo98_host.cpp':
        if sanitized: raise RuntimeError('The Linux GUI sanitizer build is not yet connected')
        cflags = subprocess.check_output(['sdl2-config', '--cflags'], text=True).split()
        libs = subprocess.check_output(['sdl2-config', '--libs'], text=True).split()
        gtk_flags, gtk_libs = linux_gtk_flags()
        cflags += gtk_flags
        libs += gtk_libs
        extra = []
        # As the release package (tools/linux_app.py): Vulkan unless
        # NEO98_WITH_VULKAN=0, so every test that builds the host gets the
        # same one and none replaces it with a host without a rasterizer.
        with_vulkan = vulkan if vulkan is not None else os.environ.get('NEO98_WITH_VULKAN', '1') == '1'
        if with_vulkan:
            extra = vulkan_objects()
            cflags += ['-DNEO98_WITH_VULKAN']
            libs += ['-lvulkan']
        from native_build import compile_objects
        extra += compile_objects(ROOT, [ROOT / 'tools/host_devices/linux.cpp',
                                        *([ROOT / 'tools/host_media/picture_linux.cpp'] if host_media else [])], [*FAST_FLAGS, *gtk_flags],
                                 neo98.tool_environment(), ROOT / 'build/native-app/object-cache-fast')
        cflags += media_flags
        if host_media:
            # Kodi draws into a Wayland compositor of the host, and the GPU
            # copies its picture (tools/host_wayland).
            drm_flags = subprocess.check_output(['pkg-config', '--cflags', 'libdrm'], text=True).split()
            protocols, protocol_objects, _ = wayland_protocols()
            extra += compile_objects(ROOT, [ROOT / 'tools/host_wayland/compositor.cpp', ROOT / 'tools/host_wayland/video.cpp'],
                                     [*FAST_FLAGS, *drm_flags, '-I' + str(protocols)], neo98.tool_environment(),
                                     ROOT / 'build/native-app/object-cache-fast')
            extra += [str(o) for o in protocol_objects]
            cflags += drm_flags
            libs += ['-lwayland-server', '-lEGL', '-lGLESv2', '-lgbm', '-ldrm']
        extra += dos_vm_objects(FAST_FLAGS, ROOT / 'build/native-app/object-cache-fast')
        cflags += ['-DNEO98_WITH_DOSBOX']
        return build_fast('neo98_linux.cpp', name, compile_flags=cflags, link_flags=['-lz', *libs], extra_inputs=extra,
                          replaced=replaced)
    export_contract_assets()
    if source != "neo98_host.cpp" and not runtime_content:
        from serialui_assets import generate as serialui_assets
        serialui_assets()
        neo98.marlett_assets(); neo98.user_resource_assets()
        neo98.win98_assets(); neo98.text_assets(); neo98.user_assets(); neo98.frame_control_assets()
        neo98.dialog_assets(); neo98.registry_assets()
        comparison_assets()
        neo98.message_table_assets()
    if not native_x64():
        # src/x86_cpu_unicorn.cpp includes the pinned Unicorn headers; a
        # fresh checkout has to import them before the first compile.
        from unicorn_dependencies import ensure
        ensure('unicorn')
    from native_build import compile_objects
    out = ROOT / 'build/native-app'
    out.mkdir(exist_ok=True)
    env = neo98.tool_environment()
    # The host reads the registry from the hives of its drive C: at start
    # (tools/host_registry.h) and is built without the image of
    # tools/import_registry.py, so the program holds no installation's
    # registry. A runner with a main of its own keeps the image.
    hives_only = source == 'neo98_host.cpp'
    # Release hosts load COMDLG32 and fonts from the user's installed C:.
    # Standalone comparison runners retain their generated fixtures.
    runtime_content = hives_only or runtime_content
    if runtime_content:
        fast = [*fast, '-DNEO98_RUNTIME_CONTENT']
    arm64_table()
    # replaced: {name of a source of src/: a changed copy of it}, for a host
    # that differs from the usual one the way a later NEO98 would
    # (tools/test_state_host.py, the update test).
    replaced = replaced or {}
    objects = compile_objects(ROOT,
        [fiber_source(),
         *[ROOT / 'src' / f'{s}.cpp' for s in runtime_sources() if not (hives_only and s == 'registry') and f'{s}.cpp' not in replaced]],
        [*fast, *STATE_FLAGS], env, out / cache_name)
    if replaced:
        objects += compile_objects(ROOT, list(replaced.values()), [*fast, *STATE_FLAGS, '-I', str(ROOT / 'src')],
                                   env, out / cache_name)
    if hives_only:
        objects += compile_objects(ROOT, [ROOT / 'src/registry.cpp'], [*fast, *STATE_FLAGS, '-DNEO98_NO_REGISTRY_IMAGE'],
                                   env, out / cache_name)
    cflags = subprocess.check_output(['sdl2-config', '--cflags'], text=True).split()
    libs = subprocess.check_output(['sdl2-config', '--libs'], text=True).split()
    library = unicorn_library()
    metal, frameworks = host_metal_objects(fast, out / cache_name)
    menu, menu_frameworks = host_menu_objects([*fast, *cflags], out / cache_name, host_media)
    cflags += media_flags
    metal += menu
    frameworks += menu_frameworks
    dos_vm = dos_vm_objects(fast, out / cache_name)
    dos_vm += fex_objects(fast, out / cache_name)
    machine, machine_frameworks = arm64_objects(fast, out / cache_name)
    dos_vm += machine
    frameworks += machine_frameworks
    binary = out / name
    temporary = out / f'.{name}.{os.getpid()}'
    link_runner(fast, [*stamp_flags(fast), *cflags], source, [*metal, *dos_vm, library],
                 [*(['-Wl,-rpath,' + str(library.parent)] if library else ['-ldl', '-pthread']), '-lz', *libs, *frameworks,
                 *map(str, objects)], temporary, env)
    if runtime_content:
        release_dependencies(objects, out / cache_name, binary)
    os.replace(temporary, binary)
    if name.startswith('neo98-host'):
        state_layout(binary, cache_name)
    if name == 'neo98-host':
        build_dock_helper(out, env)
    return binary


def wayland_protocols():
    """The server side of xdg-shell and linux-dmabuf for the compositor of
    the KMS host (tools/host_wayland), and the client side for the test
    client: (include folder, server objects, client objects)."""
    out = ROOT / 'build/generated/wayland'
    out.mkdir(parents=True, exist_ok=True)
    env = neo98.tool_environment()
    server, client = [], []
    for xml in sorted((ROOT / 'tools/host_wayland/protocols').glob('*.xml')):
        stem = xml.stem
        for kind, header in (('server', f'{stem}-server-protocol.h'), ('client', f'{stem}-client-protocol.h')):
            target = out / header
            if not target.exists() or target.stat().st_mtime < xml.stat().st_mtime:
                subprocess.run(['wayland-scanner', f'{kind}-header', str(xml), str(target)], check=True)
        code, obj = out / f'{stem}-protocol.c', out / f'{stem}-protocol.o'
        if not obj.exists() or obj.stat().st_mtime < xml.stat().st_mtime:
            subprocess.run(['wayland-scanner', 'private-code', str(xml), str(code)], check=True)
            subprocess.run(['clang', '-O2', '-fPIC', '-c', str(code), '-o', str(obj)], env=env, check=True)
        server.append(obj)
        client.append(obj)
    return out, server, client


def build_kms_host(*, vulkan=True, host_media=None):
    """neo98-kms, the Linux host without X11 or GTK: DRM/KMS, libinput and
    SDL for sound only (tools/neo98_kms.cpp).

    host_media: build host media in (with_host_media), as build_host does."""
    if not native_x64():
        raise RuntimeError('neo98-kms is built for Linux x64')
    from build_host_devices import build as build_hardware_manager
    from native_build import compile_objects
    build_hardware_manager()
    host_media = with_host_media(host_media)
    if host_media:
        from build_host_media import build as build_media_center
        build_media_center()
    sdl_flags = subprocess.check_output(['sdl2-config', '--cflags'], text=True).split()
    sdl_libs = subprocess.check_output(['sdl2-config', '--libs'], text=True).split()
    gtk_flags, _ = linux_gtk_flags()
    drm_flags = subprocess.check_output(['pkg-config', '--cflags', 'libdrm', 'libinput', 'libudev'], text=True).split()
    protocols, protocol_objects, _ = wayland_protocols()
    cflags = [*sdl_flags, *drm_flags]
    libs = [*sdl_libs, '-ldrm', '-linput', '-ludev', '-lwayland-server', '-lEGL', '-lGLESv2', '-lgbm', '-l:libgdk_pixbuf-2.0.so.0', '-lgio-2.0', '-lgobject-2.0', '-lglib-2.0']
    cache = ROOT / 'build/native-app/object-cache-fast'
    if host_media:
        cflags += ['-DNEO98_WITH_HOST_MEDIA']
    extra = compile_objects(ROOT, [ROOT / 'tools/host_devices/linux.cpp', *([ROOT / 'tools/host_media/picture_linux.cpp'] if host_media else [])],
                            [*FAST_FLAGS, *gtk_flags], neo98.tool_environment(), cache)
    extra += compile_objects(ROOT, [ROOT / 'tools/host_wayland/compositor.cpp', ROOT / 'tools/host_wayland/video.cpp'],
                             [*FAST_FLAGS, *drm_flags, '-I' + str(protocols)],
                             neo98.tool_environment(), cache)
    extra += [str(o) for o in protocol_objects]
    if vulkan:
        extra += vulkan_objects()
        cflags += ['-DNEO98_WITH_VULKAN']
        libs += ['-lvulkan']
    extra += dos_vm_objects(FAST_FLAGS, cache)
    cflags += ['-DNEO98_WITH_DOSBOX']
    return build_fast('neo98_kms.cpp', 'neo98-kms', compile_flags=cflags, link_flags=['-lz', *libs], extra_inputs=extra)


def linux_gtk_flags():
    """GTK is part of the GUI host; the CLI never links these libraries."""
    if subprocess.run(['pkg-config', '--exists', 'gtk+-3.0']).returncode == 0:
        return (subprocess.check_output(['pkg-config', '--cflags', 'gtk+-3.0'], text=True).split(),
                subprocess.check_output(['pkg-config', '--libs', 'gtk+-3.0'], text=True).split() + ['-lX11', '-lXext'])
    local = ROOT / 'build/toolchain/usr/include'
    if not (local / 'gtk-3.0/gtk/gtk.h').is_file():
        raise RuntimeError('GTK headers are missing. Install libgtk-3-dev to build the Linux GUI.')
    flags = ['-I' + str(local / name) for name in ('gtk-3.0', 'pango-1.0', 'atk-1.0', 'cairo',
             'gdk-pixbuf-2.0', 'harfbuzz', 'fribidi', 'freetype2', 'pixman-1')]
    flags += subprocess.check_output(['pkg-config', '--cflags', 'glib-2.0', 'gobject-2.0', 'gio-2.0'], text=True).split()
    return flags, ['-l:libgtk-3.so.0', '-l:libgdk-3.so.0', '-l:libgdk_pixbuf-2.0.so.0',
                   '-l:libcairo.so.2', '-lgio-2.0', '-lgobject-2.0', '-lglib-2.0', '-lX11', '-lXext']


def vulkan_objects():
    """Compile the Linux renderer and runtime shader translator."""
    from build_vulkan_shaders import generate
    from native_build import compile_objects
    generate()
    return compile_objects(ROOT, [ROOT / 'tools/host_vulkan.cpp', ROOT / 'tools/vulkan_program.cpp',
                                 ROOT / 'src/d3d_shader_msl.cpp'], FAST_FLAGS,
                           neo98.tool_environment(), ROOT / 'build/native-app/object-cache-fast')


def build_probe(*, vulkan=False):
    """Build the headless native runner used for Linux graphics and app tests."""
    if vulkan:
        if not native_x64():
            raise RuntimeError('The Vulkan probe requires Linux x86_64')
        return build_fast('probe_application.cpp', 'neo98-probe-vulkan',
                          compile_flags=['-DNEO98_WITH_VULKAN'], link_flags=['-lvulkan'],
                          extra_inputs=vulkan_objects())
    return build_fast('probe_application.cpp', 'neo98-probe')


def build_dock_helper(out, env):
    """The Dock icon of a guest program, tools/host_dock_helper.mm: a program
    of its own beside the host, copied into a bundle per guest program at run
    time (tools/host_dock.h)."""
    if sys.platform != 'darwin':
        return None
    source, binary = ROOT / 'tools/host_dock_helper.mm', out / 'neo98-dock-helper'
    if binary.exists() and binary.stat().st_mtime >= source.stat().st_mtime:
        return binary
    temporary = out / f'.neo98-dock-helper.{os.getpid()}'
    subprocess.run(['clang++', '-std=c++17', '-O2', '-fobjc-arc', str(source), '-framework', 'AppKit',
                    '-o', str(temporary)], cwd=ROOT, env=env, check=True)
    os.replace(temporary, binary)
    return binary


def test():
    out=ROOT/'build/native-app';report_path=out/'host-results.json'
    report_path.unlink(missing_ok=True)
    binary=build_test('test_native_app.cpp','test-native-app')
    exe = ROOT / 'build/programs/NOTEPAD.EXE'
    original = hashlib.sha256(exe.read_bytes()).hexdigest()
    # The FNT comes from the same local source as the guest's embedded copy.
    result = subprocess.run([str(binary), str(exe), str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                             str(out / 'notepad.ppm')], capture_output=True, text=True)
    print(result.stdout, end='')
    print(result.stderr, end='', file=sys.stderr)
    result.check_returncode()
    assert hashlib.sha256(exe.read_bytes()).hexdigest() == original
    counters = re.search(r'NATIVE APP PASS.* calls=(\d+) callbacks=(\d+)', result.stdout)
    assert counters
    report = {'exe_sha256': original, 'source_file_unchanged': True, 'sanitizers': ['address', 'undefined'],
              'api_calls': int(counters[1]), 'callbacks': int(counters[2]),
              'word_wrap_toggles': 10, 'original_unsaved_message_box': 'cancelled with Escape and document preserved', 'exit_code': 0, 'remaining_windows': 0, 'remaining_gdi_objects': 0}
    report_path.write_text(json.dumps(report, indent=2) + '\n')
    return report


if __name__ == '__main__':
    test()
    # Guest runners using --objects must see the same class layouts as this build.
    object_directory()
