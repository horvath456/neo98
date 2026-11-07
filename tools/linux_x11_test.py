#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""X11 inspection and input for windows belonging to a test process."""
import ctypes as C
from pathlib import Path
import struct
import time
import zlib

U = C.c_ulong
P = C.c_void_p
I = C.c_int

class Attributes(C.Structure):
    _fields_ = [(name, kind) for name, kind in (
        ('x', I), ('y', I), ('width', I), ('height', I), ('border', I), ('depth', I),
        ('visual', P), ('root', U), ('klass', I), ('gravity', I), ('win_gravity', I),
        ('backing_store', I), ('backing_planes', U), ('backing_pixel', U), ('save_under', I),
        ('colormap', U), ('map_installed', I), ('map_state', I), ('all_events', C.c_long),
        ('your_events', C.c_long), ('no_propagate', C.c_long), ('override_redirect', I), ('screen', P))]

class Image(C.Structure):
    _fields_ = [('width', I), ('height', I), ('xoffset', I), ('format', I), ('data', P),
                ('byte_order', I), ('bitmap_unit', I), ('bit_order', I), ('pad', I), ('depth', I),
                ('stride', I), ('bpp', I), ('red', U), ('green', U), ('blue', U)]

class Error(C.Structure):
    _fields_=[('type',I),('display',P),('resource',U),('serial',U),('code',C.c_ubyte),
              ('request',C.c_ubyte),('minor',C.c_ubyte)]

class ClientMessage(C.Structure):
    _fields_=[('type',I),('serial',U),('send',I),('display',P),('window',U),('message',U),('format',I),
              ('data',C.c_long*5)]

