#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Starting and ending processes that may hold a virtual processor.

A host or test runner that runs an ARM64 program holds a virtual processor of
Hypervisor.framework. Ending such a process with SIGKILL has taken the whole
Mac down: the kernel frees the thread and its processor in
the wrong order. So nothing here ever sends SIGKILL. A process is asked to end
with SIGTERM and then waited for; one that does not end is reported and left
alone, for a person to look at.

    run(arguments, timeout=...) like subprocess.run with captured text
    end(process) SIGTERM, then wait
    lock the lock of ARM64 runs (NEO98_ARM64_LOCK)

    arm64_run.py --locked -- command a whole command under that lock
"""
import contextlib
import os
import subprocess
import sys
import time


class Stuck(RuntimeError):
    """A process did not end after SIGTERM. It is still running."""


def ensure_idle():
    """Also refuse hosts started outside the shared worktree test lock.

    Only on macOS, where a host may hold a virtual processor of
    Hypervisor.framework. A Linux host holds none, and the hosts of tests
    that run side by side each lock their own drive C:."""
    if sys.platform != 'darwin':
        return
    active = subprocess.run(['pgrep', '-x', 'neo98-host'], capture_output=True, text=True)
    if active.returncode == 0:
        raise RuntimeError('STOP: another neo98-host is active: ' + active.stdout.strip())
    if active.returncode != 1:
        raise RuntimeError('STOP: cannot check for another neo98-host: ' + active.stderr.strip())


def sync():
    """Before a run that may cost the machine: what is written is on disk."""
    os.sync()


class LockStuck(RuntimeError):
    """The lock of ARM64 runs is older than half an hour; a person decides."""


def lock_path():
    """The directory that serializes ARM64 runs across working trees, from
    NEO98_ARM64_LOCK; empty for none. Two hosts with a virtual processor at
    once are what took the machine down, so agents that work side by
    side name the same path."""
    return os.environ.get('NEO98_ARM64_LOCK', '')


@contextlib.contextmanager
def lock(patience=3600):
    """Holds the lock of ARM64 runs: mkdir takes it, rmdir gives it back, also
    after a failure. Waits while another run holds it. A lock older than
    thirty minutes is reported and left alone."""
    path = lock_path()
    # NEO98_ARM64_LOCK_HELD: a caller further out holds the lock for this
    # whole command (`arm64_run.py --locked -- command`).
    if not path or os.environ.get('NEO98_ARM64_LOCK_HELD') in ('1', path):
        yield
        return
    waited = time.monotonic()
    while True:
        try:
            os.mkdir(path)
            break
        except FileExistsError:
            try:
                age = time.time() - os.stat(path).st_mtime
            except FileNotFoundError:
                continue
            if age > 1800:
                raise LockStuck(f'{path} is {int(age / 60)} minutes old; check that no ARM64 run is alive before removing it') from None
            if time.monotonic() - waited > patience:
                raise LockStuck(f'waited {patience} s for {path}') from None
            time.sleep(0.5)
    try:
        with open(os.path.join(path, 'owner'), 'w') as stream:
            stream.write(f'{os.getpid()} {os.getcwd()}\n')
        yield
    finally:
        with contextlib.suppress(OSError):
            os.unlink(os.path.join(path, 'owner'))
        with contextlib.suppress(OSError):
            os.rmdir(path)


def end(process, patience=120):
    """Asks a process to end and waits. Never kills."""
    if process.poll() is not None:
        return process.returncode
    process.terminate()
    try:
        return process.wait(patience)
    except subprocess.TimeoutExpired:
        raise Stuck(f'process {process.pid} did not end {patience} s after SIGTERM; it is left running, '
                    f'do not kill it with SIGKILL') from None


# The same lock under the name the C++ runtime's tests use.
Lock = lock


def run(arguments, timeout, env=None, cwd=None):
    """Runs to the end with captured output. After `timeout` seconds the
    process is asked to end; the result then has `timed_out` set."""
    with lock():
        ensure_idle()
        sync()
        process = subprocess.Popen(arguments, env=env, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
                                   errors='replace', stdin=subprocess.DEVNULL)
        timed_out = False
        try:
            out, err = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.terminate()
            try:
                out, err = process.communicate(timeout=120)
            except subprocess.TimeoutExpired:
                raise Stuck(f'process {process.pid} did not end 120 s after SIGTERM; it is left running, '
                            f'do not kill it with SIGKILL') from None
    result = subprocess.CompletedProcess(arguments, process.returncode, out, err)
    result.timed_out = timed_out
    return result


def main():
    """`arm64_run.py --locked -- command...` runs a command with the lock of
    ARM64 runs held, for a test that starts its host itself."""
    import sys
    arguments = sys.argv[1:]
    if arguments[:2] != ['--locked', '--'] or len(arguments) < 3:
        print('usage: arm64_run.py --locked -- command [arguments]', file=sys.stderr)
        return 2
    with lock():
        return subprocess.call(arguments[2:], env=dict(os.environ, NEO98_ARM64_LOCK_HELD='1'))


if __name__ == '__main__':
    raise SystemExit(main())
