#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Check dependency cycles, forwarders, ordinals and delay imports together."""
from pathlib import Path
from tempfile import TemporaryDirectory
from types import SimpleNamespace as NS
from unittest.mock import patch
import unittest
import app_import_inventory as scanner


def image(imports=(), delay=(), exports=()):
    def descriptors(items):
        return [NS(dll=dll.encode(), imports=[NS(name=s.encode() if isinstance(s,str) else None,
                                                ordinal=s if isinstance(s,int) else None) for s in symbols])
                for dll,symbols in items]
    return NS(FILE_HEADER=NS(Machine=0x14c), OPTIONAL_HEADER=NS(Subsystem=3), sections=[],
              DIRECTORY_ENTRY_IMPORT=descriptors(imports), DIRECTORY_ENTRY_DELAY_IMPORT=descriptors(delay),
              DIRECTORY_ENTRY_EXPORT=NS(symbols=[NS(name=n.encode(),ordinal=o,forwarder=f.encode() if f else None)
                                                for n,o,f in exports]),
              parse_data_directories=lambda **kw:None, close=lambda:None)


class InventoryTest(unittest.TestCase):
    def test_complete_graph(self):
        # LCMapStringW is a failure contract of the tables generated from
        # Windows 98; without them it is an ordinary gap.
        if not any(scanner.ROOT.glob('build/generated/*unsupported.h')):
            self.skipTest('needs build/generated/*unsupported.h, generated from Windows 98')
        with TemporaryDirectory() as tmp:
            root=Path(tmp)
            images={
                'APP.EXE':image([('A.DLL',['Function',7]), ('KERNEL32.DLL',['CloseHandle','LCMapStringW'])],
                                [('LATE.DLL',['Later'])]),
                'A.DLL':image([('B.DLL',['Back'])],exports=[('Function',7,'KERNEL32.CloseHandle')]),
                'B.DLL':image([('A.DLL',[7])],exports=[('Back',1,None)])}
            for name in images:(root/name).touch()
            with patch.object(scanner.pefile,'PE',side_effect=lambda p,**kw:images[Path(p).name]):
                result=scanner.inventory([root/'APP.EXE'],[root],set())
            self.assertEqual(set(result['gaps']),{'LATE.DLL!Later'})
            self.assertEqual(len(result['modules']),3)
            self.assertEqual(sum(r['kind']=='forwarder' for r in result['imports']),3)
            self.assertTrue(any(r['status']=='original-failure-contract' for r in result['imports']))
            self.assertTrue(any(r['kind']=='delay-import' for r in result['imports']))

    def test_native_override(self):
        # SHELL32 runs as original x86 code since PLAN.md step 7, so an export
        # the file does not carry is a missing original export. --native-module
        # turns the same DLL around and asks our catalogue instead.
        with TemporaryDirectory() as tmp:
            root=Path(tmp)
            (root/'APP.EXE').touch();(root/'SHELL32.DLL').touch()
            images={'APP.EXE':image([('SHELL32.DLL',['NoSuchExport'])]),'SHELL32.DLL':image()}
            with patch.object(scanner.pefile,'PE',side_effect=lambda p,**kw:images[Path(p).name]):
                original=scanner.inventory([root/'APP.EXE'],[root],set())
                native=scanner.inventory([root/'APP.EXE'],[root],{'SHELL32.DLL'})
            self.assertEqual(original['gaps']['SHELL32.DLL!NoSuchExport']['status'],'missing-original-export')
            self.assertEqual(native['gaps']['SHELL32.DLL!NoSuchExport']['status'],'missing-native-export')


if __name__=='__main__':unittest.main()
