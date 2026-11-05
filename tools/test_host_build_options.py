#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check target selection and forwarding before any build or asset import."""
import argparse
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import neo98
from tools import host_build_options as options


class HostBuildOptions(unittest.TestCase):
    def setUp(self):
        self.environment = patch.dict(os.environ, {}, clear=True)
        self.environment.start()
        self.addCleanup(self.environment.stop)

    def host(self, system, machine):
        system_patch = patch.object(options.sys, 'platform', system)
        machine_patch = patch.object(options.platform, 'machine', return_value=machine)
        system_patch.start()
        machine_patch.start()
        self.addCleanup(system_patch.stop)
        self.addCleanup(machine_patch.stop)

    def test_linux_defaults(self):
        self.host('linux', 'x86_64')
        self.assertEqual(options.configure(), ('linux-x64', 'vulkan'))
        self.assertEqual(os.environ['NEO98_WITH_VULKAN'], '1')

    def test_mac_defaults(self):
        self.host('darwin', 'arm64')
        self.assertEqual(options.configure(), ('macos-arm64', 'metal'))
        self.assertNotIn('NEO98_WITH_VULKAN', os.environ)

    def test_explicit_gpu_overrides_environment(self):
        self.host('linux', 'x86_64')
        os.environ['NEO98_GPU'] = 'vulkan'
        self.assertEqual(options.configure('linux-x64', 'none'), ('linux-x64', 'none'))
        self.assertEqual(os.environ['NEO98_WITH_VULKAN'], '0')

    def test_auto_gpu_uses_environment(self):
        self.host('linux', 'x86_64')
        os.environ['NEO98_GPU'] = 'none'
        self.assertEqual(options.resolve(), ('linux-x64', 'none'))

    def test_cross_target_rejected(self):
        self.host('linux', 'x86_64')
        with self.assertRaisesRegex(RuntimeError, 'cross compilation'):
            options.configure('macos-arm64')
        self.assertNotIn('NEO98_GPU', os.environ)

    def test_gpu_for_wrong_target_rejected(self):
        self.host('darwin', 'arm64')
        with self.assertRaisesRegex(RuntimeError, 'unavailable for macos-arm64'):
            options.resolve(gpu='vulkan')

    def test_cli_ignores_gpu_environment(self):
        self.host('linux', 'x86_64')
        os.environ['NEO98_GPU'] = 'vulkan'
        self.assertEqual(options.configure(cli=True), ('linux-x64', 'none'))
        self.assertEqual(os.environ['NEO98_WITH_VULKAN'], '0')

    def test_cli_explicit_gpu_rejected(self):
        self.host('linux', 'x86_64')
        with self.assertRaisesRegex(RuntimeError, 'CLI build has no GPU'):
            options.resolve(gpu='vulkan', cli=True)

    def test_unsupported_architecture_rejected(self):
        self.host('linux', 'aarch64')
        with self.assertRaisesRegex(RuntimeError, 'macOS ARM64 or Linux x86_64'):
            options.resolve()

    def test_build_only_forwards_options(self):
        args = argparse.Namespace(target='linux-x64', gpu='none')
        with patch.object(neo98, 'analysis_python', return_value=Path('/python')):
            command = neo98.host_command(args, build_only=True)
        self.assertEqual(command[-5:], ['--target', 'linux-x64', '--gpu', 'none', '--build-only'])

    def test_cli_build_uses_analysis_python(self):
        self.host('linux', 'x86_64')
        args = argparse.Namespace(command='build-cli', no_build=False)
        with patch.object(neo98, 'analysis_python', return_value=Path('/analysis/bin/python')), \
                patch.object(neo98.subprocess, 'run') as run:
            self.assertEqual(neo98.cli_command(args), 0)
        self.assertEqual(run.call_args.args[0][:2], ['/analysis/bin/python', '-c'])
        self.assertTrue(run.call_args.kwargs['check'])


if __name__ == '__main__':
    unittest.main()
