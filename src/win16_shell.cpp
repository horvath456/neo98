// SPDX-License-Identifier: GPL-2.0-or-later
// The SHELL, KEYBOARD and WIN87EM gates of the 16-bit side, PLAN.md step 9,
// third part.
// These three modules are ours by the rule of PLAN.md section 0: everything
// that is 16-bit code under Win98 is native ARM64 code here. The gates have
// the same shape as the USER and GDI gates of src/win16_user.cpp: a Pascal
// frame comes in, the native service behind it answers, and the answer leaves
// in DX:AX.
// There are no success stubs. An ordinal this port does not carry stops the
// run and names itself, and so does a WIN87EM subfunction that is not one of
// the ones a coprocessor makes trivial.
//
#include "win16_machine.h"
#include "win98_win16_user.h"
#include "win98_win87em.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
namespace Win16U = Win98Win16User;

// ------------------------------------------------------------------ KEYBOARD
// KEYBOARD.DRV of the tree exports these under the names the resident name
// table carries: 5 ANSITOOEM, 6 OEMTOANSI, 131 MAPVIRTUALKEY, 133
// GETKEYNAMETEXT.
bool Application::Win16KeyboardCall(unsigned ordinal, const Win16Frame &frame, unsigned &result,
                                    unsigned &words)
{
    switch (ordinal)
    {
    case 5: // AnsiToOem(ansi, oem)
    case 6: // OemToAnsi(oem, ansi)
    {
        words = 4;
        const unsigned source = frame.Pointer(2), target = frame.Pointer(0);
        const unsigned a[] = {source, target};
        result = source && target ? Api(ordinal == 5 ? "CharToOemA" : "OemToCharA", a) : 0;
        return true;
    }
    case 134: // AnsiToOemBuff(source, target, length)
    case 135: // OemToAnsiBuff(source, target, length). Both frames are the
              // same argument layout: a far source at bp+0Ch, a far
              // target at bp+8 and the count at bp+6, and the two differ only
              // in the translation table, 848h against 8E8h.
    {
        words = 5;
        const unsigned source = frame.Pointer(3), target = frame.Pointer(1),
                       length = frame.Word(0);
        const unsigned a[] = {source, target, length};
        result = source && target ? Api(ordinal == 134 ? "CharToOemBuffA" : "OemToCharBuffA", a) : 0;
        return true;
    }
    case 131: // MapVirtualKey(code, kind)
    {
        words = 2;
        const unsigned a[] = {frame.Word(1), frame.Word(0)};
        result = Api("MapVirtualKeyA", a);
        return true;
    }
    case 133: // GetKeyNameText(lparam, buffer, size)
    {
        words = 5;
        const unsigned a[] = {frame.Long(3), frame.Pointer(1), frame.Word(0)};
        result = Api("GetKeyNameTextA", a);
        return true;
    }
    default:
        break;
    }
    char text[160];
    snprintf(text, sizeof text, "Win16 KEYBOARD ordinal %u is not ported yet", ordinal);
    Fail(text);
    return false;
}

