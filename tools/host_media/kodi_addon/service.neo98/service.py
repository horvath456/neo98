# SPDX-License-Identifier: GPL-2.0-or-later
"""NEO98's helper inside Kodi: the dialog Kodi shows, for Media Center 98.

Kodi's JSON-RPC waits while an add-on waits for a dialog (Files.GetDirectory
of Netflix waits for the choice of profile), so NEO98 cannot ask Kodi
itself. This service runs in Kodi on a thread of its own, reads the topmost
dialog through info labels, and answers it with Kodi's built-in functions (SendClick, SetFocus, Action).
It listens on 127.0.0.1, port 9779 (NEO98_KODI_HELPER_PORT), one JSON
object per line:

  {"get": 1} -> the dialog: {"serial": n, "kind": "yesno", "heading":...}
                     or {"serial": 0}
  {"answer": {"serial": n, "button": 1}} a button (OK 0; No 0, Yes 1, third 2)
  {"answer": {"serial": n, "item": 2}} an entry of a list
  {"answer": {"serial": n, "text": "..."}} the text of an input dialog
  {"answer": {"serial": n, "cancel": true}} Back
                  -> {"ok": true} or {"ok": false}

It reads only info labels, never control objects of Kodi's own dialogs,
which Python must not hold. The labels are the controls of Kodi's dialogs (DialogConfirm, DialogSelect,
DialogKeyboard, DialogNumeric, DialogProgress, DialogTextViewer), the same
in Estuary and most skins.
"""
import json
import os
import shutil
import socket
import threading
import time
import xml.etree.ElementTree as ElementTree

import xbmc
import xbmcgui

PORT = int(os.environ.get('NEO98_KODI_HELPER_PORT', '9779'))
# Kodi's window ids of the dialogs, and what they are to NEO98.
YES_NO, PROGRESS, KEYBOARD, NUMERIC, SELECT, OK, TEXT_VIEWER = 10100, 10101, 10103, 10109, 12000, 12002, 10147
KINDS = {YES_NO: 'yesno', PROGRESS: 'progress', KEYBOARD: 'input', NUMERIC: 'input', SELECT: 'select', OK: 'ok',
         TEXT_VIEWER: 'ok'}
# Kodi's own overlays over a video (seek bar, volume, toasts, spinner, info)
# open by conditions of the skin, so closing them does not last. The add-on
# keeps a copy of Estuary, SKIN, in which everything in those windows sits in
# a group hidden while the home window's property HIDE is set; it sets HIDE
# while NEO98 asks (within the last two seconds) and a video plays. Kodi
# looks like Estuary otherwise.
SKIN = 'skin.estuary.neo98'
HIDE = 'neo98.video'
OVERLAY_FILES = ('DialogSeekBar.xml', 'DialogVolumeBar.xml', 'Custom_1103_VolumeSlider.xml', 'DialogNotification.xml',
                 'DialogBusy.xml', 'DialogBusyNoCancel.xml', 'DialogExtendedProgressBar.xml', 'DialogFullScreenInfo.xml',
                 'DialogPlayerProcessInfo.xml', 'VideoFullScreen.xml', 'Custom_1109_TopBarOverlay.xml')
SKIN_PATCH = 3
# Words of a heading that asks for something secret.
SECRET = ('password', 'passwort', 'kennwort', 'pin', 'secret', 'passcode', 'mot de passe', 'contraseña')
CHOICES = 32


def info(label):
    return xbmc.getInfoLabel(label) or ''


def visible(control):
    return bool(xbmc.getCondVisibility('Control.IsVisible(%d)' % control))


def read(window):
    """The dialog window as NEO98 sees it, without its serial."""
    kind = KINDS[window]
    dialog = {'kind': kind, 'window': window, 'heading': info('Control.GetLabel(1)'), 'text': '', 'buttons': [], 'flags': []}
    if window == TEXT_VIEWER:
        dialog['text'] = info('Control.GetLabel(5)')
    elif window in (YES_NO, OK, PROGRESS):
        # The text box, or the three lines of older skins.
        dialog['text'] = info('Control.GetLabel(9)') or '\n'.join(t for t in (info('Control.GetLabel(%d)' % i) for i in (2, 3, 4)) if t)
    if window == OK:
        # DialogConfirm as the OK dialog: only the button 11 shows (Kodi 21).
        dialog['buttons'] = [info('Control.GetLabel(11)') or info('Control.GetLabel(10)')]
    elif window == YES_NO:
        dialog['buttons'] = [info('Control.GetLabel(10)'), info('Control.GetLabel(11)'),
                             info('Control.GetLabel(12)') if visible(12) else '']
    elif window == SELECT:
        # The plain list (3) or the one with pictures (6).
        control = 6 if visible(6) and not visible(3) else 3
        dialog['list'] = control
        count = min(int(info('Container(%d).NumItems' % control) or 0), CHOICES)
        dialog['choices'] = [info('Container(%d).ListItemAbsolute(%d).Label' % (control, i)) for i in range(count)]
    elif window in (KEYBOARD, NUMERIC):
        dialog['heading'] = info('Control.GetLabel(311)') if window == KEYBOARD else dialog['heading']
        text = info('Control.GetLabel(312)') if window == KEYBOARD else ''
        hidden = any(word in dialog['heading'].lower() for word in SECRET) or (text and set(text) == {'*'})
        if hidden:
            dialog['flags'].append('hidden')
        if window == NUMERIC:
            dialog['flags'].append('number')
        dialog['input'] = '' if hidden else text
    elif window == PROGRESS:
        # Only info labels: a control object of a system dialog
        # (ControlProgress.getPercent) takes Kodi down.
        percent = info('System.ProgressBar')
        dialog['percent'] = int(percent) if percent.isdigit() else 0
        if visible(10):
            dialog['flags'].append('cancel')
    return dialog


