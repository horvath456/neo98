# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Incremental clang++ objects for native_app.build_test; linking stays with caller.

compile_objects(root, sources_paths, flags, env, cache_dir) returns object Paths
in source order. Flags contain compilation options, not sources or link outputs.
Clang -MD records transitive dependencies, including generated/system headers.
Reuse requires matching compiler/options/environment and content hashes of every
reported dependency and the object. At most four compilers run concurrently.
Manifests commit atomically after successful compilation; objects have content
addresses so concurrent links retain their files. The caller owns cache cleanup.
Only artifacts/manifests are stored; no source/build trees are copied or scanned.
"""
from concurrent.futures import ThreadPoolExecutor
from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import time


def _hash(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _identity(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def _absolute(path, root):
    path = Path(path)
    # Keep dependency symlink names: retargeting a header must invalidate reuse.
    return Path(os.path.abspath(path if path.is_absolute() else root / path))


def _dependencies(path, root):
    """Read clang's single -MT rule, including Make escapes and continuations."""
    text = Path(path).read_text().replace("\\\r\n", "").replace("\\\n", "")
    target, colon, text = text.partition(":")
    if not colon or target.strip() != "native_cache_object":
        raise ValueError("Unexpected clang dependency rule")
    tokens, token, index = [], [], 0
    while index < len(text):
        char = text[index]
        if char == "\\" and index + 1 < len(text):
            index += 1
            token.append(text[index])
        elif char == "$" and text[index:index + 2] == "$$":
            token.append("$")
            index += 1
        elif char.isspace():
            if token:
                tokens.append("".join(token))
                token = []
        else:
            token.append(char)
        index += 1
    if token:
        tokens.append("".join(token))
    if not tokens:
        raise ValueError("Clang reported no dependencies")
    return {_absolute(token, root) for token in tokens}


def _response_files(flags, root):
    result = {}
    def visit(arguments):
        for argument in arguments:
            if not argument.startswith("@"):
                continue
            path = _absolute(argument[1:], root)
            if str(path) in result:
                continue
            result[str(path)] = _hash(path)
            visit(shlex.split(path.read_text(), posix=True))
    visit(flags)
    return result


def _compiler(env, root):
    executable = shutil.which("clang++", path=env.get("PATH", os.defpath))
    if not executable:
        raise FileNotFoundError("clang++ is not on the build PATH")
    executable = str(_absolute(executable, root))
    def query(option):
        return subprocess.check_output([executable, option], cwd=root, env=env, text=True).strip()
    version, driver, resources = query("--version"), query("-print-prog-name=clang"), query("-print-resource-dir")
    driver_path = Path(driver) if Path(driver).is_absolute() else Path(shutil.which(driver, path=env.get("PATH", os.defpath)) or executable)
    return executable, {"executable": executable, "sha256": _hash(executable),
                        "driver": str(driver_path), "driver_sha256": _hash(driver_path),
                        "version": version, "resources": resources}


@contextmanager
def locked(path):
    """Exclusive lock on `path`; other processes wait instead of interleaving."""
    with path.open("a+b") as stream:
        fcntl.flock(stream, fcntl.LOCK_EX)
        try:
            yield
        finally:
            fcntl.flock(stream, fcntl.LOCK_UN)


# The cache entry behind each object that compile_objects returned in this
# process, by object path. Two builds can produce byte-identical objects from
# different dependency sets (an include that changes nothing); a release check
# must read the entries of its own build, not every entry with those bytes.
SELECTED_ENTRIES = {}


def _select(artifact, entry):
    SELECTED_ENTRIES.setdefault(str(artifact), set()).add(str(entry))


