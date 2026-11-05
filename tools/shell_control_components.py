# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
def test(source_root=None,object_dir=None):
 from test_shell_control_native import SOURCE,test as native
 return native(source_root or SOURCE,object_dir)