class Dialogs:
    """The dialog Kodi shows now, read on Kodi's side every fifth of a second
    (every twentieth while a video plays)."""

    def __init__(self):
        self.lock = threading.Lock()
        self.serial, self.key, self.current = 0, None, None
        self.answers = []
        # When NEO98 last asked, whether HIDE is set, and until when a Yes/No
        # is Kodi's "Keep this skin?" after the add-on changed the skin.
        self.asked, self.hidden, self.confirm_until = 0.0, False, 0.0

    def poll(self):
        """Reads the dialog; True while a video plays (then it looks again sooner)."""
        playing = bool(xbmc.getCondVisibility('Player.HasVideo'))
        hide = playing and time.monotonic() - self.asked < 2
        if hide != self.hidden:
            self.hidden = hide
            xbmc.executebuiltin(('SetProperty(%s,1,home)' if hide else 'ClearProperty(%s,home)') % HIDE)
        window = xbmcgui.getCurrentWindowDialogId()
        if window == YES_NO and time.monotonic() < self.confirm_until:
            # Kodi asks whether to keep the skin the add-on chose.
            self.confirm_until = 0
            xbmc.executebuiltin('SendClick(%d,11)' % YES_NO)
            return playing
        dialog = read(window) if window in KINDS else None
        with self.lock:
            if dialog is None:
                self.key, self.current = None, None
            else:
                # A new dialog, or the same window with other words; a
                # progress keeps its serial while its text and bar move.
                key = (window,) if window == PROGRESS else (window, dialog['heading'], dialog['text'], tuple(dialog.get('choices', ())))
                if key != self.key:
                    self.serial += 1
                    self.key = key
                dialog['serial'] = self.serial
                self.current = dialog
            answers, self.answers = self.answers, []
        for answer in answers:
            self.perform(*answer)
        return playing

    def get(self):
        self.asked = time.monotonic()
        with self.lock:
            return dict(self.current) if self.current else {'serial': 0}

    def answer(self, answer):
        with self.lock:
            if not self.current or answer.get('serial') != self.current['serial']:
                return False
            self.answers.append((dict(self.current), answer))
            return True

    @staticmethod
    def perform(dialog, answer):
        window = dialog['window']
        if answer.get('cancel'):
            xbmc.executebuiltin('Action(Back)')
        elif window in (KEYBOARD, NUMERIC):
            xbmc.executeJSONRPC(json.dumps({'jsonrpc': '2.0', 'id': 1, 'method': 'Input.SendText',
                                            'params': {'text': str(answer.get('text', '')), 'done': True}}))
        elif window == SELECT:
            # A click message carries no action, and the list only takes
            # Select: focus the entry, then select it.
            control = dialog.get('list', 3)
            xbmc.executebuiltin('SetFocus(%d,%d,absolute)' % (control, int(answer.get('item', 0))))
            xbmc.executebuiltin('Action(Select,%d)' % window)
        elif window == YES_NO:
            xbmc.executebuiltin('SendClick(%d,%d)' % (window, 10 + min(max(int(answer.get('button', 0)), 0), 2)))
        elif window == OK:
            xbmc.executebuiltin('SendClick(%d,%d)' % (window, 11 if visible(11) or not visible(10) else 10))
        else:
            xbmc.executebuiltin('Action(Back)')


def serve(dialogs, listener, stopping):
    """One line in, one line out, for each connection of NEO98."""
    connections, talkers = [], []

    def talk(connection):
        with connection:
            buffer = b''
            while not stopping.is_set():
                try:
                    data = connection.recv(65536)
                except socket.timeout:
                    continue
                except OSError:
                    return
                if not data:
                    return
                buffer += data
                while b'\n' in buffer:
                    line, buffer = buffer.split(b'\n', 1)
                    try:
                        request = json.loads(line.decode('utf-8'))
                    except ValueError:
                        request = {}
                    if 'answer' in request:
                        reply = {'ok': dialogs.answer(request['answer'])}
                    else:
                        reply = dialogs.get()
                    try:
                        connection.sendall(json.dumps(reply).encode('utf-8') + b'\n')
                    except OSError:
                        return
    listener.settimeout(0.5)
    while not stopping.is_set():
        try:
            connection, _ = listener.accept()
        except socket.timeout:
            continue
        except OSError:
            return
        connection.settimeout(1.0)
        connections.append(connection)
        talkers.append(threading.Thread(target=talk, args=(connection,), daemon=True))
        talkers[-1].start()
    # Kodi waits for the threads of an add-on it stops.
    for connection in connections:
        try:
            connection.shutdown(socket.SHUT_RDWR)
        except OSError:
            pass
    for talker in talkers:
        talker.join(2)


