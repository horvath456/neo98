#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The disk image of the Mac app: a small Finder window with Neo98 on the
left, the Applications folder on the right and an arrow between them that
says what to do.

    build/analysis-venv/bin/python tools/dmg_layout.py APP DMG
    build/analysis-venv/bin/python tools/dmg_layout.py --background-only FOLDER

dmgbuild writes the window's layout (.DS_Store) without Finder or AppleScript,
so the image looks the same when CI builds it. The background is drawn here,
once at 1x and once at 2x for Retina screens.
"""
import argparse
from pathlib import Path
import sys

WIDTH, HEIGHT = 600, 380
ICON_SIZE = 112
# Icon centres in window points; the labels go below them.
APP_AT, APPLICATIONS_AT = (150, 170), (450, 170)
TEAL, DARK, GREY = (0, 128, 128), (34, 34, 34), (96, 96, 96)
FONTS = ('/System/Library/Fonts/Helvetica.ttc', '/System/Library/Fonts/SFNS.ttf',
         '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')
BOLD_FONTS = ('/System/Library/Fonts/Helvetica.ttc', '/System/Library/Fonts/SFNS.ttf',
              '/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf')


def font(size, bold=False):
    from PIL import ImageFont
    for path in BOLD_FONTS if bold else FONTS:
        if Path(path).exists():
            # Helvetica.ttc: face 1 is the bold one.
            index = 1 if bold and path.endswith('Helvetica.ttc') else 0
            return ImageFont.truetype(path, size, index=index)
    return ImageFont.load_default(size)


def background(scale):
    from PIL import Image, ImageDraw
    w, h = WIDTH * scale, HEIGHT * scale
    image = Image.new('RGB', (w, h))
    draw = ImageDraw.Draw(image)
    # A light vertical gradient, white into a pale teal.
    for y in range(h):
        t = y / (h - 1)
        draw.line([(0, y), (w, y)], fill=tuple(round(255 - (255 - c) * 0.10 * t) for c in (226, 242, 242)))
    s = scale
    # The arrow from the app to the folder.
    y = APP_AT[1] * s
    left, right = (APP_AT[0] + 78) * s, (APPLICATIONS_AT[0] - 78) * s
    head = 22 * s
    draw.line([(left, y), (right - head + 2 * s, y)], fill=TEAL, width=8 * s)
    draw.polygon([(right, y), (right - head, y - 15 * s), (right - head, y + 15 * s)], fill=TEAL)
    # Text above and below.
    title = font(26 * s, bold=True)
    body = font(15 * s)
    def centred(text, top, face, colour):
        box = draw.textbbox((0, 0), text, font=face)
        draw.text(((w - (box[2] - box[0])) / 2 - box[0], top), text, font=face, fill=colour)
    centred('Install Neo98', 28 * s, title, DARK)
    centred('Drag Neo98 onto the Applications folder.', 282 * s, body, DARK)
    centred('Then open Neo98 and choose your Windows 98 SE CD image.', 306 * s, body, GREY)
    return image


def write_backgrounds(folder):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    one, two = folder / 'background.png', folder / 'background@2x.png'
    background(1).save(one)
    background(2).save(two)
    return one


def build(app, target):
    import dmgbuild
    app, target = Path(app).resolve(), Path(target).resolve()
    work = target.with_name(target.name + '.layout')
    image = write_backgrounds(work)
    settings = {
        'format': 'UDZO',
        'filesystem': 'HFS+',
        'files': [str(app)],
        'symlinks': {'Applications': '/Applications'},
        'icon_locations': {app.name: APP_AT, 'Applications': APPLICATIONS_AT},
        'background': str(image),
        'window_rect': ((200, 120), (WIDTH, HEIGHT)),
        'default_view': 'icon-view',
        'icon_size': ICON_SIZE,
        'text_size': 13,
        'show_status_bar': False,
        'show_tab_view': False,
        'show_toolbar': False,
        'show_pathbar': False,
        'show_sidebar': False,
        'show_icon_preview': False,
        'arrange_by': None,
        'hide_extensions': [app.name],
    }
    target.unlink(missing_ok=True)
    import os
    dmgbuild.build_dmg(str(target), 'Neo98', settings=settings,
                       lookForHiDPI=os.environ.get('NEO98_DMG_HIDPI', '1') == '1')
    return target


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--background-only', type=Path, metavar='FOLDER',
                        help='only draw the backgrounds into FOLDER (works on any system)')
    parser.add_argument('app', nargs='?', type=Path)
    parser.add_argument('dmg', nargs='?', type=Path)
    args = parser.parse_args(argv)
    if args.background_only:
        print(write_backgrounds(args.background_only))
        return 0
    if sys.platform != 'darwin' or not args.app or not args.dmg:
        parser.error('a disk image is built on macOS from APP into DMG')
    print(build(args.app, args.dmg))
    return 0


if __name__ == '__main__':
    sys.exit(main())
