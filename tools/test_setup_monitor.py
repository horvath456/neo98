#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check setup's HMP client against fragmented terminal replies."""
from pathlib import Path
import socket
import tempfile
import threading
import time
import setup_win98


def main():
    with tempfile.TemporaryDirectory(prefix='neo98 monitor ') as temporary:
        root=Path(temporary);monitor=root/'monitor.sock';screen=root/'screen.ppm'
        commands=[];errors=[]
        server=socket.socket(socket.AF_UNIX);server.bind(str(monitor));server.listen();server.settimeout(5)
        def send(connection,data):
            for part in (data[:1],data[1:5],data[5:-2],data[-2:]):
                connection.sendall(part);time.sleep(.01)
        def serve():
            try:
                for _ in range(2):
                    connection,_=server.accept()
                    with connection:
                        connection.settimeout(5);send(connection,b'QEMU monitor\r\n(qemu) ')
                        while True:
                            line=bytearray()
                            while not line.endswith(b'\n'):
                                part=connection.recv(1)
                                if not part:break
                                line.extend(part)
                            if not line:break
                            command=line.decode().strip();commands.append(command)
                            # The command echo alone must not complete the read.
                            send(connection,line[:-1]);time.sleep(.02)
                            if command.startswith('screendump '):screen.write_bytes(b'P6\n32 32\n255\n'+bytes(32*32*3))
                            send(connection,b'\r\n(qemu) ')
            except Exception as error:errors.append(error)
            finally:server.close()
        thread=threading.Thread(target=serve,daemon=True);thread.start()
        setup_win98.capture_screen(monitor,screen)
        assert screen.exists() and screen.stat().st_size>1000
        setup_win98.send_keys(monitor,'tab','ret');thread.join(5)
        assert not thread.is_alive() and not errors,errors
        assert commands==[f'screendump "{screen}"','sendkey tab','sendkey ret'],commands
    left,right=socket.socketpair();right.close()
    try:
        try:setup_win98.monitor_reply(left)
        except ConnectionError:pass
        else:raise AssertionError('Truncated monitor reply was accepted')
    finally:left.close()
    print('SETUP MONITOR PASS: fragmented greeting, command echoes, screenshots, keys and early disconnect')

if __name__=='__main__':main()