def compile_objects(root, sources_paths, flags, env, cache_dir):
    root = _absolute(root, Path.cwd())
    cache = _absolute(cache_dir, root)
    sources = [_absolute(path, root) for path in sources_paths]
    flags = [str(flag) for flag in flags]
    environment = {str(key): str(value) for key, value in (os.environ if env is None else env).items()}
    if not sources:
        return []
    compiler, compiler_info = _compiler(environment, root)
    state_build = '--neo98-state-layout' in flags
    compile_flags = [flag for flag in flags if flag != '--neo98-state-layout']
    pipeline = None
    optimizer = Path(compiler).parent / 'opt'
    if state_build:
        pipeline = subprocess.check_output([str(optimizer), '-passes=default<O2>', '-print-pipeline-passes',
                                            '-disable-output', '/dev/null'], text=True, env=environment).strip()
        pipeline = ','.join(part for part in pipeline.split(',') if part != 'globalopt')
    response_files = _response_files(flags, root)
    configuration = {"schema": 1, "root": str(root), "compiler": compiler_info,
                     "flags": flags, "environment": environment, "response_files": response_files}
    if state_build: configuration["state_pipeline"] = pipeline
    options = _identity(configuration)
    for name in ("objects", "entries", "locks", "temporary"):
        (cache / name).mkdir(parents=True, exist_ok=True)

    def build(source):
        key = _identity({"root": str(root), "source": str(source), "options": options})
        entry = cache / "entries" / (key + ".json")
        with locked(cache / "locks" / (key + ".lock")):
            try:
                record = json.loads(entry.read_text())
                object_hash = record["object_sha256"]
                if len(object_hash) != 64 or any(c not in "0123456789abcdef" for c in object_hash):
                    raise ValueError("Invalid cached object identity")
                artifact = cache / "objects" / (object_hash + ".o")
                deps = record["dependencies"]
                if (record["options"] == options and record["source"] == str(source)
                        and str(source) in deps and _hash(artifact) == object_hash
                        and all(_hash(path) == value for path, value in deps.items())):
                    _select(artifact, entry)
                    return artifact, False
            except (OSError, ValueError, KeyError, TypeError, AttributeError):
                pass

            with tempfile.TemporaryDirectory(prefix=key[:12] + "-", dir=cache / "temporary") as directory:
                temporary = Path(directory)
                obj, dep = temporary / "object.o", temporary / "object.d"
                for attempt in range(2):
                    started = time.time_ns()
                    if state_build and source.suffix == '.cpp':
                        raw, optimized = temporary / 'raw.bc', temporary / 'optimized.bc'
                        subprocess.run([compiler, *compile_flags, '-emit-llvm', '-Xclang', '-disable-llvm-passes',
                            '-c', str(source), '-MD', '-MF', str(dep), '-MT', 'native_cache_object', '-o', str(raw)],
                            cwd=root, env=environment, check=True)
                        subprocess.run([str(optimizer), '-passes=' + pipeline, str(raw), '-o', str(optimized)],
                            cwd=root, env=environment, check=True)
                        subprocess.run([compiler, *compile_flags, '-Xclang', '-disable-llvm-passes',
                            '-c', str(optimized), '-o', str(obj)], cwd=root, env=environment, check=True)
                    else:
                        subprocess.run([compiler, *compile_flags, "-c", str(source), "-MD", "-MF", str(dep),
                                        "-MT", "native_cache_object", "-o", str(obj)],
                                       cwd=root, env=environment, check=True)
                    paths = _dependencies(dep, root) | {source} | {Path(path) for path in response_files}
                    dependencies = {str(path): _hash(path) for path in sorted(paths)}
                    # Headers first discovered by -MD may have changed during the
                    # compile. Retry once; never associate that object with newer bytes.
                    unstable = any(max(path.stat().st_mtime_ns, path.stat().st_ctime_ns,
                                       path.lstat().st_mtime_ns, path.lstat().st_ctime_ns) >= started
                                   for path in paths)
                    if not unstable:
                        break
                    if attempt:
                        raise RuntimeError("Dependencies changed during compilation: " + str(source))
                object_hash = _hash(obj)
                artifact = cache / "objects" / (object_hash + ".o")
                # The manifest is the commit record. A failed compiler never
                # reaches publication; an interrupted publish causes a cache miss.
                os.replace(obj, artifact)
                record = {"source": str(source), "options": options,
                          "object_sha256": object_hash, "dependencies": dependencies}
                manifest = temporary / "entry.json"
                manifest.write_text(json.dumps(record, sort_keys=True) + "\n")
                os.replace(manifest, entry)
                _select(artifact, entry)
                return artifact, True

    jobs = int(os.environ.get("NEO98_NATIVE_JOBS", "4"))
    with ThreadPoolExecutor(max_workers=max(1, min(jobs, len(sources)))) as workers:
        results = list(workers.map(build, sources))
    compiled = sum(built for _, built in results)
    print(f"Native objects: {compiled} compiled, {len(results) - compiled} reused")
    return [artifact for artifact, _ in results]