class X11:
    def __init__(self):
        self.x = C.CDLL('libX11.so.6')
        self.xt = C.CDLL('libXtst.so.6')
        self.errors=[]
        handler=C.CFUNCTYPE(I,P,C.POINTER(Error))
        self.error_handler=handler(lambda display,event:self.errors.append((event.contents.code,event.contents.request)) or 0)
        self.x.XSetErrorHandler.argtypes=[handler]
        self.x.XSetErrorHandler(self.error_handler)
        signatures = {
            'XOpenDisplay': ([C.c_char_p], P), 'XDefaultRootWindow': ([P], U),
            'XInternAtom': ([P, C.c_char_p, I], U), 'XFree': ([P], I), 'XFlush': ([P], I),
            'XQueryTree': ([P, U, C.POINTER(U), C.POINTER(U), C.POINTER(C.POINTER(U)), C.POINTER(C.c_uint)], I),
            'XGetWindowProperty': ([P,U,U,C.c_long,C.c_long,I,U,C.POINTER(U),C.POINTER(I),C.POINTER(U),C.POINTER(U),C.POINTER(P)], I),
            'XGetWindowAttributes': ([P,U,C.POINTER(Attributes)], I),
            'XTranslateCoordinates': ([P,U,U,I,I,C.POINTER(I),C.POINTER(I),C.POINTER(U)], I),
            'XGetImage': ([P,U,I,I,C.c_uint,C.c_uint,U,I], C.POINTER(Image)),
            'XDestroyImage': ([P], I), 'XKeysymToKeycode': ([P,U], C.c_ubyte),
            'XStringToKeysym': ([C.c_char_p], U), 'XSetInputFocus': ([P,U,I,U], I),
            'XIconifyWindow': ([P,U,I], I), 'XMapRaised': ([P,U], I),
            'XGetInputFocus': ([P,C.POINTER(U),C.POINTER(I)], I),
            'XSendEvent': ([P,U,I,C.c_long,P], I),
        }
        for name, (args, ret) in signatures.items():
            fn = getattr(self.x, name); fn.argtypes = args; fn.restype = ret
        for name,args in [('XTestFakeMotionEvent',[P,I,I,I,U]), ('XTestFakeButtonEvent',[P,C.c_uint,I,U]),
                          ('XTestFakeKeyEvent',[P,C.c_uint,I,U])]:
            getattr(self.xt,name).argtypes = args
        self.display = self.x.XOpenDisplay(None)
        if not self.display: raise RuntimeError('X11 display unavailable')
        self.root = self.x.XDefaultRootWindow(self.display)

    def require_input(self):
        # XWayland can advertise XTEST while its compositor refuses injection
        # (for example until a remote-control portal session is authorized).
        self.x.XQueryPointer.argtypes = [P, U, C.POINTER(U), C.POINTER(U),
            C.POINTER(I), C.POINTER(I), C.POINTER(I), C.POINTER(I), C.POINTER(C.c_uint)]
        def position():
            root, child, rx, ry, wx, wy, mask = U(), U(), I(), I(), I(), I(), C.c_uint()
            self.x.XQueryPointer(self.display, self.root, C.byref(root), C.byref(child),
                C.byref(rx), C.byref(ry), C.byref(wx), C.byref(wy), C.byref(mask))
            return rx.value, ry.value
        original = position()
        target = (original[0] - 1 if original[0] else 1, original[1])
        self.xt.XTestFakeMotionEvent(self.display, -1, *target, 0)
        self.x.XFlush(self.display)
        deadline = time.monotonic() + 1
        while position() != target and time.monotonic() < deadline:
            time.sleep(.02)
        moved = position() == target
        self.xt.XTestFakeMotionEvent(self.display, -1, *original, 0)
        self.x.XFlush(self.display)
        if not moved:
            from test_support import skip
            skip('X11 compositor advertises XTEST but refuses synthetic input')

    def focus(self):
        window, mode = U(), I()
        self.x.XGetInputFocus(self.display,C.byref(window),C.byref(mode))
        return window.value

    def activate(self, window):
        event=(U*24)()
        message=C.cast(event,C.POINTER(ClientMessage)).contents
        message.type=33;message.display=self.display;message.window=window;message.format=32
        message.message=self.x.XInternAtom(self.display,b'_NET_ACTIVE_WINDOW',False)
        message.data[0]=2
        self.x.XSendEvent(self.display,self.root,False,(1<<19)|(1<<20),event)
        self.x.XFlush(self.display)

    def property(self, window, name):
        atom = self.x.XInternAtom(self.display, name.encode(), False)
        actual, count, remaining, fmt, data = U(), U(), U(), I(), P()
        self.x.XGetWindowProperty(self.display,window,atom,0,4096,False,0,
                                 C.byref(actual),C.byref(fmt),C.byref(count),C.byref(remaining),C.byref(data))
        if not data: return b''
        if fmt.value == 32:
            result = list(C.cast(data,C.POINTER(U))[:count.value])
        else: result = C.string_at(data,count.value * fmt.value // 8)
        self.x.XFree(data)
        return result

    def windows(self, pid):
        self.errors.clear()
        found = []
        def scan(window, depth=0):
            if depth > 8: return
            # GTK override-redirect menus identify their process through the
            # client leader rather than carrying _NET_WM_PID themselves.
            leader = self.property(window, 'WM_CLIENT_LEADER')
            owned = self.property(window, '_NET_WM_PID') == [pid]
            if not owned and leader:
                owned = self.property(leader[0], '_NET_WM_PID') == [pid]
            if owned:
                a = Attributes()
                if self.x.XGetWindowAttributes(self.display,window,C.byref(a)) and a.map_state == 2:
                    x,y,child = I(),I(),U()
                    self.x.XTranslateCoordinates(self.display,window,self.root,0,0,C.byref(x),C.byref(y),C.byref(child))
                    title = self.property(window,'_NET_WM_NAME') or self.property(window,'WM_NAME')
                    found.append(dict(id=window,title=title.decode('utf-8','replace') if isinstance(title,bytes) else '',
                                      x=x.value,y=y.value,width=a.width,height=a.height))
            root,parent,children,n = U(),U(),C.POINTER(U)(),C.c_uint()
            if self.x.XQueryTree(self.display,window,C.byref(root),C.byref(parent),C.byref(children),C.byref(n)):
                entries = list(children[:n.value]); self.x.XFree(children)
                for entry in entries: scan(entry,depth+1)
        scan(self.root)
        # A window can close between QueryTree and GetProperty. Its stale
        # XID is harmless during enumeration; other protocol errors are not.
        assert all(code in (3,9) for code,_ in self.errors),self.errors
        return found

    def capture(self, window, path):
        a = Attributes(); self.x.XGetWindowAttributes(self.display,window,C.byref(a))
        image = self.x.XGetImage(self.display,window,0,0,a.width,a.height,U(-1).value,2)
        if not image: raise RuntimeError('Window capture failed')
        im = image.contents
        assert im.bpp == 32 and im.byte_order == 0 and (im.red,im.green,im.blue)==(0xff0000,0xff00,0xff)
        raw = C.string_at(im.data,im.stride*im.height)
        rows = bytearray()
        for y in range(im.height):
            row = raw[y*im.stride:y*im.stride+im.width*4]
            rows.append(0)
            for at in range(0,len(row),4): rows.extend((row[at+2],row[at+1],row[at]))
        def chunk(kind,data): return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
        Path(path).write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',im.width,im.height,8,2,0,0,0))+
                               chunk(b'IDAT',zlib.compress(rows))+chunk(b'IEND',b''))
        self.x.XDestroyImage(image)

    def click(self, x, y):
        self.xt.XTestFakeMotionEvent(self.display,-1,x,y,0)
        self.xt.XTestFakeButtonEvent(self.display,1,True,0)
        self.xt.XTestFakeButtonEvent(self.display,1,False,0)
        self.x.XFlush(self.display)

    def key(self, name, down=True):
        code = self.x.XKeysymToKeycode(self.display,self.x.XStringToKeysym(name.encode()))
        self.xt.XTestFakeKeyEvent(self.display,code,down,0); self.x.XFlush(self.display)

    def chord(self, *names):
        for name in names: self.key(name)
        for name in reversed(names): self.key(name,False)
