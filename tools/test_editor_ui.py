# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Drive the real ARM text window through QEMU USB input and inspect its pixels."""
import re
import time
import os


def test_editor_ui(qmp, log, process, build, wait_for, screenshot):
    wait_for(log, "NEO98 SELFTEST original-text=PASS", process)
    wait_for(log, "NEO98 INPUT usb-mouse=ready", process)
    position = [400, 300]

    def move(x, y):
        while position != [x, y]:
            dx = max(-90, min(90, x - position[0])); dy = max(-90, min(90, y - position[1]))
            qmp.command("input-send-event", {"events": [
                {"type": "rel", "data": {"axis": "x", "value": dx}},
                {"type": "rel", "data": {"axis": "y", "value": dy}}]})
            position[0] += dx; position[1] += dy; time.sleep(0.04)

    def button(down):
        qmp.command("input-send-event", {"events": [{"type": "btn", "data": {"button": "left", "down": down}}]})
        time.sleep(0.12)

    def key(code):
        # QMP qcodes name physical US keys; the guest defaults to German QWERTZ.
        if os.environ.get("NEO98_KEYMAP", "DE").upper() == "DE":
            code = "-".join({"y": "z", "z": "y"}.get(k, k) for k in code.split("-"))
        qmp.command("human-monitor-command", {"command-line": f"sendkey {code}"})
        time.sleep(0.15)

    def typing(text):
        for ch in text:
            key("spc" if ch == " " else "ret" if ch == "\n" else f"shift-{ch.lower()}" if ch.isupper() else ch)

    def state():
        found = re.findall(r"NEO98 EDIT action=([\w-]+) length=(\d+) cursor=(\d+) selected=(\d+) font=(\d+) lines=(\d+) scroll=(\d+) hash=([\da-f]+)", log.read_text())
        if not found: raise RuntimeError("No editor state was produced by USB input")
        a, n, c, s, f, lines, scroll, h = found[-1]
        return dict(action=a, length=int(n), cursor=int(c), selected=int(s), font=int(f), lines=int(lines), scroll=int(scroll), hash=int(h, 16))

    def presented_after(offset, event):
        # Input acknowledgement precedes drawing. A fixed key delay can leave
        # screendump reading a partly presented frame on a busy host.
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            recent = log.read_text()[offset:]
            at = recent.find(event)
            if at >= 0 and "NEO98 GDIFRAME" in recent[at:]:
                return
            if process.poll() is not None or "NEO98 FAIL" in recent:
                raise RuntimeError(f"Guest failed during editor redraw: {recent[-2000:]}")
            time.sleep(0.02)
        raise RuntimeError(f"Editor did not present a frame after {event!r}")

    def document(text):
        actual = state(); h = 2166136261
        for byte in text.encode("latin-1"): h = ((h ^ byte) * 16777619) & 0xffffffff
        if actual["length"] != len(text) or actual["hash"] != h:
            raise RuntimeError(f"Editor document mismatch, expected {text!r}, state={actual}")
        return actual

    def font(index):
        move(56 + 158 + index * 51 + 22, 146); button(True); button(False); move(780, 540)
        if state()["font"] != index: raise RuntimeError("Font button did not select its FNT")

    move(780, 540)
    boot = screenshot(qmp, "text-boot")
    frames = log.read_text().count("NEO98 GDIFRAME")
    time.sleep(1.15)
    if screenshot(qmp, "text-idle") != boot or frames != log.read_text().count("NEO98 GDIFRAME"):
        raise RuntimeError("Idle editor repaints or changes pixels")
    key("ctrl-a"); initial = state()
    if not initial["selected"] or initial["selected"] != initial["length"]: raise RuntimeError("Ctrl+A selection failed")
    font(5); large = state(); large_pixels = screenshot(qmp, "text-24pt-selected")
    if large["hash"] != initial["hash"] or large["selected"] != initial["selected"] or large["lines"] <= initial["lines"]:
        raise RuntimeError("Font change did not reflow the document while preserving selection")
    key("ctrl-home"); key("pgdn")
    if state()["scroll"] <= 0: raise RuntimeError("PageDown failed to scroll the text")
    screenshot(qmp, "text-scrolled")
    font(2); key("ctrl-a"); typing("NEO98 gmdq")
    expected = "NEO98 gmdq"; document(expected)
    # Letters used as desktop shortcuts must remain normal text in the editor.
    key("ret"); typing("x"); key("backspace"); expected += "\n"; document(expected)
    key("ctrl-home"); key("shift-right")
    if state()["selected"] != 1 or state()["cursor"] != 1: raise RuntimeError("Shift+Right selection failed")
    key("ctrl-c"); key("ctrl-end"); key("ctrl-v"); expected += "A"; document(expected)
    key("ctrl-z"); document(expected[:-1]); key("ctrl-z"); document(expected)
    key("ctrl-a"); key("ctrl-x"); document(""); key("ctrl-v"); document(expected)
    key("ctrl-home"); key("delete"); expected = expected[1:]; document(expected)
    key("ctrl-z"); expected = "A" + expected; document(expected)
    screenshot(qmp, "text-edited")
    # Hit-testing selects the entire first line, using the proportional font widths.
    move(76, 176); button(True); move(650, 176); button(False)
    selected = state()
    if selected["selected"] != len(expected.split("\n")[0]): raise RuntimeError(f"Mouse text selection failed: {selected}")
    move(780, 540); screenshot(qmp, "text-mouse-selection")
    offset = len(log.read_text())
    key("ctrl-home")
    presented_after(offset, "NEO98 EDIT action=move")
    before = screenshot(qmp, "text-before-drag")
    offset = len(log.read_text())
    move(156, 95); button(True); move(176, 80); button(False); move(780, 540)
    presented_after(offset, "NEO98 GDI drag=2 x=76 y=65")
    moved = screenshot(qmp, "text-dragged")
    if moved == before: raise RuntimeError("Dragging the editor did not change its position")
    offset = len(log.read_text())
    move(176, 80); button(True); move(156, 95); button(False); move(780, 540)
    presented_after(offset, "NEO98 GDI drag=2 x=56 y=80")
    if screenshot(qmp, "text-restored") != before: raise RuntimeError("Editor drag round trip did not restore exact pixels")
    # Verify document persistence, then leave the graphics desktop for its tests.
    key("esc"); wait_for(log, "NEO98 VIEW gdi", process)
    key("e"); document(expected); key("esc")
    move(400, 300)
    return {"reference_calls": 3648, "font_sizes": [8, 10, 12, 14, 18, 24],
            "passed": ["original text vectors on AArch64", "idle has no redraws", "font selection and proportional reflow",
                       "PageDown scrolling", "USB text and navigation sequences", "desktop shortcuts type as text",
                       "mouse and Shift selection", "local copy/cut/paste", "undo/redo", "Delete and Backspace",
                       "pixel-identical editor drag round trip", "document survives switching desktops"]}