BRIDGE_FLAGS = ["-std=c++17", "-O1", *(["-dynamiclib"] if sys.platform == "darwin" else ["-shared", "-fPIC"])]


def build_bridge(source, output, *, flags=(), root=None):
    """Compile one original-comparison bridge into a loadable library.

    Tests that compare a implementation against original code load a small
    `tools/*_bridge.cpp` through ctypes. Each such test builds its own library
    so that it starts on its own, without a preceding bundle run. The link goes
    to a private name and is moved into place, so two tests that want the same
    bridge at the same time never read a half-written file.
    """
    source, output = Path(source), Path(output)
    root = Path(root) if root else Path(__file__).resolve().parents[1]
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.parent / f".{output.name}.{os.getpid()}"
    subprocess.run(["clang++", *BRIDGE_FLAGS, *flags, str(source), "-o", str(temporary)],
                   cwd=root, check=True)
    os.replace(temporary, output)
    shutil.rmtree(str(temporary) + ".dSYM", ignore_errors=True)
    return output


# The USER drawing implementation and what it needs of GDI, the sources
# neo98.frame_control_assets links the frame control bridge with.
USER_DRAW_SOURCES = ("user_draw", "gdi", "dib_text", "dibeng", "win98_rect")


def build_user_draw_bridge(source, output):
    """build_bridge for a bridge that calls the USER drawing code of src/
    (Win98Draw::DrawEdge, ButtonFrame, the frame control cells). Tests build
    it themselves instead of loading a library an earlier run left."""
    root = Path(__file__).resolve().parents[1]
    return build_bridge(source, output, flags=["-DUSER_DRAW_NO_VECTORS",
                        *[str(root / "src" / f"{name}.cpp") for name in USER_DRAW_SOURCES]])


def load_bridge(output, bridge, *sources, flags=()):
    """Build `tools/<bridge>` with further tree-relative `sources` into
    `output` now and load it through ctypes.

    For the standalone original-comparison tests: they load the library they
    just built from the current sources, never one a bundle run left behind
    (a stale library answers with old rules, a missing one ends the test
    before it starts)."""
    import ctypes
    root = Path(__file__).resolve().parents[1]
    extra = [str(root / source) for source in sources]
    # A component script run on a source overlay (its --source) names the
    # overlay's src here, so the bridge it compares sees the same headers.
    overlay = os.environ.get("NEO98_BRIDGE_SOURCE")
    includes = ["-I" + overlay] if overlay else []
    try:
        built = build_bridge(root / "tools" / bridge, output,
                             flags=[*includes, "-I" + str(root / "src"), *flags, *extra])
    except subprocess.CalledProcessError as error:
        raise SystemExit(f"Building {output} from tools/{bridge} failed "
                         f"(exit {error.returncode}); see the compiler output above.")
    return ctypes.CDLL(str(built))


def original_input(target):
    """Copy an original Win98 file the tests compare against into `target`
    (build/gdi-audit/USER.EXE and similar) when it is missing. The file comes
    out of the pristine drive C: by its name under WINDOWS\\SYSTEM, WINDOWS or
    WINDOWS\\FONTS, as tools/gui_inventory.py fetches it; the folder is only
    read."""
    target = Path(target)
    if target.is_file():
        return target
    import win98_c
    win98_c.use_pristine()
    for folder in ("::/WINDOWS/SYSTEM/", "::/WINDOWS/", "::/WINDOWS/FONTS/"):
        if win98_c.path(folder + target.name) is not None:
            return win98_c.copy(folder + target.name, target)
    raise SystemExit(f"{target.name} is not in drive C: ({win98_c.FOLDER}); "
                     "run `python3 neo98.py setup` first.")
