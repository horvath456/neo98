#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""A forked Linux host can allocate while another thread used the registry."""
from pathlib import Path
import subprocess
import sys
import tempfile

if sys.platform != 'linux':
    print('SKIP: Linux allocation registry and fork')
    sys.exit(77)

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import neo98

SOURCE = r'''
#include "host_state_alloc_linux.cpp"
#include <atomic>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>
int main() {
    std::atomic<bool> held{false};
    std::thread worker([&] {
        pthread_mutex_lock(&lock);
        held = true;
        usleep(100000);
        pthread_mutex_unlock(&lock);
    });
    while (!held) usleep(1000);
    pid_t child = fork();
    if (!child) {
        alarm(2);
        void *(*volatile allocate)(size_t) = malloc;
        void *p = allocate(73);
        bool okay = p && neo98_state_allocation_bytes(p) == 73;
        free(p);
        _exit(okay ? 0 : 1);
    }
    worker.join();
    int status = 0;
    if (child < 0 || waitpid(child, &status, 0) != child) return 2;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0 ? 0 : 3;
}
'''

def main():
    with tempfile.TemporaryDirectory() as folder:
        source = Path(folder) / 'fork.cpp'
        binary = Path(folder) / 'fork'
        source.write_text(SOURCE)
        subprocess.run(['clang++', '-std=c++17', '-O2', '-pthread', '-I',
                        str(Path(__file__).resolve().parent), str(source), '-o', str(binary)],
                       env=neo98.tool_environment(), check=True)
        subprocess.run([str(binary)], timeout=10, check=True)
    print('ok fork preserves allocation registry ownership and permits child allocations')

if __name__ == '__main__':
    main()
