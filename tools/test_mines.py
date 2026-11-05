# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
"""Drive the ARM game's USB mouse with QMP, checking original-generated outcomes."""
import json
import re
import time


def test_mines(qmp, serial_log, process, build, wait_for, screenshot):
    wait_for(serial_log, "NEO98 SELFTEST original-winmine=PASS", process)
    wait_for(serial_log, "NEO98 INPUT usb-mouse=ready", process)
    screenshot(qmp, "mines-boot")
    position = [400, 300]

    def move(x, y):
        while position != [x, y]:
            dx = max(-100, min(100, x - position[0]))
            dy = max(-100, min(100, y - position[1]))
            qmp.command("input-send-event", {"events": [
                {"type": "rel", "data": {"axis": "x", "value": dx}},
                {"type": "rel", "data": {"axis": "y", "value": dy}}]})
            position[0] += dx; position[1] += dy
            time.sleep(0.035)

    def click(button):
        for down in (True, False):
            qmp.command("input-send-event", {"events": [
                {"type": "btn", "data": {"button": button, "down": down}}]})
            time.sleep(0.065)

    def await_new(offset, expected, timeout=5):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            data = serial_log.read_text(errors="replace")[offset:]
            if expected in data: return data
            if process.poll() is not None or "NEO98 FAIL" in data: break
            time.sleep(0.025)
        raise RuntimeError(f"Minesweeper did not reach {expected!r}:\n{data}")

    actions = json.loads((build / "generated/winmine-gui.json").read_text())
    for index, action in enumerate(actions):
        offset = len(serial_log.read_text())
        if action["kind"] == "new": move(400, 164); click("left")
        else:
            move(288 + (action["column"] - 1) * 32, 234 + (action["row"] - 1) * 32)
            click(action["kind"])
        expected = " ".join(f"{name}={action[name]}" for name in ("phase", "face", "revealed", "remaining"))
        await_new(offset, expected)
        if index == 0: screenshot(qmp, "mines-flagged")
        if index == 3: screenshot(qmp, "mines-playing")
        if action["face"] == 2: screenshot(qmp, "mines-lost")
        if action["face"] == 3: screenshot(qmp, "mines-won")

    # Menus change level through mouse input, including restoring their backdrop.
    offset = len(serial_log.read_text())
    move(270, 98); click("left")
    move(303, 161); click("left")
    await_new(offset, "board=16x16")
    screenshot(qmp, "mines-intermediate")
    offset = len(serial_log.read_text())
    qmp.command("human-monitor-command", {"command-line": "sendkey 3"})
    await_new(offset, "board=30x16")
    time.sleep(0.1)
    screenshot(qmp, "mines-expert")
    frames = re.findall(r"NEO98 MINEFRAME kind=(\w+) pixels=(\d+) elapsed_us=(\d+)", serial_log.read_text())
    partial = [int(pixels) for kind, pixels, _ in frames if kind == "partial" and int(pixels)]
    if not partial or min(partial) >= 480000:
        raise RuntimeError("Minesweeper did not use partial updates")
    replay_steps = int(re.search(r"original-winmine=PASS steps=(\d+)", serial_log.read_text()).group(1))
    return {"mouse_actions_against_original": len(actions), "guest_replay_steps": replay_steps,
            "passed": ["USB mouse enumeration", "flag/question/covered cycle",
                       "first-click mine relocation", "loss", "smiley starts new game", "middle-button chord", "win",
                       "Game menu selects intermediate", "keyboard selects expert", "partial drawing"]}