def listen(port=PORT):
    listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(('127.0.0.1', port))
    listener.listen(4)
    return listener


def rpc(method, params=None):
    reply = xbmc.executeJSONRPC(json.dumps({'jsonrpc': '2.0', 'id': 1, 'method': method, 'params': params or {}}))
    return json.loads(reply).get('result')


def hide_overlays(path):
    """Everything in a window of the skin into one group hidden by HIDE."""
    tree = ElementTree.parse(path)
    controls = tree.getroot().find('controls')
    if controls is None or controls.get('neo98') == str(SKIN_PATCH):
        return
    group = ElementTree.Element('control', {'type': 'group'})
    visible = ElementTree.SubElement(group, 'visible')
    visible.text = 'String.IsEmpty(Window(Home).Property(%s))' % HIDE
    for child in list(controls):
        controls.remove(child)
        group.append(child)
    controls.append(group)
    controls.set('neo98', str(SKIN_PATCH))
    tree.write(path, encoding='utf-8', xml_declaration=True)


def prepare_skin(dialogs):
    """SKIN beside Estuary, and Kodi on it, where Kodi runs Estuary."""
    import xbmcvfs
    current = rpc('Settings.GetSettingValue', {'setting': 'lookandfeel.skin'}) or {}
    if current.get('value') not in ('skin.estuary', SKIN):
        return
    source = xbmcvfs.translatePath('special://xbmc/addons/skin.estuary')
    target = xbmcvfs.translatePath('special://home/addons/' + SKIN)
    if not os.path.isdir(source):
        return
    version = ElementTree.parse(os.path.join(source, 'addon.xml')).getroot().get('version', '')
    stamp = '%s/%d' % (version, SKIN_PATCH)
    stamp_file = os.path.join(target, 'neo98.stamp')
    fresh = not (os.path.exists(stamp_file) and open(stamp_file).read() == stamp)
    if fresh:
        shutil.rmtree(target, ignore_errors=True)
        shutil.copytree(source, target)
        manifest = ElementTree.parse(os.path.join(target, 'addon.xml'))
        manifest.getroot().set('id', SKIN)
        manifest.getroot().set('name', 'Estuary (NEO98)')
        manifest.write(os.path.join(target, 'addon.xml'), encoding='utf-8', xml_declaration=True)
        for name in OVERLAY_FILES:
            path = os.path.join(target, 'xml', name)
            if os.path.exists(path):
                hide_overlays(path)
        # The user's settings of Estuary (menu items, colours) come along.
        settings = xbmcvfs.translatePath('special://profile/addon_data/skin.estuary/settings.xml')
        own = xbmcvfs.translatePath('special://profile/addon_data/%s/' % SKIN)
        if os.path.exists(settings) and not os.path.exists(os.path.join(own, 'settings.xml')):
            os.makedirs(own, exist_ok=True)
            shutil.copy(settings, own)
        with open(stamp_file, 'w') as f:
            f.write(stamp)
        xbmc.executebuiltin('UpdateLocalAddons')
        for _ in range(50):
            if xbmc.getCondVisibility('System.HasAddon(%s)' % SKIN) or rpc('Addons.GetAddonDetails', {'addonid': SKIN}):
                break
            time.sleep(0.2)
        rpc('Addons.SetAddonEnabled', {'addonid': SKIN, 'enabled': True})
        # Kodi already on the copy holds the old files.
        if current.get('value') == SKIN:
            xbmc.executebuiltin('ReloadSkin()')
    if current.get('value') == 'skin.estuary':
        xbmc.log('NEO98: Kodi uses %s, Estuary with its overlays hidden under NEO98' % SKIN, xbmc.LOGINFO)
        dialogs.confirm_until = time.monotonic() + 20
        rpc('Settings.SetSettingValue', {'setting': 'lookandfeel.skin', 'value': SKIN})


def main():
    dialogs, stopping = Dialogs(), threading.Event()
    try:
        listener = listen()
    except OSError as error:
        xbmc.log('NEO98: port %d is taken (%s)' % (PORT, error), xbmc.LOGWARNING)
        return
    thread = threading.Thread(target=serve, args=(dialogs, listener, stopping), daemon=True)
    thread.start()
    try:
        prepare_skin(dialogs)
    except Exception as error:  # Kodi runs on with its own skin.
        xbmc.log('NEO98: no skin of its own (%s)' % error, xbmc.LOGWARNING)
    monitor = xbmc.Monitor()
    playing = False
    while not monitor.waitForAbort(0.05 if playing else 0.2):
        playing = dialogs.poll()
    stopping.set()
    thread.join(5)
    listener.close()


if __name__ == '__main__':
    main()
