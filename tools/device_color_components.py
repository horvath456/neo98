#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Import contract for the native bitmap device-color adapter fixture."""
IMPORTS={'KERNEL32':{'ExitProcess':4},'GDI32':{'CreateBitmap':20,'GetBitmapBits':12,'BitBlt':36,'CreateCompatibleDC':4,'DeleteDC':4,'DeleteObject':4,'SelectObject':8,'CreateDIBSection':24,'CreateSolidBrush':4,'CreatePen':12,'PatBlt':24,'MoveToEx':16,'LineTo':12,'GetPixel':12,'SetPixel':16,'SaveDC':4,'RestoreDC':8,'IntersectClipRect':20}}
IMPORTS['USER32']={'FillRect':12}
