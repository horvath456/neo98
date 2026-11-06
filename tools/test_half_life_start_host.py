#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Half-Life after key validation: intro, Escape, main menu and New Game.

Supply an initialized C: tree and its registry journal. Their private clones
are used without printing or inspecting license values. No key is entered.
"""
import argparse
from test_support import ExternalInputParser
import json
import os
from pathlib import Path
import shutil
import subprocess
from PIL import Image, ImageChops
import arm64_run

ROOT = Path(__file__).resolve().parents[1]

def main():
    parser = ExternalInputParser()
    parser.add_argument('--drive-c', required=True, type=Path)
    parser.add_argument('--registry', required=True, type=Path)
    parser.add_argument('--host', type=Path, default=ROOT / 'build/native-app/neo98-host')
    parser.add_argument('--iso', type=Path, help='also start a new game with this CD image')
    parser.add_argument('--renderer', choices=('software','d3d','opengl'), default='software')
    parser.add_argument('--out', type=Path, default=ROOT / 'build/half-life-start')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    drive, registry = out / 'c', out / 'c.registry'
    assert args.drive_c.resolve() != drive and args.registry.resolve() != registry
    shutil.rmtree(drive, ignore_errors=True)
    subprocess.run(['cp', *(['-c'] if __import__('sys').platform == 'darwin' else ['--reflink=auto']), '-R', str(args.drive_c.resolve()), str(drive)], check=True)
    shutil.copyfile(args.registry, registry)
    renderer_switch={'software':'-soft','d3d':'-d3d','opengl':'-gl'}[args.renderer]
    width,height=(320,240) if args.renderer=='software' else (640,480)
    command=f'hl.exe {renderer_switch} -width {width} -height {height} -fullscreen'
    if args.renderer=='opengl':command+=' -gldrv Default'
    # Hardware renderers initialize after New Game, then show the chapter
    # title over black before fading into the tram. Sample the scene after it.
    game_times=(45,51) if args.renderer=='software' else (60,66)
    last=game_times[-1]
    steps = [f'2:shot={out}/intro.ppm', '5:key=1b', '8:key=1b',
             '10:wait=122,295,efb53a',
             f'12:shot={out}/menu.ppm', '14:click=170,305',
             f'16:shot={out}/difficulty.ppm']
    if args.iso:
        steps += ['18:click=110,240', *[f'{t}:shot={out}/game-{t}.ppm' for t in game_times],
                  f'{last+1}:key=1b', f'{last+4}:shot={out}/return-menu.ppm', f'{last+6}:menu=quit']
    else:
        steps += ['18:menu=quit']
    env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy',
               NEO98_HOST_INPUT=';'.join(steps), NEO98_HOST_DEADLINE=str(last+25), NEO98_HOST_SCREEN='1024x768',
               NEO98_SETTINGS_DIR=str(out / 'settings'), NEO98_LOG_DIR=str(out / 'logs'))
    for key in ('NEO98_HOST_ATTACHED', 'NEO98_HOST_MENU', 'NEO98_TRACE_CALLS_FROM', 'NEO98_TRACE_PAINT'):
        env.pop(key, None)
    try:
        with (out / 'host.log').open('w') as log:
            process = subprocess.Popen([str(args.host.resolve()), '-', str(ROOT / 'build/generated/notepad-fixedsys.fnt'),
                '--system-tree', str(drive), '--drive-c', str(drive), '--registry', str(registry),
                *(['--cdrom', str(args.iso.resolve())] if args.iso else []),
                '--exe', r'C:\SIERRA\Half-Life\hl.exe', '--directory', r'C:\SIERRA\Half-Life',
                '--command', command,
                '--quiet', '--crash-report-dir', str(out / 'reports')],
                env=env, stdout=log, stderr=subprocess.STDOUT)
            try:code=process.wait(timeout=last+40)
            except subprocess.TimeoutExpired:
                arm64_run.end(process)
                raise
        log = (out / 'host.log').read_text(errors='replace')
        assert code == 0 and 'NEO98 HOST failed' not in log and 'FAULT BOX' not in log, log[-3000:]
        assert 'Heap arena exhausted' not in log and 'Heap arena grown to 64 MiB' in log
        counts = {}
        for name, boxes in [('menu', [(70, 180, 220, 200), (70, 400, 220, 455)]),
                            ('difficulty', [(45, 50, 345, 110), (50, 140, 125, 260)])]:
            frame = Image.open(out / (name + '.ppm')).convert('RGB')
            frame.save(out / (name + '.png'))
            frame = frame.resize((640, 480), Image.Resampling.NEAREST)
            for i, box in enumerate(boxes):
                orange = sum(n for n, (r, g, b) in frame.crop(box).getcolors(640 * 480)
                             if r > 150 and g > 70 and b < 100)
                assert orange > 100, (name, box, orange)
                counts[f'{name}_{i}'] = orange
        # Half-Life paints these STATIC controls itself. The old compositor
        # replaced the text with a flat gray rectangle on its next frame.
        menu = Image.open(out / 'menu.ppm').convert('RGB').resize((640, 480), Image.Resampling.NEAREST)
        for y in (186, 218, 250, 282, 314, 378, 410, 442):
            colors = menu.crop((236, y, 472, y + 20)).getcolors(640 * 480)
            text = sum(n for n, (r, g, b) in colors if min(r, g, b) >= 90 and max(r, g, b) - min(r, g, b) <= 5)
            assert 40 < text < 1500, ('description missing or overwritten', y, text)
            counts[f'description_{y}'] = text
        Image.open(out / 'intro.ppm').save(out / 'intro.png')
        if args.iso:
            world = []
            for t in game_times:
                frame = Image.open(out / f'game-{t}.ppm').convert('RGB')
                frame.save(out / f'game-{t}.png')
                world.append(frame.resize((640, 480)))
                for x, y in ((0, 0), (320, 0), (0, 240), (320, 240)):
                    quadrant = world[-1].crop((x, y, x + 320, y + 240))
                    nonblack = sum(n for n, color in quadrant.getcolors(320 * 240) if max(color) > 0)
                    # The tram first passes through a black tunnel. Its later
                    # frame must fill every quadrant with the rendered world.
                    if t==game_times[-1]:
                        assert nonblack > 15000, ('incomplete game viewport', t, x, y, nonblack)
                    counts[f'world_{t}_{x}_{y}'] = nonblack
            assert len(world[1].getcolors(640 * 480)) > 50, 'no rendered scene'
            delta = ImageChops.difference(*world).convert('L')
            counts['world_changed'] = 640 * 480 - delta.histogram()[0]
            assert counts['world_changed'] > 1000, counts
            modes = [line for line in log.splitlines() if 'DDRAWDRIVER display mode' in line]
            assert any(f'0x{width:04x}{height:04x}' in line for line in modes), modes
            assert '0x028001e0' in modes[-1], ('menu mode not restored', modes)
            frame = Image.open(out / 'return-menu.ppm').convert('RGB')
            frame.save(out / 'return-menu.png')
            counts['return_menu_orange'] = sum(n for n, (r, g, b) in frame.getcolors(frame.width * frame.height)
                                               if r > 150 and g > 70 and b < 100)
            assert counts['return_menu_orange'] > 1000, 'Escape did not restore the menu'
        (out / 'results.json').write_text(json.dumps({'status': 'PASS', 'host': str(args.host.resolve()),
            'renderer':args.renderer,'command':command,
            'checks': ['32 MiB allocation proceeds', 'original intro opens', 'Escape input during intro playback',
                       'main menu with description text', 'New Game difficulty selection'] + (['Black Mesa arrival fills all four screen quadrants and advances', 'Escape restores the 640x480 menu'] if args.iso else []),
            'pixels': counts,
            'limits': ['Full playthrough not tested',
                       'Dummy audio; audible playback not assessed']}, indent=2) + '\n')
        print(f'HALF-LIFE START PASS ({args.renderer}): 32 MiB allocation, intro, Escape, menu, difficulty' +
              (', moving Black Mesa arrival' if args.iso else ''))
    finally:
        shutil.rmtree(drive, ignore_errors=True)
        registry.unlink(missing_ok=True)

if __name__ == '__main__':
    main()
