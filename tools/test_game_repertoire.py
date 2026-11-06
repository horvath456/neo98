#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Safety and result semantics for the downloaded-game runner; no guest execution."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import shutil
import subprocess
import unittest
from unittest.mock import Mock, patch
import zipfile

spec = importlib.util.spec_from_file_location('repertoire', Path(__file__).with_name('game_repertoire.py'))
r = importlib.util.module_from_spec(spec)
spec.loader.exec_module(r)


class RepertoireTests(unittest.TestCase):
    def test_stream_classification_bounds_memory_and_keeps_priority(self):
        import tracemalloc
        chunk = 'x' * (64 * 1024)
        def trace():
            yield 'CRASHREPORT '
            for _ in range(40960):  # 2.5 GiB, including one unbounded line
                yield chunk
            yield 'unhandled-guest-exception\n'
        tracemalloc.start()
        try:
            self.assertEqual(r.classify(0, trace(), Path('/missing'), 'game', False)[0], 'crash')
            self.assertLess(tracemalloc.get_traced_memory()[1], 2 * 1024**2)
        finally:
            tracemalloc.stop()
        self.assertEqual(r.classify(1, iter(['Unhandled guest excep', 'tion\n',
            'NEO98 HOST dead', 'line 20 s reached\n']), Path('/missing'), 'game', False)[0], 'hang-suspected')

    def test_stream_deadline_with_unbounded_number_and_partial_suffix(self):
        chunks = ['NEO98 HOST deadline ', '9'*65536, '9'*65536, ' s rea', 'ched\n']
        self.assertEqual(r.classify(0, iter(chunks), Path('/missing'), 'game', False)[0], 'hang-suspected')
        self.assertEqual(r.classify(0, 'unhandled-guest-exception CRASHREPORT path\n',
            Path('/missing'), 'game', False)[0], 'no-image')

    def test_post_host_reserve_is_checked_between_bounded_reads(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)/'host.log'
            path.write_text('x' * 200000)
            with patch.object(r.shutil, 'disk_usage', side_effect=[Mock(free=9*r.GIB), Mock(free=5*r.GIB)]):
                stream = r.log_fragments(path, Path(folder))
                self.assertEqual(len(next(stream)), 64*1024)
                with self.assertRaisesRegex(RuntimeError, 'result processing'):
                    next(stream)

    def test_cached_payload_links_cannot_expose_host_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder); payload=root/'payload'; payload.mkdir()
            (payload/'game.exe').write_bytes(b'game')
            r.validate_payload(payload)
            (payload/'outside').symlink_to(root, target_is_directory=True)
            with self.assertRaises(ValueError):
                r.validate_payload(payload)

    def test_real_play_runner_reuses_original_install_path_and_registry(self):
        # The fixture's own build/win98-c, not a folder the caller configured.
        with tempfile.TemporaryDirectory() as folder, patch.dict(os.environ):
            os.environ.pop('NEO98_WIN98_ROOT', None)
            root=Path(folder); corpus=root/'corpus'; work=root/'work'
            payload=corpus/'prepared/game'; payload.mkdir(parents=True)
            (payload/'setup.exe').write_bytes(b'installer')
            (root/'build/win98-c/WINDOWS').mkdir(parents=True)
            (root/'build/win98-c/old.exe').write_bytes(b'pre-existing game')
            (root/'build/win98-c.registry').write_bytes(b'original registry')
            (root/'build/native-app').mkdir()
            (root/'build/native-app/neo98-host').write_bytes(b'fake host')
            commands=[]
            def copy_or_pgrep(command, **kwargs):
                if command[0]=='pgrep':
                    return subprocess.CompletedProcess(command,1,stdout='')
                self.assertEqual(command[:3],['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R'])
                shutil.copytree(command[-2],command[-1])
                return subprocess.CompletedProcess(command,0)
            def host(command, **kwargs):
                commands.append(command)
                drive=Path(command[command.index('--drive-c')+1])
                registry=Path(command[command.index('--registry')+1])
                if len(commands)==1:
                    disc=Path(command[command.index('--cdrom')+1])
                    self.assertEqual(disc, work/'runs/proof/game/installer/game')
                    self.assertNotEqual(disc, payload)
                    self.assertEqual((disc/'setup.exe').read_bytes(), b'installer')
                    (drive/'Games/Test').mkdir(parents=True)
                    (drive/'Games/Test/game.exe').write_bytes(b'game')
                    registry.write_bytes(b'installed registry')
                else:
                    self.assertNotIn('--cdrom', command)
                    self.assertEqual(command[command.index('--exe')+1], 'C:\\Games\\Test\\game.exe')
                    self.assertEqual(registry.read_bytes(),b'installed registry')
                return Mock(poll=Mock(return_value=0),returncode=0)
            game={'id':'game','sha256':'0'*64,
                  'install':{'exe':'E:\\setup.exe','discover_exe':['game.exe'],'cdrom':True},
                  'launch':{'exe':'E:\\game.exe'}}
            with patch.object(r,'ROOT',root), patch.object(r,'guard'), \
                 patch.object(r,'prepare',return_value=payload), \
                 patch.object(r.subprocess,'run',side_effect=copy_or_pgrep), \
                 patch.object(r.subprocess,'check_output',return_value='revision'), \
                 patch.object(r.subprocess,'Popen',side_effect=host):
                result=r.play(corpus,game,'proof',5,work)
            self.assertEqual(result['phase'],'game')
            self.assertEqual(len(commands),2)
            installer=json.loads((work/'runs/proof/game/installer/result.json').read_text())
            self.assertEqual(installer['new_executables'], ['C:\\Games\\Test\\game.exe'])
            self.assertEqual((root/'build/win98-c.registry').read_bytes(),b'original registry')
            self.assertFalse((root/'build/win98-c/Games').exists())
            self.assertEqual([p.name for p in payload.iterdir()],['setup.exe'])
            self.assertFalse((work/'runs/proof/game/installer/c').exists())
            self.assertFalse((work/'runs/proof/game/installer/game').exists())

    def test_reference_source_never_uses_live_tree_or_journal(self):
        for journal in (None, b'reference journal'):
            with self.subTest(journal=journal), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                source = root/'reference-c'
                source.mkdir()
                (source/'SYSTEM.DAT').write_bytes(b'reference hive')
                if journal is not None:
                    source.with_suffix('.registry').write_bytes(journal)
                payload = root/'payload'
                payload.mkdir()
                (payload/'game.exe').write_bytes(b'game')
                (root/'build/native-app').mkdir(parents=True)
                (root/'build/native-app/neo98-host').write_bytes(b'fake host')
                copied = []
                def copy(command, **kwargs):
                    if command[0] == 'pgrep':
                        return subprocess.CompletedProcess(command, 1, stdout='')
                    copied.append(Path(command[-2]))
                    shutil.copytree(command[-2], command[-1])
                    return subprocess.CompletedProcess(command, 0)
                def host(command, **kwargs):
                    drive = Path(command[command.index('--drive-c')+1])
                    registry = Path(command[command.index('--registry')+1])
                    self.assertEqual((drive/'SYSTEM.DAT').read_bytes(), b'reference hive')
                    self.assertEqual(registry.read_bytes(), journal or b'')
                    return Mock(poll=Mock(return_value=0), returncode=0)
                game = {'id':'game', 'sha256':'0'*64, 'launch':{'exe':r'E:\game.exe'}}
                with patch.object(r, 'ROOT', root), patch.object(r, 'guard'), \
                     patch.dict(os.environ, NEO98_WIN98_ROOT=str(source)), \
                     patch.object(r, 'prepare', return_value=payload), \
                     patch.object(r.subprocess, 'run', side_effect=copy), \
                     patch.object(r.subprocess, 'check_output', return_value='revision'), \
                     patch.object(r.subprocess, 'Popen', side_effect=host):
                    r.run(root/'corpus', game, 'reference', 5, 'game', root/'work')
                self.assertEqual(copied, [source.resolve(), payload])
                self.assertFalse((root/'build/win98-c').exists())
                self.assertFalse((root/'build/win98-c.registry').exists())

    def test_play_preserves_session_and_removes_both_drives(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            calls = []
            def phase(home, game, label, seconds, name, work, session):
                calls.append((name, seconds, id(session)))
                if name == 'installer':
                    for key in ('drive', 'writable'):
                        session[key] = root/key
                        session[key].mkdir()
                    session['registry'] = root/'registry.bin'
                    session['registry'].write_bytes(b'installer registry')
                    session['game_exe'] = 'C:\\Games\\game.exe'
                    return {'status': 'installation-pending'}
                self.assertEqual(session['registry'].read_bytes(), b'installer registry')
                self.assertEqual(session['game_exe'], 'C:\\Games\\game.exe')
                return {'status': 'image-needs-review'}
            game = {'install': {'seconds': 80}, 'launch': {'seconds': 40}}
            with patch.object(r, 'run', side_effect=phase):
                self.assertEqual(r.play(root, game, 'test', None, root)['status'], 'image-needs-review')
            self.assertEqual([(n,s) for n,s,_ in calls], [('installer',80),('game',40)])
            self.assertEqual(calls[0][2], calls[1][2])
            self.assertFalse((root/'drive').exists())
            self.assertFalse((root/'writable').exists())

    def test_play_never_launches_after_missing_exe_or_installer_crash(self):
        for status in ('installation-pending', 'crash', 'hang-suspected'):
            with patch.object(r, 'run', return_value={'status':status}) as run:
                r.play(Path('/source'), {'install': {}, 'launch': {}}, 'test', 30, Path('/work'))
            self.assertEqual(run.call_count, 1)

    def test_play_cleans_on_failure_but_preserves_live_host_files(self):
        for alive in (False, True):
            with self.subTest(alive=alive), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                def failed(*args):
                    session = args[-1]
                    session['drive'] = root/'c'
                    session['drive'].mkdir()
                    session['process'] = Mock()
                    session['process'].poll.return_value = None if alive else 0
                    raise RuntimeError('STOP: host failed')
                with patch.object(r, 'run', side_effect=failed), self.assertRaises(RuntimeError):
                    r.play(root, {'install': {}, 'launch': {}}, 'test', 30, root)
                self.assertEqual((root/'c').exists(), alive)

    def test_disk_floor_terminates_only_own_host_without_sigkill(self):
        process = Mock()
        process.poll.return_value = None
        with patch.object(r.shutil, 'disk_usage', return_value=Mock(free=5*r.GIB)):
            with self.assertRaisesRegex(RuntimeError, 'fewer than 6 GiB'):
                r.wait_host(process, 30, Path('/work'))
        process.terminate.assert_called_once_with()
        process.wait.assert_called_once_with(timeout=20)
        process.kill.assert_not_called()

    def test_prepare_never_writes_read_only_corpus(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder); home=root/'source'; work=root/'work'
            home.mkdir(); work.mkdir()
            source=home/'original.exe'; source.write_bytes(b'original')
            game={'id':'game','filename':'original.exe','install':{'method':'guest'}}
            with patch.object(r, 'verify', return_value=source), patch.object(r, 'guard'):
                target=r.prepare(home,game,work)
            self.assertEqual(target,work/'prepared/game')
            self.assertEqual((target/'original.exe').read_bytes(),b'original')
            self.assertEqual([p.name for p in home.iterdir()],['original.exe'])

    def test_archive_paths_cannot_escape(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name in ('../outside', '/outside', 'C:/outside', 'a\\b'):
                with self.subTest(name=name), self.assertRaises(ValueError):
                    r.inside(root, name)
            (root/'link').symlink_to(root.parent, target_is_directory=True)
            with self.assertRaises(ValueError):
                r.inside(root, 'link/outside')

    def test_zip_traversal_rejected_before_writing(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            source = root/'bad.zip'
            with zipfile.ZipFile(source, 'w') as archive:
                archive.writestr('../outside', b'unsafe')
            with self.assertRaises(ValueError):
                r.extract(source, root/'output', root)
            self.assertFalse((root/'output').exists())
            self.assertFalse((root/'outside').exists())

    def test_hash_mismatch_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            game={'id':'test','filename':'game.zip','sha256':'0'*64}
            path=r.archive_path(root,game)
            path.parent.mkdir(parents=True)
            path.write_bytes(b'corrupt')
            with self.assertRaises(ValueError): r.verify(root,game)

    def test_rooted_cab_members_are_validated_before_extraction(self):
        for kind, name, valid in [('Cab', '/Setup.exe', True),
                                  ('Cab', '/../outside', False),
                                  ('Cab', '/C:/outside', False),
                                  ('7z', '/Setup.exe', False)]:
            with self.subTest(kind=kind, name=name), tempfile.TemporaryDirectory() as folder:
                root = Path(folder)
                source = root/'sfx.exe'; source.write_bytes(b'package')
                listing = f'Type = {kind}\n----------\nPath = {name}\nSize = 5\n'
                def command(args, **kwargs):
                    if args[1] == 'l':
                        return subprocess.CompletedProcess(args, 0, stdout=listing)
                    self.assertNotIn('-spf', args)
                    (root/'out/Setup.exe').write_bytes(b'setup')
                    return subprocess.CompletedProcess(args, 0)
                with patch.object(r, 'guard'), patch.object(r.subprocess, 'run', side_effect=command):
                    if valid:
                        r.extract(source, root/'out', root, rooted_cab=True)
                        self.assertEqual((root/'out/Setup.exe').read_bytes(), b'setup')
                    else:
                        with self.assertRaises(ValueError):
                            r.extract(source, root/'out', root, rooted_cab=True)
                        self.assertFalse((root/'out').exists())

    def test_cab_recipe_does_not_reuse_guest_executable_cache(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); home = root/'corpus'; work = root/'work'
            (home/'prepared/game').mkdir(parents=True)
            (home/'prepared/game/sfx.exe').write_bytes(b'original')
            game = {'id':'game', 'install':{'method':'cab'}}
            with patch.object(r, 'verify', return_value=home/'sfx.exe'), patch.object(r, 'extract') as unpack:
                self.assertEqual(r.prepare(home, game, work), work/'prepared-cab/game')
            unpack.assert_called_once_with(home/'sfx.exe', work/'prepared-cab/game', work, rooted_cab=True)
            self.assertEqual((home/'prepared/game/sfx.exe').read_bytes(), b'original')

    def test_installer_and_timeout_are_not_game_success(self):
        self.assertEqual(r.classify(0,'',Path('/missing'),'installer',False)[0],'installation-pending')
        self.assertEqual(r.classify(0,'',Path('/missing'),'game',True)[0],'hang-suspected')
        self.assertEqual(r.classify(0,'NEO98 HOST deadline 35 s reached',Path('/missing'),'game',False)[0],'hang-suspected')
        self.assertEqual(r.classify(-11,'',Path('/missing'),'game',False)[0],'crash')
        self.assertEqual(r.classify(0,'SetUnhandledExceptionFilter',Path('/missing'),'game',False)[0],'no-image')
        self.assertEqual(r.classify(0,'Unhandled guest exception c0000005',Path('/missing'),'game',False)[0],'crash')
        self.assertEqual(r.classify(0,'CRASHREPORT /reports/unhandled-guest-exception-c0000005',Path('/missing'),'installer',False)[0],'crash')
        self.assertEqual(r.classify(1,'start failed: PE layout',Path('/missing'),'game',False)[0],'launch-error')

    def test_wizard_events_cannot_delay_smoke_screenshot(self):
        self.assertEqual(r.bounded_steps('36:button=OK;5:key=0d;3:guestopen=E:\\setup.exe;17:button=Next >',20),
                         '3:guestopen=E:\\setup.exe;5:key=0d;17:button=Next >')
        self.assertEqual(r.bounded_steps(';',20),'')

    def test_bundled_libraries_stay_in_private_c_copy(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'setup').mkdir()
            drive=root/'c';(drive/'WINDOWS/SYSTEM').mkdir(parents=True)
            (root/'setup/WING32.DLL').write_bytes(b'original library')
            recipe={'system_libraries':['setup/WING32.DLL']}
            r.stage_system_libraries(root,drive,recipe)
            self.assertEqual((drive/'WINDOWS/SYSTEM/WING32.DLL').read_bytes(),b'original library')
            self.assertEqual((root/'setup/WING32.DLL').read_bytes(),b'original library')
            with self.assertRaises(ValueError):r.stage_system_libraries(root,drive,recipe)
            with self.assertRaises(ValueError):r.stage_system_libraries(root,drive,{'system_libraries':['../outside']})
            (root/'link').symlink_to(drive,target_is_directory=True)
            with self.assertRaises(ValueError):r.stage_system_libraries(root,root/'link',recipe)

    def test_manifest_has_unique_pinned_games(self):
        manifest=json.loads(r.MANIFEST.read_text())
        games=manifest['games']
        self.assertGreaterEqual(len(games),30)
        self.assertEqual(len(games),len({g['id'] for g in games}))
        for renderer, ids in manifest['coverage_candidates'].items():
            self.assertGreaterEqual(len(set(ids)), 2, renderer)
            self.assertTrue(set(ids) <= {g['id'] for g in games})
        for game in games:
            with self.subTest(id=game['id']):
                self.assertRegex(game['sha256'],r'^[0-9a-f]{64}$')
                self.assertTrue(game['source_url'].startswith('https://'))
                self.assertTrue(game['license'])
                executable = game['launch']['exe']
                if executable.startswith('C:\\'):
                    # Installed games run from the private installer session.
                    self.assertTrue(game['install']['required'])
                    self.assertIn(executable.rsplit('\\', 1)[-1],
                                  game['install']['discover_exe'])
                else:
                    self.assertTrue(executable.startswith('E:\\'))
                self.assertIn(game['launch']['phase'],('installer','game'))


if __name__=='__main__': unittest.main()