// --------------------------------------------------------------------- SHELL
// SHELL.DLL of the tree, by the names of its resident name table. The entries
// this port carries are the ones the programs of the tree reach: the program
// start, the icons of a program group and the drop target.
bool Application::Win16ShellCall(unsigned ordinal, const Win16Frame &frame, unsigned &result,
                                 unsigned &words)
{
    switch (ordinal)
    {
    // The registry of Windows 3.1, which SHELL.DLL carries for 16-bit code:
    // ordinals 1 to 7. A key is a doubleword; HKEY_CLASSES_ROOT is 1 there
    // and 80000000h in Win32, and SHELL of Win98 takes both. A value is the
    // default string of a key. AVIFILE reads the handlers of file types and
    // stream types this way (HKEY_CLASSES_ROOT\AVIFile).
    case 1: // RegOpenKey(key, subkey, result)
    case 2: // RegCreateKey(key, subkey, result)
    {
        words = 6;
        const unsigned key = frame.Long(4);
        const unsigned a[] = {key == 1 ? 0x80000000u : key, frame.Pointer(2), frame.Pointer(0)};
        result = Api(ordinal == 1 ? "RegOpenKeyA" : "RegCreateKeyA", a);
        return true;
    }
    case 3: // RegCloseKey(key)
    {
        words = 2;
        const unsigned key = frame.Long(0);
        const unsigned a[] = {key};
        result = key == 1 || key == 0x80000000u ? 0u : Api("RegCloseKey", a);
        return true;
    }
    case 4: // RegDeleteKey(key, subkey)
    {
        words = 4;
        const unsigned key = frame.Long(2);
        const unsigned a[] = {key == 1 ? 0x80000000u : key, frame.Pointer(0)};
        result = Api("RegDeleteKeyA", a);
        return true;
    }
    case 5: // RegSetValue(key, subkey, type, data, bytes)
    {
        words = 10;
        const unsigned key = frame.Long(8);
        const unsigned a[] = {key == 1 ? 0x80000000u : key, frame.Pointer(6), frame.Long(4), frame.Pointer(2), frame.Long(0)};
        result = Api("RegSetValueA", a);
        return true;
    }
    case 6: // RegQueryValue(key, subkey, value, bytes)
    {
        words = 8;
        const unsigned key = frame.Long(6);
        const unsigned a[] = {key == 1 ? 0x80000000u : key, frame.Pointer(4), frame.Pointer(2), frame.Pointer(0)};
        result = Api("RegQueryValueA", a);
        return true;
    }
    case 7: // RegEnumKey(key, index, name, bytes)
    {
        words = 8;
        const unsigned key = frame.Long(6);
        const unsigned a[] = {key == 1 ? 0x80000000u : key, frame.Long(4), frame.Pointer(2), frame.Long(0)};
        result = Api("RegEnumKeyA", a);
        return true;
    }
    case 9: // DragAcceptFiles(hwnd, accept)
    {
        words = 2;
        const unsigned a[] = {frame.Word(1), frame.Word(0)};
        result = Api("DragAcceptFiles", a);
        return true;
    }
    case 20: // ShellExecute(hwnd, operation, file, parameters, directory, show)
    {
        words = 10;
        const unsigned a[] = {frame.Word(9),    frame.Pointer(7), frame.Pointer(5),
                              frame.Pointer(3), frame.Pointer(1), frame.Word(0)};
        result = Api("ShellExecuteA", a);
        return true;
    }
    case 34: // ExtractIcon(instance, file, index)
    {
        // A native icon handle carries a tag and a generation above bit 15, and
        // the gate epilogue puts only the low word in AX. Without the WORD
        // alias the 16-bit caller got a handle DrawIcon and DestroyIcon could
        // not resolve, so the icon was never drawn and never freed.
        words = 4;
        const unsigned a[] = {frame.Word(3), frame.Pointer(1), frame.Word(0)};
        const unsigned native = Api("ExtractIconA", a);
        if (native <= 1)
        {
            result = native;
            return true;
        }
        result = Win16ImageAlias(native, frame.Word(3), false, 0, nullptr);
        if (!result)
        {
            const unsigned destroy[] = {native};
            Api("DestroyIcon", destroy);
        }
        return true;
    }
    case 40: // ExtractIconExA(file, index, large, small, count)
    {
        // SHELL.DLL goes through Shl1632 (entry 8) to SHELL32's
        // ExtractIconExA; the arrays hold 16-bit handles, one word each.
        // SETUPX draws the class icons of the device installer with it. With both arrays null the answer is the number of
        // icons in the file.
        words = 8;
        const unsigned file = frame.Pointer(6), large = frame.Pointer(3), small = frame.Pointer(1);
        const int index = (short)frame.Word(5);
        const unsigned count = frame.Word(0);
        const unsigned wide = count && count <= 64 ? Allocate(count * 8) : 0;
        if (count && !wide)
        {
            result = 0;
            return true;
        }
        for (unsigned i = 0; wide && i < count * 2; ++i)
            Write32(wide + i * 4, 0);
        const unsigned a[] = {file, (unsigned)index, large ? wide : 0u, small ? wide + count * 4 : 0u, count};
        result = Api("ExtractIconExA", a) & 0xffff;
        for (unsigned i = 0; wide && i < count; ++i)
        {
            const unsigned places[2] = {large, small};
            for (unsigned k = 0; k < 2; ++k)
            {
                if (!places[k])
                    continue;
                const unsigned native = Read32(wide + k * count * 4 + i * 4);
                unsigned alias = native ? Win16ImageAlias(native, 0, false, 0, nullptr) : 0;
                if (native && !alias)
                {
                    const unsigned destroy[] = {native};
                    Api("DestroyIcon", destroy);
                }
                Win16Word(places[k] + i * 2, alias);
            }
        }
        if (wide)
            Free(wide);
        return true;
    }
    case 102: // RegisterShellHook(hwnd, flags)
    {
        // SHELL.DLL's own routine, implemented in src/win98_shell.h and
        // reached by the 32-bit SHELL32 thunk as well (x86_flat_thunk.cpp):
        // the window goes into the list and hears HSHELL_WINDOWCREATED and
        // HSHELL_WINDOWDESTROYED through the registered message.
        words = 2;
        result = RegisterShell(frame.Word(1), frame.Word(0)) & 0xffff;
        return true;
    }
    case 157: // RestartDialog(hwnd, prompt, flags)
    {
        // SHELL.DLL hands this up to SHELL32 ordinal 59: "You must restart
        // your computer before the new settings will take effect. Do you
        // want to restart your computer now?", behind the caller's own
        // prompt. The Device Manager asks so after "Disable in this hardware
        // profile". Yes restarts through SHELL32's own way (ExitWindows,
        // src/x86_flat_thunk.cpp); the answer is IDYES or IDNO.
        words = 5;
        const unsigned prompt = frame.Long(2);
        const unsigned a[] = {frame.Word(4), prompt >> 16 ? frame.Pointer(2) : 0u, frame.Long(0)};
        bool found = false;
        result = Win16Shell32(nullptr, 59, a, 3, found);
        if (!found)
            result = 7; // IDNO
        return state != Failed;
    }
    // The entries SHELL.DLL hands to SHELL32 through Shl1632 (
    // and Windows 98 load CX = 10h, 14h and 1Ch for the target table, as
    // ShellExecute does with 18h). SHELL32 runs as the original, so the gate
    // calls the same export. The word counts are the SDK signatures; the
    // thunk keeps them on the 32-bit side.
    case 21: // FindExecutable(file, directory, result)
    {
        words = 6;
        const unsigned a[] = {frame.Pointer(4), frame.Pointer(2), frame.Pointer(0)};
        bool found = false;
        result = Win16Shell32("FindExecutableA", 0, a, 3, found);
        if (!found)
            result = 31; // SE_ERR_NOASSOC
        return state != Failed;
    }
    case 22: // ShellAbout(hwnd, application, other, icon)
    {
        words = 6;
        const unsigned a[] = {frame.Word(5), frame.Pointer(3), frame.Pointer(1), Win16ImageNative(frame.Word(0))};
        bool found = false;
        result = Win16Shell32("ShellAboutA", 0, a, 4, found);
        return state != Failed;
    }
    case 36: // ExtractAssociatedIcon(instance, path, index): the icon under
             // its word alias. The instance is a 16-bit one, which SHELL32's
             // icon extraction does not read.
    {
        words = 5;
        const unsigned a[] = {0, frame.Pointer(2), frame.Pointer(0)};
        bool found = false;
        const unsigned native = Win16Shell32("ExtractAssociatedIconA", 0, a, 3, found);
        result = native ? Win16ImageAlias(native, frame.Word(4), false, 0, nullptr) : 0u;
        if (native && !result)
        {
            const unsigned destroy[] = {native};
            Api("DestroyIcon", destroy);
        }
        return state != Failed;
    }
    case 37: // DoEnvironmentSubst(text, size): SHELL expands the
             // %NAME% of the DOS environment in place and answers the length
             // in AX and the success in DX, as SHELL32 does with the process
             // environment, which is the same block here.
    {
        words = 3;
        const unsigned a[] = {frame.Pointer(1), frame.Word(0)};
        bool found = false;
        result = Win16Shell32("DoEnvironmentSubstA", 0, a, 2, found);
        return state != Failed;
    }
    case 262: // DriveType(drive)
    {
        // SHELL.DLL sign-extends the drive number and passes it to
        // RealDriveType(drive, TRUE). It returns zero for numbers below zero
        // or at least 26, otherwise the GetDriveTypeA result for the drive root.
        // SHELL32 keeps that answer per drive; here it is asked each time.
        // 'A' to 'Z' themselves go to ordinal 525, which
        // answers the kept word with flag bits above the type; they are
        // taken like their number here, the flag bits are not ported.
        words = 1;
        int drive = (short)frame.Word(0);
        if (drive >= 'A' && drive <= 'Z')
            drive -= 'A';
        else if (drive < 0 || drive >= 26)
        {
            result = 0;
            return true;
        }
        char root[4] = {(char)('A' + drive), ':', '\\', 0};
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        Store(scratch, root, sizeof root);
        const unsigned a[] = {scratch};
        result = Api("GetDriveTypeA", a);
        return true;
    }
    default:
        // The page collector SHELL.DLL hands a 16-bit page function
        // (src/win16_property_sheet.cpp).
        if (Win16CollectorCall(ordinal, frame, result, words))
            return true;
        break;
    }
    char text[200];
    snprintf(text, sizeof text,
             "Win16 SHELL ordinal %u is not ported yet: PLAN.md step 9 carries the entries the "
             "programs of the tree reach",
             ordinal);
    Fail(text);
    return false;
}

// An export of the original SHELL32, by name or by ordinal, called on the
// flat stack of the task (Win16FlatEnter), the way SHELL.DLL's Shl1632 thunk
// reaches it. `found` is false when SHELL32 has no such export.
unsigned Application::Win16Shell32(const char *name, unsigned ordinal, const unsigned *a, unsigned count, bool &found)
{
    found = false;
    CpuContext *saved = nullptr;
    unsigned top = 0;
    if (!Win16FlatEnter(saved, top))
        return 0;
    const unsigned text = Allocate(64);
    unsigned procedure = 0;
    if (text)
    {
        PutString(text, "SHELL32.DLL");
        const unsigned module = Api("LoadLibraryA", &text);
        if (name)
            PutString(text, name);
        const unsigned query[] = {module, name ? text : ordinal};
        procedure = module ? Api("GetProcAddress", query) : 0;
        Free(text);
    }
    unsigned result = 0;
    if (procedure)
    {
        found = true;
        result = Call(procedure, a, count);
    }
    Win16FlatLeave(saved, top);
    return result;
}

