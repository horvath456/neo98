// SPDX-License-Identifier: GPL-2.0-or-later
// The preferred devices of the wave mapper and the volume the multimedia
// page of the Control Panel shows (tools/test_sound_mapper_host.py).
//
//   MAPPER.EXE forget  removes "Playback" and "Record" of
//                      HKCU\Software\Microsoft\Multimedia\Sound Mapper
//   MAPPER.EXE foreign sets "Playback" to the name of a device of another
//                      machine
//   MAPPER.EXE lower   sets the wave volume to 4000h through the mixer
//   MAPPER.EXE applet  starts the multimedia page and waits for it, then as
//                      read
//   MAPPER.EXE read    what is stored there, the names of the first output
//                      and input device, and the values of the volume
//                      controls of the output device's mixer: the speakers'
//                      destination (master) and the wave line under it,
//                      which the multimedia page moves
// Each writes C:\MAPPER.TXT.
#include <windows.h>
#include <mmsystem.h>

// The structures are cleared with memset, which -nostdlib leaves out.
void *memset(void *p, int v, unsigned n)
{
    unsigned char *q = p;
    while (n--)
        *q++ = (unsigned char)v;
    return p;
}

static char text[1024];
static int used;

static void stored(const char *label, const char *name)
{
    char value[80];
    DWORD size = sizeof value;
    HKEY key;
    lstrcpyA(value, "absent");
    if (!RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Microsoft\\Multimedia\\Sound Mapper", &key))
    {
        if (RegQueryValueExA(key, name, 0, 0, (BYTE *)value, &size))
            lstrcpyA(value, "absent");
        RegCloseKey(key);
    }
    used += wsprintfA(text + used, "%s=%s\r\n", label, value);
}

static void volume(const char *label, DWORD component, DWORD set)
{
    UINT id = 0;
    HMIXER mixer = 0;
    MIXERLINEA line = {sizeof line};
    MIXERCONTROLA control = {sizeof control};
    MIXERLINECONTROLSA controls = {sizeof controls};
    MIXERCONTROLDETAILS details = {sizeof details};
    MIXERCONTROLDETAILS_UNSIGNED value[2] = {{0}};
    if (mixerGetID((HMIXEROBJ)0, &id, MIXER_OBJECTF_WAVEOUT) || mixerOpen(&mixer, id, 0, 0, 0))
    {
        used += wsprintfA(text + used, "%s=no mixer\r\n", label);
        return;
    }
    line.dwComponentType = component;
    controls.dwControlType = MIXERCONTROL_CONTROLTYPE_VOLUME;
    controls.cControls = 1;
    controls.cbmxctrl = sizeof control;
    controls.pamxctrl = &control;
    details.cbDetails = sizeof value[0];
    details.paDetails = value;
    if (!mixerGetLineInfoA((HMIXEROBJ)mixer, &line, MIXER_OBJECTF_HMIXER | MIXER_GETLINEINFOF_COMPONENTTYPE))
    {
        controls.dwLineID = line.dwLineID;
        if (!mixerGetLineControlsA((HMIXEROBJ)mixer, &controls,
                                   MIXER_OBJECTF_HMIXER | MIXER_GETLINECONTROLSF_ONEBYTYPE))
        {
            details.dwControlID = control.dwControlID;
            details.cChannels = 1;
            if (set)
            {
                value[0].dwValue = set;
                mixerSetControlDetails((HMIXEROBJ)mixer, &details, MIXER_OBJECTF_HMIXER);
            }
            if (!mixerGetControlDetailsA((HMIXEROBJ)mixer, &details, MIXER_OBJECTF_HMIXER))
            {
                used += wsprintfA(text + used, "%s=%lu\r\n", label, value[0].dwValue);
                mixerClose(mixer);
                return;
            }
        }
    }
    used += wsprintfA(text + used, "%s=no control\r\n", label);
    mixerClose(mixer);
}

void mainCRTStartup(void)
{
    const char *command = GetCommandLineA();
    int n = lstrlenA(command);
    DWORD written = 0;
    HANDLE file;
    WAVEOUTCAPSA out = {0};
    WAVEINCAPSA in = {0};
    if (n > 6 && !lstrcmpA(command + n - 6, "forget"))
    {
        HKEY key;
        if (!RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Microsoft\\Multimedia\\Sound Mapper", &key))
        {
            RegDeleteValueA(key, "Playback");
            RegDeleteValueA(key, "Record");
            RegCloseKey(key);
        }
    }
    if (n > 7 && !lstrcmpA(command + n - 7, "foreign"))
    {
        HKEY key;
        if (!RegOpenKeyA(HKEY_CURRENT_USER, "Software\\Microsoft\\Multimedia\\Sound Mapper", &key))
        {
            RegSetValueExA(key, "Playback", 0, REG_SZ, (const BYTE *)"SB16 Wave Out [220]", 20);
            RegCloseKey(key);
        }
    }
    if (n > 6 && !lstrcmpA(command + n - 6, "applet"))
    {
        STARTUPINFOA start = {sizeof start};
        PROCESS_INFORMATION child;
        char line[] = "RUNDLL32.EXE SHELL32.DLL,Control_RunDLL MMSYS.CPL";
        if (CreateProcessA(0, line, 0, 0, 0, 0, 0, 0, &start, &child))
        {
            WaitForSingleObject(child.hProcess, 60000);
            CloseHandle(child.hProcess);
            CloseHandle(child.hThread);
        }
    }
    stored("playback", "Playback");
    stored("record", "Record");
    waveOutGetDevCapsA(0, &out, sizeof out);
    used += wsprintfA(text + used, "output=%s\r\n", out.szPname);
    used += wsprintfA(text + used, "inputs=%u\r\n", waveInGetNumDevs());
    if (waveInGetNumDevs())
    {
        waveInGetDevCapsA(0, &in, sizeof in);
        used += wsprintfA(text + used, "input=%s\r\n", in.szPname);
    }
    volume("master", MIXERLINE_COMPONENTTYPE_DST_SPEAKERS, 0);
    volume("wave", MIXERLINE_COMPONENTTYPE_SRC_WAVEOUT, n > 5 && !lstrcmpA(command + n - 5, "lower") ? 0x4000 : 0);
    file = CreateFileA("C:\\MAPPER.TXT", GENERIC_WRITE, 0, 0, CREATE_ALWAYS, 0, 0);
    WriteFile(file, text, used, &written, 0);
    CloseHandle(file);
    ExitProcess(0);
}
