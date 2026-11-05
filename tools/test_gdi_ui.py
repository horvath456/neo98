# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Exercise the actual ARM GDI composition through QEMU's USB mouse/keyboard."""
import re
import time


def test_gdi_ui(qmp, log, process, build, wait_for, screenshot):
    wait_for(log, "NEO98 SELFTEST original-dibeng=PASS", process)
    wait_for(log, "NEO98 INPUT usb-mouse=ready", process)
    position = [400, 300]

    def move(x, y):
        while position != [x, y]:
            dx = max(-90, min(90, x - position[0])); dy = max(-90, min(90, y - position[1]))
            qmp.command("input-send-event", {"events": [
                {"type": "rel", "data": {"axis": "x", "value": dx}},
                {"type": "rel", "data": {"axis": "y", "value": dy}}]})
            position[0] += dx; position[1] += dy
            time.sleep(0.05)

    def button(down):
        qmp.command("input-send-event", {"events": [{"type": "btn", "data": {"button": "left", "down": down}}]})
        time.sleep(0.1)

    def key(name):
        qmp.command("human-monitor-command", {"command-line": f"sendkey {name}"})
        time.sleep(0.25)

    def pixel(data, x, y): return tuple(data[(y * 800 + x) * 3:(y * 800 + x) * 3 + 3])

    def unchanged_outside(before, after, rect):
        left, top, right, bottom = rect
        for y in range(600):
            spans = [(0, left * 3), (right * 3, 2400)] if top <= y < bottom else [(0, 2400)]
            for a, b in spans:
                start = y * 2400
                if before[start + a:start + b] != after[start + a:start + b]:
                    raise RuntimeError(f"GDI modified pixels outside its damage area at row {y}")

    move(780, 540); time.sleep(0.2)
    boot = screenshot(qmp, "gdi-boot")
    before_idle = log.read_text().count("NEO98 GDIFRAME")
    time.sleep(1.2)
    idle = screenshot(qmp, "gdi-idle")
    if boot != idle or log.read_text().count("NEO98 GDIFRAME") != before_idle:
        raise RuntimeError("Idle GDI scene repaints or changes pixels")
    key("spc"); wait_for(log, "NEO98 GDI operation=1 scroll=0", process)
    xor = screenshot(qmp, "gdi-xor")
    if xor == boot: raise RuntimeError("Changing the GDI raster operation had no visible effect")
    # The upper window must stay intact while the lower window changes.
    for y in range(252, 524):
        a = (y * 800 + 294) * 3; b = (y * 800 + 716) * 3
        if xor[a:b] != boot[a:b]: raise RuntimeError("Repainting lower window damaged upper window")
    unchanged_outside(boot, xor, (52, 80, 480, 386))
    move(340, 264); button(True); move(396, 162); button(False); move(780, 540)
    wait_for(log, "NEO98 GDI drag=1 x=350 y=150", process)
    time.sleep(0.2)
    moved = screenshot(qmp, "gdi-dragged")
    if pixel(moved, 310, 258) != (48, 96, 144):
        raise RuntimeError(f"Exposed lower-window bitmap was not restored: {pixel(moved, 310, 258)}")
    if pixel(moved, 310, 500) != (0, 128, 128):
        raise RuntimeError("Vacated window area did not restore the desktop")
    move(396, 162); button(True); move(340, 264); button(False); move(780, 540)
    time.sleep(0.2)
    restored = screenshot(qmp, "gdi-restored")
    if restored != xor: raise RuntimeError("Dragging a window back did not restore the exact image")
    key("s"); wait_for(log, "NEO98 GDI operation=1 scroll=1", process)
    scrolled = screenshot(qmp, "gdi-scrolled")
    if scrolled == restored: raise RuntimeError("Overlapping BitBlt scroll had no visible effect")
    unchanged_outside(restored, scrolled, (294, 252, 716, 524))
    # Leave the cursor where the existing Minesweeper test expects it.
    move(400, 300)
    frames = [dict(pixels=int(p), elapsed_us=int(t), objects=int(o)) for p, t, o in re.findall(
        r"NEO98 GDIFRAME pixels=(\d+) elapsed_us=(\d+) rect=[\d,]+ objects=(\d+)", log.read_text())]
    if not frames or len({f["objects"] for f in frames}) != 1:
        raise RuntimeError("GDI object count changed while repainting")
    return {"frames": frames, "passed": ["original DIBENG comparisons on AArch64", "idle has no scene redraws",
            "XOR changes lower window without damaging upper window", "USB title-bar dragging",
            "exposed bitmap and desktop restoration", "pixel-identical drag round trip",
            "overlapping bitmap scroll", "stable GDI object count"]}