// WIN87EM __FPMATH dispatches thirteen subfunctions selected by BX.
// Out-of-range requests return FFFFh. The virtual CPU has an x87 coprocessor,
// so reset, control-word, rounding, conversion, status, and stack-usage
// requests operate on its state. Only subfunctions 3, 7, and 10 change DX.
// Clearing exceptions preserves condition codes and the stack pointer.
// Arithmetic helpers live in src/win98_win87em.h.
bool Application::Win16MathClear()
{
    unsigned status = 0;
    if (!Check(cpu->Get(Reg::Fpsw, status)))
        return false;
    status &= ~0x80ffu;
    if (!Check(cpu->Set(Reg::Fpsw, status)))
        return false;
    win16.state->math_status = 0;
    return true;
}
// An unmasked x87 exception would be delivered at the next wait. The C
// runtime's 0x1332 control word leaves invalid-operation, divide-by-zero and
// overflow unmasked. Delivery is not implemented, so stop with a diagnostic
// rather than continue with an undefined result. The interrupt-3Dh path does
// the same.
bool Application::Win16MathPending(unsigned flags, unsigned control)
{
    const unsigned unmasked = flags & ~control & 0x3f;
    if (!unmasked)
        return true;
    char text[200];
    snprintf(text, sizeof text,
             "Win16 WIN87EM __FPMATH raised an unmasked floating-point exception %02xh (control word "
             "%04xh); exception delivery is not ported yet",
             unmasked, control);
    Fail(text);
    return false;
}
bool Application::Win16MathCall(unsigned ordinal, const Win16Frame &frame, unsigned &result,
                                unsigned &words)
{
    (void)frame;
    words = 0;
    // Preserve caller DX when a subfunction does not write it, because the
    // gate takes DX from the result's upper word.
    const unsigned keep_dx = (Register(Reg::Edx) & 0xffff) << 16;
    result = keep_dx;
    if (ordinal != 1)
    {
        char text[200];
        snprintf(text, sizeof text,
                 "Win16 WIN87EM ordinal %u is not ported yet: the guest has a coprocessor, so only "
                 "__FPMATH is carried",
                 ordinal);
        Fail(text);
        return false;
    }
    const unsigned subfunction = Register(Reg::Ebx) & 0xffff;
    // only stores AX and returns, so subfunction 12 leaves AX of the
    // caller as it was. and Windows 98 answer AX = 0; measured on the
    // original with tools/win87em_original.py.
    if (subfunction == 12)
        result = keep_dx | (Register(Reg::Eax) & 0xffff);
    switch (subfunction)
    {
    case 0: // the per-instance start
    case 1: // the state reset
    case 2: // the per-instance end, which begins with the same reset
    {
        // Reset the coprocessor with extended precision and round-to-nearest,
        // leaving invalid, divide-by-zero, and overflow exceptions unmasked
        // as expected by the Microsoft C runtime. Apply the same FF3Ch
        // control-word mask as subfunction 4 and clear collected exception
        // state.
        win16.state->math_control = Win87ControlWord;
        const unsigned loaded = Win87ControlWord & 0xff3c;
        Check(cpu->Set(Reg::Fpcw, loaded));
        return Win16MathClear();
    }
    case 3: // the interrupt vector of the emulator: DOS 25h on 3Eh
    {
        // Register interrupt 3Eh regardless of coprocessor state, as DOS3Call
        // 25h registers vectors. Preserve the previous AX in DX; setting the
        // vector leaves both values unchanged.
        const unsigned offset = Register(Reg::Eax) & 0xffff;
        Register(Reg::Eax, 0x253e);
        unsigned answer = 0;
        if (!Win16DosCall(answer))
            return false;
        result = (offset << 16) | 0x253e;
        return true;
    }
    case 4: // keep the requested word, load AX & FF3Ch into x87.
    {
        win16.state->math_control = Register(Reg::Eax) & 0xffff;
        const unsigned loaded = win16.state->math_control & 0xff3c;
        result = keep_dx | loaded;
        Check(cpu->Set(Reg::Fpcw, loaded));
        return true;
    }
    case 5: // return the requested control word, including masked bits.
        result = keep_dx | win16.state->math_control;
        return true;
    case 6: // `frndint` with the rounding mode of AX bits 10 and 11.
    case 7: // `fistp dword` with that rounding mode; DX:AX is the value.
    {
        // saves the control word, replaces only its rounding
        // bits, and Windows 98 loads the saved word back. The answer of
        // subfunction 6 is the control word that was used; subfunction 7
        // answers its integer instead.
        unsigned control = 0;
        Check(cpu->Get(Reg::Fpcw, control));
        control &= 0xffff;
        const unsigned temporary =
            (control & ~0x0c00u) | (Register(Reg::Eax) & 0x0c00u);
        Win98Win87Em::Extended value;
        bool empty = true;
        if (!Win98Win87Em::Load(cpu, value, empty))
        {
            Fail("Win16 WIN87EM could not read the top of the x87 stack");
            return false;
        }
        if (empty)
        {
            // The x87 stack is empty. Stack-underflow exception delivery is
            // not implemented.
            Fail("Win16 WIN87EM __FPMATH found the x87 stack empty; stack underflow delivery is not "
                 "ported yet");
            return false;
        }
        if (subfunction == 6)
        {
            const unsigned flags = Win98Win87Em::RoundToInteger(value, temporary);
            if (!Win98Win87Em::Store(cpu, value) || !Win98Win87Em::Raise(cpu, flags))
            {
                Fail("Win16 WIN87EM could not write the rounded value back");
                return false;
            }
            result = keep_dx | temporary;
            return Win16MathPending(flags, control);
        }
        unsigned integer = 0;
        const unsigned flags = Win98Win87Em::ToInteger32(value, temporary, integer);
        if (!Win98Win87Em::Pop(cpu) || !Win98Win87Em::Raise(cpu, flags))
        {
            Fail("Win16 WIN87EM could not pop the x87 stack");
            return false;
        }
        result = integer;
        return Win16MathPending(flags, control);
    }
    case 8: // the six exception bits of the status word, collected in
            // the word and masked with 1FFFh.
    {
        unsigned status = 0;
        Check(cpu->Get(Reg::Fpsw, status));
        win16.state->math_status = (win16.state->math_status | (status & 0x3f)) & 0x1fff;
        result = keep_dx | win16.state->math_status;
        return true;
    }
    case 9: // clearing the exception state
        return Win16MathClear();
    case 10: // `fnstenv`, then eight two-bit fields of the tag word.
             // With a coprocessor has made the emulator's own stack
             // words equal, so the answer is the number of occupied registers.
    {
        unsigned count = 0;
        if (!Win98Win87Em::Depth(cpu, count))
        {
            Fail("Win16 WIN87EM could not read the x87 tag word");
            return false;
        }
        result = keep_dx | count;
        return true;
    }
    case 11: // Return the sign-extended coprocessor byte as a word.
             // The guest has a coprocessor, so it is one.
        result = keep_dx | 1;
        return true;
    case 12: // Store a word of emulator state that the caller
             // sets and only the emulation reads. Nothing of the coprocessor
             // path looks at it again.
        return true;
    default:
        break;
    }
    char text[200];
    snprintf(text, sizeof text,
             "Win16 WIN87EM __FPMATH subfunction %u (BX) is not ported yet",
             subfunction);
    Fail(text);
    return false;
}

