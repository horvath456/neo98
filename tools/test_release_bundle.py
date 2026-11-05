#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Release packaging must exclude Media Center, even in a developer's shell."""
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

import check_release_bundle as bundle
import linux_app
import mac_app
import make_release_assets as assets
import native_app


class ReleaseBundleTest(unittest.TestCase):
    def test_media_resources_and_stripped_code(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for prefix in ('Contents/Resources', 'usr/share/neo98'):
                folder = root / prefix
                folder.mkdir(parents=True)
                for name in ('MEDIACTR.EXE', 'mediactr.lnk', 'service.neo98'):
                    with self.subTest(prefix=prefix, name=name):
                        path = folder / name
                        if name == 'service.neo98':
                            path.mkdir()  # Even an empty add-on directory is forbidden.
                        else:
                            path.write_bytes(b'resource')
                        self.assertTrue(any('Media Center resource' in e for e in bundle.check(root)))
                        path.rmdir() if path.is_dir() else path.unlink()
            host = root / 'host'
            host.write_bytes(b'\0' + bundle.HOST_MEDIA_MARKER + b'state=%u %s\n\0')
            self.assertEqual(bundle.check(root), ['host: host-media code in release'])
            host.write_bytes(b'\0ordinary host\0')
            (root / 'THIRD_PARTY.md').write_text('Media Center uses Kodi in optional builds.')
            self.assertEqual(bundle.check(root), [])

    def test_local_build_flag_still_works(self):
        for value, expected in (('0', False), ('1', True)):
            with patch.dict(os.environ, NEO98_WITH_HOST_MEDIA=value):
                self.assertEqual(native_app.with_host_media(), expected)
                self.assertTrue(native_app.with_host_media(True))
                self.assertFalse(native_app.with_host_media(False))

    def test_macos_release_overrides_environment_and_rejects_stale_host(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            app = root / 'built.app'
            (app / 'Contents/Frameworks').mkdir(parents=True)
            host = app / 'Contents/MacOS/Neo98'
            host.parent.mkdir()
            manifest = root / 'build/native-app/neo98-host.dependencies.json'
            manifest.parent.mkdir(parents=True)
            manifest.write_text('{}')
            dmg = root / 'test.dmg'
            dmg.write_bytes(b'dmg')

            def build():
                self.assertFalse(native_app.with_host_media())
                return app

            for contaminated in (False, True):
                host.write_bytes(bundle.HOST_MEDIA_MARKER if contaminated else b'host')
                output = root / str(contaminated)
                with patch.dict(os.environ, NEO98_WITH_HOST_MEDIA='1'), \
                     patch.object(sys, 'argv', ['make_release_assets.py', '--output', str(output)]), \
                     patch.object(sys, 'platform', 'darwin'), patch.object(assets, 'ROOT', root), \
                     patch.object(mac_app, 'build_app', side_effect=build), \
                     patch.object(mac_app, 'build_dmg', return_value=dmg) as disk_image, \
                     patch('check_release_embedded.check_dependencies', return_value=[]), \
                     patch.object(assets, 'source_assets'):
                    if contaminated:
                        with self.assertRaisesRegex(RuntimeError, 'host-media code in release'):
                            assets.main()
                        disk_image.assert_not_called()
                    else:
                        assets.main()
                        self.assertTrue((output / 'Neo98.dmg').is_file())

    def test_linux_release_and_no_build_reject_stale_host(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in linux_app.SETUP_FILES + ('linux_desktop_install.py',):
                path = root / 'tools' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('')
            for name in ('tools/freedos_boot', 'LICENSES'):
                (root / name).mkdir()
            for name in ('COPYING', 'THIRD_PARTY.md', 'docs/FREEDOS-BOOT.md',
                         'third_party/dosbox-pure/LICENSE', 'core.so'):
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('')
            for name in ('NEO98HW.EXE', 'NEO98CAP.DRV', 'NEO98FF.DLL', 'CAMERA.EXE', 'CAMERA.LNK'):
                path = root / 'build/host-devices' / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'device')
            toolkit = root / 'build/setup-toolkit'
            (toolkit / 'bin').mkdir(parents=True)
            (toolkit / 'manifest.json').write_text('{}')
            binaries = root / 'build/native-app'
            binaries.mkdir()
            for name in ('neo98-host', 'neo98-cli'):
                (binaries / name).write_bytes(b'host')
                (binaries / (name + '.dependencies.json')).write_text('{}')

            def build(**kwargs):
                self.assertFalse(native_app.with_host_media())

            with patch.object(linux_app, 'ROOT', root), \
                 patch.object(linux_app, 'native_x64', return_value=True), \
                 patch.object(linux_app, 'core_dylib', return_value=root / 'core.so'), \
                 patch.object(linux_app, 'build_host', side_effect=build) as host_build, \
                 patch.object(linux_app, 'build_fast'), \
                 patch.object(linux_app.subprocess, 'run'), \
                 patch('check_release_embedded.check_dependencies', return_value=[]):
                with patch.dict(os.environ, NEO98_WITH_HOST_MEDIA='1'):
                    out = linux_app.package(release=True)
                host_build.assert_called_once()
                self.assertEqual(bundle.check_host_media(out), [])
                archive = root / 'build/Neo98-linux-x64.tar.gz'
                archive.unlink()
                (binaries / 'neo98-host').write_bytes(bundle.HOST_MEDIA_MARKER)
                with patch.dict(os.environ, NEO98_WITH_HOST_MEDIA='1'):
                    with self.assertRaisesRegex(RuntimeError, 'host-media code in release'):
                        linux_app.package(no_build=True, release=True)
                self.assertFalse(archive.exists())
                host_build.assert_called_once()


if __name__ == '__main__':
    unittest.main()
