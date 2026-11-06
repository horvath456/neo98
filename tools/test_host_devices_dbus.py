#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise Linux host-device actions against private NetworkManager and UDisks services."""
if __name__ == '__main__':
    import sys as _sys
    if not _sys.platform.startswith('linux'):
        print('SKIP: tests the D-Bus services of Linux')  # Linux only; skipped elsewhere
        _sys.exit(77)
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
from native_app import ROOT
import neo98


def serve(address, directory, ready, report):
    from gi.repository import Gio, GLib
    bus = Gio.DBusConnection.new_for_address_sync(address,
        Gio.DBusConnectionFlags.AUTHENTICATION_CLIENT | Gio.DBusConnectionFlags.MESSAGE_BUS_CONNECTION, None, None)
    for name in ('org.freedesktop.NetworkManager', 'org.freedesktop.UDisks2'):
        bus.call_sync('org.freedesktop.DBus', '/org/freedesktop/DBus', 'org.freedesktop.DBus', 'RequestName',
            GLib.Variant('(su)', (name, 0)), None, Gio.DBusCallFlags.NONE, 5000, None)
    nm = '/org/freedesktop/NetworkManager'
    device, saved, second, ip4 = nm+'/Devices/1', nm+'/Settings/1', nm+'/Devices/2', nm+'/IP4Config/1'
    drive, block = '/org/freedesktop/UDisks2/drives/Test', '/org/freedesktop/UDisks2/block_devices/sdz1'
    calls = []
    state = {'enabled': True, 'mounted': False}
    def record(name):
        calls.append(name)
        Path(report).write_text(json.dumps(calls))
    def prop(kind, value):
        return GLib.Variant(kind, value)
    properties = {
        (nm, 'org.freedesktop.NetworkManager'): {'Devices': prop('ao', [device, second]), 'WirelessEnabled': prop('b', True)},
        (device, 'org.freedesktop.NetworkManager.Device'): {'DeviceType': prop('u', 2), 'State': prop('u', 100), 'ActiveConnection': prop('o', nm+'/ActiveConnection/1'),
                                                            'Interface': prop('s', 'wlp1s0'), 'Ip4Config': prop('o', ip4)},
        (device, 'org.freedesktop.NetworkManager.Device.Wireless'): {'ActiveAccessPoint': prop('o', nm+'/AccessPoint/0'), 'AccessPoints': prop('ao', [nm+'/AccessPoint/'+str(i) for i in range(6)]),
                                                                     'Bitrate': prop('u', 866700)},
        (ip4, 'org.freedesktop.NetworkManager.IP4Config'): {'AddressData': prop('aa{sv}', [{'address': GLib.Variant('s', '10.0.0.42'), 'prefix': GLib.Variant('u', 24)}])},
        (second, 'org.freedesktop.NetworkManager.Device'): {'DeviceType': prop('u', 2), 'State': prop('u', 30), 'ActiveConnection': prop('o', '/'), 'Interface': prop('s', 'wlx0011')},
        (second, 'org.freedesktop.NetworkManager.Device.Wireless'): {'ActiveAccessPoint': prop('o', '/'), 'AccessPoints': prop('ao', [])},
        (drive, 'org.freedesktop.UDisks2.Drive'): {'ConnectionBus': prop('s', 'usb'), 'Removable': prop('b', True), 'Ejectable': prop('b', True), 'Optical': prop('b', False)},
    }
    for i, name in enumerate(('Active', 'Saved', 'Secure', 'Open', 'Enterprise', 'WEP')):
        properties[(nm+'/AccessPoint/'+str(i), 'org.freedesktop.NetworkManager.AccessPoint')] = {
            'Ssid': prop('ay', ('NEO98 '+name).encode()), 'Strength': prop('y', 80-i),
            'Flags': prop('u', int(i in (2, 4, 5))), 'WpaFlags': prop('u', 0), 'RsnFlags': prop('u', 0x200 if i==4 else 0x100 if i==2 else 0)}
    def methods(path, interface, method, args):
        if method == 'ListConnections': return '(ao)', ([saved],)
        if method == 'GetSettings': return '(a{sa{sv}})', ({'802-11-wireless': {'ssid': prop('ay', b'NEO98 Saved')}},)
        if method == 'RequestScan': record('scan');return '()', ()
        if method == 'Disconnect': record('disconnect');return '()', ()
        if method == 'Delete': record('forget');return '()', ()
        if method == 'ActivateConnection':
            assert args == (saved, device, nm+'/AccessPoint/1')
            record('saved');return '(o)', (nm+'/ActiveConnection/2',)
        if method == 'AddAndActivateConnection2':
            config, dev, ap, options = args
            assert dev == device and config['connection']['type']=='802-11-wireless'
            ssid = bytes(config['802-11-wireless']['ssid'])
            if ssid == b'NEO98 Hidden':
                assert ap == '/' and config['802-11-wireless']['hidden'] is True
                assert config['802-11-wireless-security']=={'key-mgmt': 'wpa-psk', 'psk': 'hiddenkey1'} and options['persist']=='disk'
                record('hidden_saved')
            elif ssid == b'NEO98 Secure':
                assert config['802-11-wireless-security']=={'key-mgmt': 'wpa-psk', 'psk': 'prüfpasswort'}
                assert options['persist']=='disk'
                record('secure_utf8_saved')
            else:
                assert ssid == b'NEO98 Open' and '802-11-wireless-security' not in config and options['persist']=='volatile'
                record('open_temporary')
            return '(ooa{sv})', (nm+'/Settings/2', nm+'/ActiveConnection/2', {})
        if method == 'GetManagedObjects':
            def objects(bus_type, stem):
                return {
                    stem: {'org.freedesktop.UDisks2.Block': {'Drive': prop('o', drive if bus_type=='usb' else drive+'Internal'), 'IdLabel': prop('s', 'Test medium')},
                           'org.freedesktop.UDisks2.Filesystem': {'MountPoints': prop('aay', [os.fsencode(directory)+b'\0'] if state['mounted'] else [])}},
                    drive if bus_type=='usb' else drive+'Internal': {'org.freedesktop.UDisks2.Drive': {'ConnectionBus': prop('s', bus_type), 'Removable': prop('b', bus_type=='usb'), 'Ejectable': prop('b', True)}}}
            return '(a{oa{sa{sv}}})', ({**objects('usb',block), **objects('ata',block+'Internal')},)
        if method == 'Mount':
            state['mounted']=True;record('mount');return '(s)', (directory,)
        if method == 'Unmount':
            state['mounted']=False;record('unmount');return '()', ()
        if method == 'Eject': record('eject');return '()', ()
        raise AssertionError(method)
    def invoke(connection, sender, path, iface, method, parameters, invocation):
        try:
            signature, values = methods(path, iface, method, parameters.unpack())
            invocation.return_value(GLib.Variant(signature,values))
        except BaseException as error:
            # Never include method arguments, which may contain credentials.
            record('FAIL '+method+' '+type(error).__name__)
            invocation.return_dbus_error('org.neo98.TestFailure', 'Test assertion failed')
    def get_property(connection,sender,path,iface,name): return properties[(path,iface)][name]
    def set_property(connection,sender,path,iface,name,value):
        assert name=='WirelessEnabled'
        properties[(path,iface)][name]=value;record('radio_on' if value.unpack() else 'radio_off');return True
    def register(path, iface, declarations):
        props=''.join(f'<property name="{name}" type="{value.get_type_string()}" access="{"readwrite" if name=="WirelessEnabled" else "read"}"/>' for name,value in properties.get((path,iface),{}).items())
        info=Gio.DBusNodeInfo.new_for_xml(f'<node><interface name="{iface}">{declarations}{props}</interface></node>')
        bus.register_object(path,info.interfaces[0],invoke,get_property,set_property)
    def method(name, ins='', outs=''):
        return '<method name="'+name+'">'+''.join(f'<arg type="{sig}" direction="{direction}"/>' for direction,sigs in (('in',ins.split()),('out',outs.split())) for sig in sigs)+'</method>'
    register(nm,'org.freedesktop.NetworkManager',method('ActivateConnection','o o o','o')+method('AddAndActivateConnection2','a{sa{sv}} o o a{sv}','o o a{sv}'))
    register(device,'org.freedesktop.NetworkManager.Device',method('Disconnect'))
    register(device,'org.freedesktop.NetworkManager.Device.Wireless',method('RequestScan','a{sv}'))
    register(ip4,'org.freedesktop.NetworkManager.IP4Config','')
    register(second,'org.freedesktop.NetworkManager.Device','')
    register(second,'org.freedesktop.NetworkManager.Device.Wireless','')
    for i in range(6): register(nm+'/AccessPoint/'+str(i),'org.freedesktop.NetworkManager.AccessPoint','')
    register(nm+'/Settings','org.freedesktop.NetworkManager.Settings',method('ListConnections','','ao'))
    register(saved,'org.freedesktop.NetworkManager.Settings.Connection',method('GetSettings','','a{sa{sv}}')+method('Delete'))
    register('/org/freedesktop/UDisks2','org.freedesktop.DBus.ObjectManager',method('GetManagedObjects','','a{oa{sa{sv}}}'))
    register(block,'org.freedesktop.UDisks2.Filesystem',method('Mount','a{sv}','s')+method('Unmount','a{sv}'))
    register(drive,'org.freedesktop.UDisks2.Drive',method('Eject','a{sv}'))
    Path(ready).touch()
    GLib.MainLoop().run()