// ----------------------------------------------------------------- MMSYSTEM
// MMSYSTEM.DLL is the 16-bit half of the multimedia layer of Win98, so it is
// ours and its entries go to the native WINMM this port already has. The
// ordinals are the ones the resident name table of the tree's MMSYSTEM.DLL
// carries. Only entries with a native provider and without a 16-bit structure
// are here; a device entry that takes a WAVEFORMAT, a MIDIHDR or a callback
// stops the run with its name.
// The device entries of the wave, MIDI, auxiliary and joystick groups are not
// here. They carry structures whose 16-bit shape is not converted and a
// callback that is a far pointer, and a device that answers without playing is
// a success stub. They stop the run with their name.
bool Application::Win16MultimediaCall(unsigned ordinal, const Win16Frame &frame, unsigned &result,
                                      unsigned &words)
{
    switch (ordinal)
    {
    case 2: // sndPlaySound(sound, flags)
    {
        words = 3;
        // SND_ASYNC of the 16-bit side is the same bit as the 32-bit one. A
        // null name stops what is playing, so the pointer may be zero.
        const unsigned a[] = {frame.Pointer(1), frame.Word(0)};
        result = Api("sndPlaySoundA", a);
        return true;
    }
    // The device counters. They carry no structure and no callback: the
    // answer is a word, and the native WINMM of this port gives it. The
    // ordinals are the ones of the resident name table: 101 JOYGETNUMDEVS,
    // 201 MIDIOUTGETNUMDEVS, 301 MIDIINGETNUMDEVS, 350 AUXGETNUMDEVS,
    // 401 WAVEOUTGETNUMDEVS, 501 WAVEINGETNUMDEVS.
    case 101:
    case 201:
    case 301:
    case 350:
    case 401:
    case 501:
    {
        words = 0;
        const char *native = ordinal == 101   ? "joyGetNumDevs"
                             : ordinal == 201 ? "midiOutGetNumDevs"
                             : ordinal == 301 ? "midiInGetNumDevs"
                             : ordinal == 350 ? "auxGetNumDevs"
                             : ordinal == 401 ? "waveOutGetNumDevs"
                                              : "waveInGetNumDevs";
        result = Api(native, nullptr);
        return true;
    }
    case 402: // waveOutGetDevCaps(device, caps, bytes), MMSYSTEM segment
        words = 4;
        return Win16WaveOutGetDevCaps(frame, result);
    case 404: case 405: case 406: case 407: case 408: case 409: case 410: case 411: case 412: case 415: case 421:
        return Win16WaveOutCall(ordinal, frame, result, words);
    case 502: // waveInGetDevCaps(device, caps, bytes)
        words = 4;
        return Win16WaveInGetDevCaps(frame, result);
    case 503: // waveInGetErrorText(error, text, bytes)
    {
        words = 4;
        const unsigned a[] = {frame.Word(3), frame.Pointer(1), frame.Word(0)};
        result = Api("waveInGetErrorTextA", a);
        return true;
    }
    case 504: case 505: case 506: case 507: case 508: case 509: case 510: case 511: case 512: case 513: case 514:
        return Win16WaveOutCall(ordinal, frame, result, words);
    case 900: case 902: case 903: case 904: case 905:
    case 1120: case 1121: case 1122: case 1123: case 1124: case 1125:
        return Win16MmThreadCall(ordinal, frame, result, words);
    case 1210: case 1211: case 1212: case 1213: case 1214: case 1223: case 1224:
        return Win16MmioCall(ordinal, frame, result, words);
    case 31: case 705: case 707: case 708: case 710: case 711: case 713: case 1104:
        // The entries an MCI driver calls back (src/x86_mci.cpp).
        return MciHelperCall(ordinal, frame, result, words);
    case 607: // timeGetTime, MMSYSTEM segment 5 scalar timer entry
        words = 0;
        result = Api("timeGetTime", nullptr);
        return true;
    case 701: // mciSendCommand(device, message, flags, parameters)
    {
        words = 6;
        const unsigned message = frame.Word(4), flags = frame.Long(2), parameters = frame.Pointer(0);
        if (!parameters)
        {
            const unsigned a[] = {frame.Word(5), message, flags, 0};
            result = Api("mciSendCommandA", a);
            return true;
        }
        unsigned native = parameters;
        // MCI_OPEN_PARMS16 has a WORD device identifier followed by a padding
        // word and three aligned FAR pointers. Shanghai's original block and
        // the MMSYSTEM.701 entry both use that 20-byte layout.
        // The other parameter blocks used by commands currently implemented
        // by MciCommand contain only scalar DWORD values.
        if (message == 0x803)
        {
            native = Win16Scratch();
            if (!native)
                return false;
            Write32(native, Read32(parameters));
            Write32(native + 4, Win16Word(parameters + 4));
            for (unsigned i = 0; i < 3; ++i)
            {
                const unsigned field = parameters + 8 + i * 4;
                const unsigned offset = Win16Word(field), selector = Win16Word(field + 2);
                // MCI_OPEN_TYPE_ID carries the resource identifier in the
                // offset word with a null selector, like MAKEINTRESOURCE.
                Write32(native + 8 + i * 4, selector ? Win16Linear(selector, offset) : offset);
            }
        }
        const unsigned a[] = {frame.Word(5), message, flags, native};
        result = Api("mciSendCommandA", a);
        if (message == 0x803 && !result)
        {
            Win16Word(parameters + 4, Read32(native + 4));
            // MMSYSTEM clears the reserved word beside the
            // returned 16-bit device identifier after a successful open.
            Win16Word(parameters + 6, 0);
        }
        return true;
    }
    case 702: // mciSendString(command, answer, length, callback)
    {
        // MMSYSTEM ends in `retf 0Ch` and pushes the three dwords at
        // bp+0Eh, bp+0Ah and bp+6, so the frame is twelve bytes: the command
        // pointer highest, then the answer pointer, then the length and the
        // callback window in the lowest dword. Seven words read every argument
        // one word too high and left the caller two bytes of stack behind.
        words = 6;
        const unsigned a[] = {frame.Pointer(4), frame.Pointer(2), frame.Word(1), frame.Word(0)};
        result = Api("mciSendStringA", a);
        return true;
    }
    case 706: // mciGetErrorString(error, buffer, length)
    {
        words = 5;
        const unsigned a[] = {frame.Long(3), frame.Pointer(1), frame.Word(0)};
        result = Api("mciGetErrorStringA", a);
        return true;
    }
    default:
        break;
    }
    char text[240];
    snprintf(text, sizeof text,
             "Win16 MMSYSTEM ordinal %u is not ported yet: the device groups carry structures whose "
             "16-bit shape is not converted",
             ordinal);
    Fail(text);
    return false;
}

// ----------------------------------------------------------------- COMMCTRL
// COMMCTRL.DLL of the tree is a 16/32 thunk module: its entry table carries
// CCTL1632_THUNKDATA16 (301), so under Win98 the 16-bit entries are the same
// controls that COMCTL32 shows. They are ours here, and the native common
// controls are already there.
// CreateToolbarEx (ordinal 20, seg ) is the entry the file manager
// calls. The frame is `retf 0x1e`, fifteen words, and the body names the
// arguments in this order: hwnd at bp+22h, the style dword at bp+1Eh, the
// control identifier at bp+1Ch, the bitmap count at bp+1Ah, the bitmap module
// at bp+18h, the bitmap resource at bp+16h, the button array at bp+12h, the
// button count at bp+10h, the button width and height at bp+0Eh and bp+0Ch,
// the bitmap width and height at bp+0Ah and bp+08h and the button structure
// size at bp+06h. The body then does what this gate does: it makes a
// ToolbarWindow32 child, sets the structure size, the two sizes, adds the
// bitmap strip and adds the buttons.
// The 16-bit TBBUTTON is twelve bytes with word fields where the 32-bit one
// has dwords, so the array is widened to the twenty byte form the native
// toolbar takes.
bool Application::Win16CreateToolbarEx(const Win16Frame &frame, unsigned &result)
{
    const unsigned parent = frame.Word(14), style = frame.Long(12), id = frame.Word(11);
    const unsigned bitmaps = frame.Word(10), bitmap_module = frame.Word(9), bitmap_id = frame.Word(8);
    const unsigned buttons = frame.Pointer(6), button_count = frame.Word(5);
    const unsigned button_width = frame.Word(4), button_height = frame.Word(3);
    const unsigned bitmap_width = frame.Word(2), bitmap_height = frame.Word(1);
    const unsigned structure = frame.Word(0);
    if (structure != 12)
    {
        char text[200];
        snprintf(text, sizeof text,
                 "Win16 COMMCTRL.20 CreateToolbarEx got a button structure of %u bytes; the 16-bit "
                 "TBBUTTON is twelve",
                 structure);
        Fail(text);
        return false;
    }
    const unsigned klass = String("ToolbarWindow32");
    if (!klass)
    {
        last_error = 8;
        result = 0;
        return true;
    }
    // WS_CHILD, the way the original adds it with `or dh, 40h`.
    const unsigned instance = win16.state->modules[0].handle;
    const unsigned arguments[12] = {0,      klass, 0,  style | 0x40000000u, 0, 0, 100, 30,
                                    parent, id,    instance, 0};
    const unsigned hwnd = CreateWindow(arguments);
    Free(klass);
    if (!hwnd)
    {
        result = 0;
        return true;
    }
    Win16Send(hwnd, 0x041e /* TB_BUTTONSTRUCTSIZE */, 20, 0);
    if (bitmap_width && bitmap_height)
        Win16Send(hwnd, 0x0420 /* TB_SETBITMAPSIZE */, 0, bitmap_width | bitmap_height << 16);
    if (button_width && button_height)
        Win16Send(hwnd, 0x041f /* TB_SETBUTTONSIZE */, 0, button_width | button_height << 16);
    // The strip and the buttons take the way of the program's own
    // TB_ADDBITMAP and TB_ADDBUTTONS (Win16ToolbarSend): the bitmap by its
    // module and resource, the buttons in their 16-bit form.
    if (bitmaps && (bitmap_module || bitmap_id))
        Win16Send(hwnd, 0x0413 /* TB_ADDBITMAP */, bitmaps, bitmap_module | bitmap_id << 16);
    if (buttons && button_count)
        Win16Send(hwnd, 0x0414 /* TB_ADDBUTTONS */, button_count, frame.Long(6));
    Win16Send(hwnd, 0x0421 /* TB_AUTOSIZE */, 0, 0);
    result = hwnd;
    return true;
}
// The TBBUTTON of the 16-bit COMMCTRL has twelve bytes: iBitmap and
// idCommand words, the fsState and fsStyle bytes, dwData as a dword at +6 and
// iString as a word at +0Ah. The native toolbar takes the twenty bytes of the
// 32-bit TBBUTTON, which has dwords where this one has words.
void Application::Win16ToolbarButtonIn(unsigned source, unsigned target)
{
    unsigned char pair[4] = {};
    Read(source + 4, pair, 2);
    Write32(target + 0, (int)(short)Win16Word(source + 0)); // iBitmap
    Write32(target + 4, (int)(short)Win16Word(source + 2)); // idCommand
    Store(target + 8, pair, sizeof pair);                   // fsState, fsStyle
    Write32(target + 12, Read32(source + 6));               // dwData
    Write32(target + 16, (int)(short)Win16Word(source + 10)); // iString
}
void Application::Win16ToolbarButtonOut(unsigned source, unsigned target)
{
    unsigned char pair[2] = {};
    Read(source + 8, pair, 2);
    Win16Word(target + 0, Read32(source + 0) & 0xffff);
    Win16Word(target + 2, Read32(source + 4) & 0xffff);
    Store(target + 4, pair, sizeof pair);
    Write32(target + 6, Read32(source + 12));
    Win16Word(target + 10, Read32(source + 16) & 0xffff);
}
// A message of 16-bit code to a native toolbar (User::CommonControl 5), for
// the messages whose arguments have another shape on the 16-bit side. Answers
// true when the message was one of them and has been sent; every other toolbar
// message has the same arguments on both sides. File Manager makes its
// toolbar empty with CreateToolbarEx and then adds the strip and the buttons
// with these messages; before they were translated, the native toolbar found
// no bitmap and read the 16-bit buttons at a flat address, and the buttons
// stayed blank. TB_SAVERESTORE, TB_ADDSTRING, TB_SETROWS and
// TB_GETBUTTONTEXT carry strings and a rectangle in their 16-bit form.
bool Application::Win16ToolbarSend(unsigned hwnd, unsigned message, unsigned wparam, unsigned lparam,
                                   unsigned &result)
{
    if (User::CommonControl(hwnd) != 5 || message < 0x413 || message > 0x42d)
        return false;
    const unsigned flat = lparam >> 16 ? Win16Linear(lparam >> 16, lparam & 0xffff) : 0;
    auto send = [&](unsigned wp, unsigned lp) {
        const unsigned a[] = {hwnd, message, wp, lp};
        return Api("SendMessageA", a);
    };
    result = 0;
    switch (message)
    {
    case 0x413: // TB_ADDBITMAP(count, MAKELONG(hInst, idBitmap))
    {
        // The pair travels packed into lParam where the 32-bit side points to
        // a TBADDBITMAP. Without an instance the high word is the bitmap
        // itself, a GDI word that is the same on both sides; HINST_COMMCTRL
        // is FFFFh. The native toolbar loads the resource of a 16-bit
        // instance out of its NE module.
        const unsigned block = Allocate(8);
        if (!block)
        {
            last_error = 8;
            result = ~0u;
            return true;
        }
        const unsigned instance = lparam & 0xffff;
        Write32(block, instance == 0xffff ? ~0u : instance);
        Write32(block + 4, lparam >> 16);
        result = send(wparam & 0xffff, block);
        Free(block);
        return true;
    }
    case 0x414: // TB_ADDBUTTONS(count, buttons)
    case 0x415: // TB_INSERTBUTTON(index, button)
    {
        const unsigned count = message == 0x414 ? wparam & 0xffff : 1;
        if (!flat || !count)
        {
            result = send(message == 0x415 ? (int)(short)wparam : count, 0);
            return true;
        }
        const unsigned block = Allocate(count * 20);
        if (!block)
        {
            last_error = 8;
            return true;
        }
        for (unsigned i = 0; i < count; ++i)
            Win16ToolbarButtonIn(flat + i * 12, block + i * 20);
        result = send(message == 0x415 ? (int)(short)wparam : count, block);
        Free(block);
        return true;
    }
    case 0x417: // TB_GETBUTTON(index, button)
    {
        const unsigned block = flat ? Allocate(20) : 0;
        if (!block)
            return true;
        result = send(wparam & 0xffff, block);
        if (result)
            Win16ToolbarButtonOut(block, flat);
        Free(block);
        return true;
    }
    case 0x41d: // TB_GETITEMRECT(index, rect)
    {
        const unsigned block = flat ? Allocate(16) : 0;
        if (!block)
            return true;
        result = send(wparam & 0xffff, block);
        if (result)
            for (unsigned i = 0; i < 4; ++i)
                Win16Word(flat + i * 2, Read32(block + i * 4) & 0xffff);
        Free(block);
        return true;
    }
    case 0x41e: // TB_BUTTONSTRUCTSIZE: the buttons arrive widened
        result = send(20, 0);
        return true;
    case 0x41a: // TB_SAVERESTORE(save, names)
        return Win16ToolbarSaveRestore(hwnd, (wparam & 0xffff) != 0, flat, result);
    case 0x41c: // TB_ADDSTRING(instance, strings)
    {
        // Without an instance lParam points to the strings, each ended by a
        // zero and the list by a second one. With one it is string
        // LOWORD(lParam) of that module, and its first character separates
        // the strings that follow it, as the 16-bit COMMCTRL reads it.
        if (!(wparam & 0xffff))
        {
            result = send(0, flat);
            return true;
        }
        const unsigned block = Allocate(0x404);
        const unsigned length = block ? Win16LoadStringInto(wparam & 0xffff, lparam & 0xffff, block, 0x400) : 0;
        result = ~0u;
        if (length)
        {
            char text[0x404] = {};
            Read(block, text, length + 1);
            const char separator = text[0];
            for (unsigned i = 1; i < length; ++i)
                text[i - 1] = text[i] == separator ? 0 : text[i];
            text[length - 1] = text[length] = 0;
            Store(block, text, length + 1);
            result = send(0, block);
        }
        if (block)
            Free(block);
        return true;
    }
    case 0x427: // TB_SETROWS(rows, rect)
    {
        // wParam has no room for fLarger on the 16-bit side; the rectangle
        // comes back in words.
        const unsigned block = Allocate(16);
        if (!block)
        {
            last_error = 8;
            return true;
        }
        result = send(wparam & 0xffff, block);
        if (flat)
            for (unsigned i = 0; i < 4; ++i)
                Win16Word(flat + i * 2, Read32(block + i * 4) & 0xffff);
        Free(block);
        return true;
    }
    case 0x42d: // TB_GETBUTTONTEXT(command, text)
        result = send((int)(short)wparam, flat);
        return true;
    default:
        return false;
    }
}
// TB_SAVERESTORE of a 16-bit program names two far strings, an INI section and
// an INI file (File Manager: "Settings" and its WINFILE.INI). The 16-bit
// COMMCTRL of Windows 98 keeps the state in neither. Its SaveRestore writes
// the registry: under the key
// HKEY_CURRENT_USER\Software\Microsoft\Windows\CurrentVersion\ToolbarState
// a binary value named by the section, one dword per button (the command, -1
// for a separator, -2 for a hidden one), and the file name goes unused.
// Saving is the registry form of the toolbar with a TBSAVEPARAMS. Restoring
// reads the value and leaves the toolbar as it is unless the value is binary,
// longer than one record and shorter than 2 KiB. Then the toolbar gets one
// separator per separator record and asks the parent for its buttons between
// TBN_BEGINADJUST and TBN_ENDADJUST (TBN_ADJUSTINFO); a button the parent no
// longer offers is left out, every separator stays. It answers whether a
// button is left.
bool Application::Win16ToolbarSaveRestore(unsigned hwnd, bool save, unsigned names, unsigned &result)
{
    static const char key[] = "Software\\Microsoft\\Windows\\CurrentVersion\\ToolbarState";
    result = 0;
    const unsigned name = names ? Read32(names) : 0;
    const unsigned section = name >> 16 ? Win16Linear(name >> 16, name & 0xffff) : 0;
    Toolbar *t = ToolbarOf(hwnd);
    if (!section || !t)
        return true;
    char value[260];
    snprintf(value, sizeof value, "%s", ReadString(section));
    if (save)
    {
        const unsigned block = Allocate(12 + sizeof key + strlen(value) + 1);
        if (!block)
        {
            last_error = 8;
            return true;
        }
        Write32(block, 0x80000001u);
        Write32(block + 4, block + 12);
        Write32(block + 8, block + 12 + sizeof key);
        Store(block + 12, key, sizeof key);
        Store(block + 12 + sizeof key, value, strlen(value) + 1);
        const unsigned a[] = {hwnd, 0x41a, 1, block};
        result = Api("SendMessageA", a);
        Free(block);
        return true;
    }
    unsigned records[512], count = 0;
    {
        bool created = false;
        unsigned opened = 0, node = 0;
        if (registry.Open(0x80000001u, key, false, opened, created))
            return true;
        const Registry::Value *stored = nullptr;
        if (!registry.Key(opened, node) && !registry.store.Query(node, value, stored) && stored->type == 3 &&
            stored->size > 4 && stored->size < 0x800)
        {
            count = stored->size / 4;
            memcpy(records, stored->data, count * 4);
        }
        registry.Close(opened);
        if (!count)
            return true;
    }
    // The buttons in their 20-byte form: separators at once, buttons once
    // the parent has offered their command.
    const unsigned buttons = Allocate(count * 20 + 20);
    if (!buttons)
    {
        last_error = 8;
        return true;
    }
    bool filled[512] = {};
    for (unsigned i = 0; i < count; ++i)
    {
        const unsigned at = buttons + i * 20;
        for (unsigned k = 0; k < 20; k += 4)
            Write32(at + k, 0);
        if (records[i] & 0x80000000u)
        {
            const unsigned char pair[2] = {static_cast<unsigned char>(records[i] == ~0u ? 0 : 8), 1};
            Store(at + 8, pair, 2); // fsState (hidden for -2), fsStyle TBSTYLE_SEP
            filled[i] = true;
        }
        else
            Write32(at + 4, (int)(short)(records[i] & 0xffff));
    }
    const unsigned parent = Read32(t->object + 4), id = User::ControlID(hwnd) & 0xffff;
    auto adjust = [&](unsigned code) {
        if (parent && User::IsWindow(hwnd))
            CallWindow(parent, 0x111, id | code << 16, hwnd);
    };
    adjust(0x204); // TBN_BEGINADJUST
    const unsigned offered = buttons + count * 20;
    for (unsigned index = 0; index < 0x2000 && ToolbarButtonInfo(hwnd, index, offered, 0, 0); ++index)
    {
        unsigned char pair[2] = {};
        Read(offered + 8, pair, 2);
        const unsigned command = Read32(offered + 4);
        if ((pair[1] & 1) && !command)
            continue;
        for (unsigned i = 0; i < count; ++i)
            if (!(records[i] & 0x80000000u) && Read32(buttons + i * 20 + 4) == command)
            {
                for (unsigned k = 0; k < 20; k += 4)
                    Write32(buttons + i * 20 + k, Read32(offered + k));
                filled[i] = true;
                break;
            }
    }
    adjust(0x206); // TBN_ENDADJUST
    unsigned kept = 0;
    for (unsigned i = 0; i < count; ++i)
        if (filled[i])
        {
            if (kept != i)
                for (unsigned k = 0; k < 20; k += 4)
                    Write32(buttons + kept * 20 + k, Read32(buttons + i * 20 + k));
            ++kept;
        }
    if (ToolbarOf(hwnd))
    {
        auto send = [&](unsigned message, unsigned wp, unsigned lp) {
            const unsigned a[] = {hwnd, message, wp, lp};
            return Api("SendMessageA", a);
        };
        for (unsigned n = send(0x418, 0, 0); n; --n) // TB_BUTTONCOUNT, TB_DELETEBUTTON
            send(0x416, n - 1, 0);
        if (kept)
            send(0x414, kept, buttons); // TB_ADDBUTTONS
        send(0x421, 0, 0); // TB_AUTOSIZE
        User::InvalidateRect(hwnd, nullptr, true);
        result = kept >= 1;
    }
    Free(buttons);
    return true;
}
// The toolbar of the 16-bit COMMCTRL tells its parent with WM_COMMAND, never
// with WM_NOTIFY: wParam is the toolbar's identifier and lParam the
// notification code in the high word with a word beside it. TBN_BEGINDRAG
// (201h) and TBN_ENDDRAG (203h) carry the command of the button,
// TBN_ADJUSTINFO (205h), TBN_QUERYINSERT (208h) and TBN_QUERYDELETE (209h)
// an index, TBN_BEGINADJUST (204h), TBN_ENDADJUST (206h), TBN_RESET (207h),
// TBN_TOOLBARCHANGE (20Ah) and TBN_CUSTHELP (20Bh) the toolbar. TBN_ADJUSTINFO
// is TBN_GETBUTTONINFO: the parent answers a global block with a 16-bit
// TBBUTTON and the text of the button behind it. Every other notification of
// the toolbar has no 16-bit form and stays away from a 16-bit parent. Answers
// false when the parent is not a 16-bit window.
bool Application::Win16ToolbarNotify(unsigned toolbar, unsigned parent, unsigned code, unsigned note,
                                     unsigned &answer)
{
    if (!Win16Process() || !Win16IsMark(User::Procedure(parent)))
        return false;
    answer = 0;
    const unsigned id = User::ControlID(toolbar) & 0xffff, item = Read32(note + 12);
    auto command = [&](unsigned word, unsigned code16) {
        return CallWindow(parent, 0x111, id | code16 << 16, word & 0xffff) & 0xffff;
    };
    switch (code)
    {
    case 0xfffffd43: command(item, 0x201); break;           // TBN_BEGINDRAG
    case 0xfffffd42: command(item, 0x203); break;           // TBN_ENDDRAG
    case 0xfffffd41: command(toolbar, 0x204); break;        // TBN_BEGINADJUST
    case 0xfffffd40: command(toolbar, 0x206); break;        // TBN_ENDADJUST
    case 0xfffffd3f: command(toolbar, 0x207); break;        // TBN_RESET
    case 0xfffffd3e: answer = command(item, 0x208); break;  // TBN_QUERYINSERT
    case 0xfffffd3d: answer = command(item, 0x209); break;  // TBN_QUERYDELETE
    case 0xfffffd3c: command(toolbar, 0x20a); break;        // TBN_TOOLBARCHANGE
    case 0xfffffd3b: command(toolbar, 0x20b); break;        // TBN_CUSTHELP
    case 0xfffffd44:                                        // TBN_GETBUTTONINFOA
    {
        const unsigned handle = command(item, 0x205);       // TBN_ADJUSTINFO
        const auto *block = handle ? Win16GlobalBlock(handle) : nullptr;
        if (!block || !block->memory || block->size < 12)
            break;
        Win16ToolbarButtonIn(block->memory, note + 16);
        const unsigned text = Read32(note + 40), capacity = Read32(note + 36);
        if (text && capacity)
        {
            unsigned n = 0;
            for (unsigned char c = 0; n + 1 < capacity && 12 + n < block->size; ++n)
            {
                Read(block->memory + 12 + n, &c, 1);
                if (!c)
                    break;
                Store(text + n, &c, 1);
            }
            const unsigned char zero = 0;
            Store(text + n, &zero, 1);
        }
        answer = 1;
        break;
    }
    default:
        break;
    }
    return true;
}
// The Customize Toolbar dialog of a 16-bit toolbar is template 200 (C8h) of
// COMMCTRL.DLL, run with COMMCTRL's instance over the parent of the toolbar.
unsigned Application::Win16ToolbarCustomizeDialog(unsigned owner, unsigned toolbar)
{
    const unsigned module = Win16SystemModule("COMMCTRL");
    const Win98Ne::Image *image = module ? Win16SystemImage(module) : nullptr;
    unsigned offset = 0, bytes = 0;
    if (!image || !image->ResourceAt(Win16U::ResourceDialog, 0x8000 | 200, nullptr, offset, bytes) || !offset)
    {
        Fail("Win16 COMMCTRL.DLL has no Customize Toolbar dialog (template 200)");
        return 0;
    }
    return Win16DialogFrom(image->Data() + offset, bytes, module, owner, ToolbarCustomizeToken, toolbar, true);
}
// A string of COMMCTRL.DLL of the tree ("Separator" is 400h).
unsigned Application::Win16CommonControlString(unsigned id, unsigned buffer, unsigned capacity)
{
    return Win16LoadStringInto(Win16SystemModule("COMMCTRL"), id, buffer, capacity);
}
// MenuHelp of the 16-bit COMMCTRL (ordinal 2): the help line of a menu item
// in the simple part of a status bar. The body ends in `retf 12h` and does
// this, by its message:
// - WM_MENUSELECT with the flags FFFFh and no menu (the menu closes) turns
//   the simple part off.
// - Any other WM_MENUSELECT loads a string and shows it with SB_SETTEXT on
//   part 255 without borders (1FFh), then SB_SIMPLE on. A separator shows an
//   empty line. A command takes the string `ids[0] + command`; a system
//   command (F000h and up) the string `command + 9000h` of COMMCTRL itself.
//   A popup is looked up in the pairs of words behind the first two of `ids`
//   (string, menu, up to a zero string); a popup of the main menu that is
//   not there takes `ids[1] + its position`, where the system menu of a
//   maximized MDI child at position 0 does not count and takes COMMCTRL's
//   8FF0h, as does any other system menu. Only a command updates the bar at
//   once with UpdateWindow.
// - WM_COMMAND from a toolbar carries the 16-bit toolbar notification in the
//   high word of lParam: TBN_BEGINDRAG (201h) shows the help of the button's
//   command, which is the low word; TBN_BEGINADJUST (204h) COMMCTRL's
//   string 8FF3h; TBN_ENDDRAG (203h) and TBN_ENDADJUST (206h) turn the simple
//   part off again.
// File Manager hands every WM_MENUSELECT and the notifications of its
// toolbar to it; the first menu opened with the mouse stopped at it.
void Application::Win16MenuHelp(unsigned message, unsigned wparam, unsigned lparam, unsigned menu,
                                unsigned instance, unsigned status, unsigned ids)
{
    auto send = [&](unsigned m, unsigned w, unsigned l) {
        const unsigned a[] = {status, m, w, l};
        return Api("SendMessageA", a);
    };
    const unsigned own = Win16SystemModule("COMMCTRL");
    unsigned id = 0;
    bool update = true, text = true;
    if (message == 0x111)
    {
        switch (lparam >> 16)
        {
        case 0x201: // TBN_BEGINDRAG
            Win16MenuHelp(0x11f, lparam & 0xffff, 0, menu, instance, status, ids);
            return;
        case 0x203: // TBN_ENDDRAG
        case 0x206: // TBN_ENDADJUST
            send(0x409 /* SB_SIMPLE */, 0, 0);
            return;
        case 0x204: // TBN_BEGINADJUST
            update = false;
            id = 0x8ff3;
            instance = own;
            send(0x409, 1, 0);
            break;
        default:
            return;
        }
    }
    else if (message == 0x11f)
    {
        const unsigned flags = lparam & 0xffff, popup_menu = lparam >> 16;
        wparam &= 0xffff;
        if (flags == 0xffff && !popup_menu)
        {
            send(0x409, 0, 0);
            return;
        }
        if (flags & 0x800) // MF_SEPARATOR: an empty line
            text = false;
        else if (flags & 0x10) // MF_POPUP
        {
            update = false;
            text = false;
            for (unsigned at = ids + 4; ids && Win16Word(at); at += 4)
                if (Win16Word(at + 2) == wparam)
                {
                    id = Win16Word(at);
                    text = true;
                    break;
                }
            if (!text && popup_menu == menu)
            {
                int position = -1;
                const unsigned count_args[] = {menu};
                if (menu && wparam)
                    for (int i = int(Api("GetMenuItemCount", count_args)) - 1; i >= 0; --i)
                        if (User::GetSubMenu(menu, unsigned(i)) == wparam)
                        {
                            position = i;
                            break;
                        }
                if (position >= 0)
                {
                    // The system menu of a maximized MDI child stands first
                    // in the bar, with Restore as the last item.
                    const unsigned last[] = {menu, Api("GetMenuItemCount", count_args) - 1};
                    const bool child_menu = Api("GetMenuItemID", last) == 0xf120;
                    if (child_menu && !position)
                        id = 0x8ff0, instance = own;
                    else
                        id = Win16Word(ids + 2) + unsigned(position - (child_menu ? 1 : 0));
                    text = true;
                }
            }
            if (!text && (flags & 0x2000)) // MF_SYSMENU
                id = 0x8ff0, instance = own, text = true;
        }
        else if (wparam >= 0xf000)
            id = (wparam + 0x9000) & 0xffff, instance = own;
        else
            id = (Win16Word(ids) + wparam) & 0xffff;
    }
    else
        return;
    const unsigned buffer = Win16Scratch();
    if (!buffer)
        return;
    if (text)
        Win16LoadStringInto(instance, id, buffer, 256);
    send(0x401 /* SB_SETTEXT */, 0x1ff, buffer);
    send(0x409, 1, 0);
    if (update)
    {
        const unsigned a[] = {status};
        Api("UpdateWindow", a);
    }
}
// The classes of the common controls belong to COMCTL32, and they come into
// being when COMCTL32 is attached to the process. A 16-bit program never
// attaches it by itself: under Win98 it binds COMMCTRL.DLL, the thunk half,
// and loading that loads COMCTL32. This port does the same thing, and it does
// it the moment the first COMMCTRL entry is asked for. Without it
// CreateToolbarEx answered zero with ERROR_CANNOT_FIND_WND_CLASS, WINFILE saw
// its WM_CREATE fail and left before it had a window.
bool Application::Win16CommonControlModule()
{
    auto &machine = *win16.state;
    if (machine.common_controls)
        return true;
    // The classes are registered the way the attach of COMCTL32 registers
    // them, with the implementation in src/x86_common_init.cpp. The original
    // x86 COMCTL32 is not loaded for this: its entry point is 32-bit code, and
    // a 16-bit task is standing on a 16-bit stack whose selector has a base of
    // its own, so the entry would run with ESP as a flat address and write
    // outside every mapping. The classes belong to the module database of the
    // task, which is the instance a 16-bit program hands to CreateWindow.
    const unsigned a[] = {machine.modules[0].handle};
    if (!CommonInitApi("DLL_PROCESS_ATTACH", a))
    {
        Fail("Win16 COMMCTRL could not register the common control classes");
        return false;
    }
    machine.common_controls = 1;
    return true;
}
bool Application::Win16CommonControlCall(unsigned ordinal, const Win16Frame &frame, unsigned &result,
                                         unsigned &words)
{
    if (!Win16CommonControlModule())
        return false;
    if (ordinal >= 328 && ordinal <= 339)
        return Win16DpaCall(ordinal, frame, result, words);
    switch (ordinal)
    {
    case 17: // InitCommonControls: attaching COMCTL32 has registered the
             // classes, which is what the original entry amounts to.
        words = 0;
        result = 0;
        return true;
    case 2: // MenuHelp(message, wParam, lParam, menu, instance, status, ids), nine words
        words = 9;
        result = 0;
        Win16MenuHelp(frame.Word(8), frame.Word(7), frame.Long(5), frame.Word(4), frame.Word(3), frame.Word(2),
                      frame.Pointer(0));
        return state != Failed;
    case 4: // GetEffectiveClientRect(hwnd, rect, info), five words. The body
            // ends in `retf 0Ah` and does this: GetClientRect for
            // the window, then a walk over the pairs of words of the info
            // array from its fifth byte on, ending at a zero word. The second
            // word of a pair is a control identifier; a control that is there
            // and visible (the style test against 1000h of the high word) has its window rectangle taken into client coordinates
            // with two ScreenToClient calls and then subtracted from the
            // rectangle with SubtractRect.
    {
        words = 5;
        const unsigned hwnd = frame.Word(4), rect = frame.Pointer(2), info = frame.Pointer(0);
        result = 0;
        if (!hwnd || !rect)
            return true;
        const unsigned scratch = Win16Scratch();
        if (!scratch)
            return false;
        const unsigned client = scratch, child = scratch + 16, spare = scratch + 32;
        const unsigned a[] = {hwnd, client};
        Api("GetClientRect", a);
        for (unsigned at = 4; info && Win16Word(info + at); at += 4)
        {
            const unsigned id = Win16Word(info + at + 2);
            if (!id)
                continue;
            const unsigned item[] = {hwnd, id};
            const unsigned control = Api("GetDlgItem", item);
            if (!control)
                continue;
            const unsigned style[] = {control, (unsigned)-16 /* GWL_STYLE */};
            if (!(Api("GetWindowLongA", style) & 0x10000000u))
                continue;
            const unsigned window[] = {control, child};
            Api("GetWindowRect", window);
            const unsigned first[] = {hwnd, child}, second[] = {hwnd, child + 8};
            Api("ScreenToClient", first);
            Api("ScreenToClient", second);
            const unsigned subtract[] = {spare, client, child};
            Api("SubtractRect", subtract);
            for (unsigned i = 0; i < 4; ++i)
                Write32(client + i * 4, Read32(spare + i * 4));
        }
        for (unsigned i = 0; i < 4; ++i)
            Win16Word(rect + i * 2, Read32(client + i * 4) & 0xffff);
        return true;
    }
    case 6: // CreateStatusWindow(style, text, parent, id), six words. The body
            // ends in `retf 0Ch` and does exactly one thing: it
            // calls CreateWindowEx with its own class, the caller's
            // text, the style masked with FF7FFFBFh, the position -100,-100,
            // the size 10 by 10, the parent and the identifier out of the
            // dword at bp+6 and its own instance from the word at 1BEh. The
            // status bar then gives itself its place.
    {
        words = 6;
        const unsigned style = frame.Long(4) & 0xff7fffbfu, text = frame.Pointer(2);
        const unsigned parent = frame.Word(1), id = frame.Word(0);
        const unsigned klass = String("msctls_statusbar32");
        if (!klass)
        {
            last_error = 8;
            result = 0;
            return true;
        }
        const unsigned instance = win16.state->modules[0].handle;
        const unsigned arguments[12] = {0,      klass, text, style, (unsigned)-100, (unsigned)-100,
                                        10,     10,    parent, id,  instance,       0};
        result = CreateWindow(arguments);
        Free(klass);
        return true;
    }
    case 20: // CreateToolbarEx, fifteen words
        words = 15;
        return Win16CreateToolbarEx(frame, result);
    case 40: case 41: case 42: case 43: case 44: case 45: case 46: case 49: case 53: case 54:
        // The image lists, src/win16_common_controls.cpp.
        return Win16ImageListCall(ordinal, frame, result, words);
    case 159: // PropertySheet(lppsh), src/win16_property_sheet.cpp
        words = 2;
        result = Win16PropertySheet(frame.Long(0));
        return state != Failed;
    case 160: // CreatePropertySheetPage(lppsp)
        words = 2;
        result = Win16CreatePropertyPage(frame.Long(0));
        return true;
    case 161: // DestroyPropertySheetPage(hpage)
        words = 2;
        result = Win16DestroyPropertyPage(frame.Long(0)) ? 1 : 0;
        return true;
    default:
        break;
    }
    char text[240];
    snprintf(text, sizeof text,
             "Win16 COMMCTRL ordinal %u is not ported yet: its structure has the 16-bit shape and is "
             "not converted",
             ordinal);
    Fail(text);
    return false;
}
#include "win16_dpa.inc"
} // namespace X86