def main():
    out=ROOT/'build/native-linux/host-devices';out.mkdir(parents=True,exist_ok=True)
    env=neo98.tool_environment()
    flags=shlex.split(subprocess.check_output(['pkg-config','--cflags','--libs','gio-2.0','sdl2'],text=True))
    client=out/'dbus-test'
    subprocess.run(['clang++','-std=c++17','-O1',str(ROOT/'tools/test_host_devices_dbus.cpp'),str(ROOT/'tools/host_devices/linux.cpp'),*flags,'-o',str(client)],env=env,check=True)
    with tempfile.TemporaryDirectory(prefix='neo98-devices-') as temporary:
        temp=Path(temporary);ready=temp/'ready';report=temp/'calls.json';medium=temp/'medium';medium.mkdir()
        daemon=subprocess.Popen(['dbus-daemon','--session','--nofork','--print-address=1'],stdout=subprocess.PIPE,text=True)
        try:
            address=daemon.stdout.readline().strip()
            server=subprocess.Popen(['/usr/bin/python3',__file__,'--serve',address,str(medium),str(ready),str(report)])
            try:
                import time
                until=time.monotonic()+5
                while not ready.exists() and time.monotonic()<until:
                    assert server.poll() is None;time.sleep(.02)
                assert ready.exists()
                result=subprocess.run([str(client),str(medium)],env=dict(os.environ,DBUS_SYSTEM_BUS_ADDRESS=address),capture_output=True,text=True,timeout=25)
                assert result.returncode==0,result.stdout+result.stderr
                calls=json.loads(report.read_text())
                assert calls==['scan','disconnect','radio_off','radio_on','saved','secure_utf8_saved','open_temporary','forget','hidden_saved','mount','unmount','eject'],calls
                (out/'dbus-results.json').write_text(json.dumps({'private_bus':True,'calls':calls,'battery_mapping':True,'utf8':True},indent=2)+'\n')
                print(result.stdout,end='')
            finally:
                server.terminate();server.wait(timeout=5)
        finally:
            daemon.terminate();daemon.wait(timeout=5)

if __name__=='__main__':
    if len(sys.argv)>1 and sys.argv[1]=='--serve':serve(*sys.argv[2:])
    else:main()
