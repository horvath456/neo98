#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the pinned DOSBox Pure core for the ARM64 host as a static library.

DOSBox Pure is the DOS runtime of NEO98: an original DOS program runs in it,
and the core is compiled natively for ARM64 and linked into the host runners.
This script follows tools/build_unicorn_host.py. The pinned tree under
`third_party/dosbox-pure` is never edited; it is cloned into
`build/dosbox-pure-host/source`, the NEO98 patches are written into the clone,
and every translation unit of the upstream Makefile is compiled into
`build/dosbox-pure-host/lib/libdosbox-pure.a`. tools/dosbox_pure_neo98.h is
copied into the clone as neo98_dbp.h; it is the interface the patches add.


The build is skipped when the stamp matches: same patched sources, same
interface header, same compiler, same flags, same import manifest.

    build/analysis-venv/bin/python tools/build_dosbox_pure.py [--force] [--jobs N]
"""
from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'third_party/dosbox-pure'
OUT = ROOT / 'build/dosbox-pure-host'
INTERFACE = ROOT / 'tools/dosbox_pure_neo98.h'
# Copied into the clone beside the interface: the record of the 3dfx Voodoo,
# which the interface includes, and the code voodoo.cpp includes (patch_voodoo).
EXTRA = {'neo98_adc.inc': ROOT / 'tools/dosbox_pure_adc.inc', 'dos_voodoo_frame.h': ROOT / 'src/dos_voodoo_frame.h', 'neo98_voodoo.inc': ROOT / 'tools/dosbox_pure_voodoo.inc',
         'neo98_serial.inc': ROOT / 'tools/dosbox_pure_serial.inc',
         'neo98_paste.inc': ROOT / 'tools/dosbox_pure_paste.inc',
         'neo98_files.inc': ROOT / 'tools/dosbox_pure_files.inc'}
# An extra file only one translation unit includes: a change compiles that unit alone.
INCLUDED_BY = {'neo98_adc.inc': 'src/hardware/sblaster.cpp', 'neo98_voodoo.inc': 'src/hardware/voodoo.cpp',
               'neo98_serial.inc': 'src/hardware/serialport/serialport.cpp',
               'neo98_paste.inc': 'dosbox_pure_libretro.cpp',
               'neo98_files.inc': 'dosbox_pure_libretro.cpp'}
LIBRARY_NAME = 'libdosbox-pure.a'
# The same objects as a dynamic library that exports the libretro entry points
# and the shared state alone. The host loads one copy of it per DOS session,
# because the core keeps its machine in globals.
DYLIB_NAME = 'libdosbox-pure.so' if sys.platform.startswith('linux') else 'libdosbox-pure.dylib'
EXPORTS = ['_retro_set_environment', '_retro_set_video_refresh', '_retro_set_audio_sample',
           '_retro_set_audio_sample_batch', '_retro_set_input_poll', '_retro_set_input_state', '_retro_init',
           '_retro_deinit', '_retro_load_game', '_retro_unload_game', '_retro_run', '_retro_get_system_av_info',
           '_retro_serialize_size', '_retro_serialize', '_retro_unserialize', # saved states
           '__Z14DBP_Neo98Statev']  # Neo98DbpState *DBP_Neo98State, C++ linkage
# The flags of the upstream Makefile for a macOS build, without the shared
# library bits. The dynamic recompiler for ARM64 is built (C_DYNREC with
# risc_armv8le.h, include/config.h); its code cache lives in MAP_JIT memory
# (patch_dyn_cache below), and the core option "core" chooses it.
# -flto: measured on 24 September 2026 with tools/bench_dos_vm.py, the
# interpreter at cycles=max runs 13 percent faster (median of seven runs,
# 5.17 s against 5.93 s), the recompiler is unchanged; -O3 did not measurably
# help, and -mcpu=apple-m1 is the default of the arm64 macOS target already
# (the library is byte for byte the same).
# The compiler of the system (Xcode or the Command Line Tools), not the first
# clang++ on PATH. Every object is LLVM bitcode (-flto), and the system linker
# reads it with the libLTO of the system; the Homebrew clang that
# neo98.tool_environment puts first on PATH writes a newer bitcode that this
# linker rejects ("Unknown attribute kind"), so the core failed to link in the
# host suite while it built from a plain shell.
CXX = '/usr/bin/clang++'
FLAGS = ['-std=gnu++11', '-O2', '-flto', '-DNDEBUG', '-fomit-frame-pointer', '-fexceptions',
         '-Wno-address-of-packed-member', '-Wno-format', '-Wno-switch', '-Wno-psabi',
         '-Wno-unknown-warning-option', '-Wno-deprecated-declarations', '-Wno-deprecated',
         '-D__LIBRETRO__', '-D_FILE_OFFSET_BITS=64', '-pthread',
         '-fno-ident', '-ffunction-sections']
if sys.platform.startswith('linux'):
    sys.path.insert(0, str(ROOT))
    import neo98
    CXX = shutil.which('clang++', path=neo98.tool_environment()['PATH'])
    FLAGS += ['-fPIC']


def replace_once(text, marker, replacement):
    assert text.count(marker) == 1, 'marker not found exactly once: ' + marker[:60]
    return text.replace(marker, replacement)


def patch_libretro(text):
    """The NEO98 hooks in the libretro core.

    1. The interface header and the shared state.
    2. Z:NEO98END.COM, the last line of the autoexec: it reads the DOS return
       code of the program and ends the shell, the way PUREMENU -FINISH ends it
       when the menu time is zero.
    3. The drives: the host directories of NEO98 instead of the content path.
    4. The autoexec: working directory, program, NEO98END.
    5. The end: when the machine has stopped the core reports it to the host
       instead of opening its own menu or asking the frontend to shut down.
    """
    text = replace_once(text, '#include "core_options.h"\n', '''#include "core_options.h"
#include "include/dos_inc.h"
#include "neo98_dbp.h"
// NEO98: the state the host shares with the core.
static Neo98DbpState DBP_Neo98;
Neo98DbpState* DBP_Neo98State() { return &DBP_Neo98; }
bool DBP_Neo98Active() { return DBP_Neo98.active; }
''')
    text = replace_once(text, 'static void init_dosbox_load_dosboxconf(', '''// NEO98: Z:NEO98END.COM. The autoexec runs it after the program. The DOS
// return code is what INT 21h/4Ch left behind; ending the first shell stops
// the machine, and retro_run then reports the end to the host.
struct DBP_Neo98EndProgramImpl : Program
{
	virtual void Run() override
	{
		DBP_Neo98.exit_code = dos.return_code;
		DBP_Neo98.program_finished = true;
		// Let the machine draw the screen the program left behind before it
		// stops: a program that prints and ends inside one frame would leave
		// no picture otherwise. Two frames, counted by GFX_EndUpdate.
		const Bit32u start = dbp_framecount;
		while (dbp_framecount < start + 2 && !first_shell->exit) CALLBACK_Idle();
		first_shell->exit = true;
	}
};
static void DBP_Neo98EndProgram(Program** make) { *make = new DBP_Neo98EndProgramImpl; }

// NEO98: Z:NEO98LIN.COM, the Initial size of the PIF's Screen page. What
// MODE CON LINES= does on a VGA: 350 scan lines for 43 rows or 400 for 50
// (INT 10h AH=12h BL=30h), text mode 3, then the 8x8 font of the video BIOS
// with the rows recomputed (INT 10h AX=1112h).
struct DBP_Neo98LinesImpl : Program
{
	virtual void Run() override
	{
		const unsigned rows = DBP_Neo98.lines;
		if (rows != 43 && rows != 50) return;
		reg_ax = (rows == 43 ? 0x1201 : 0x1202); reg_bl = 0x30; CALLBACK_RunRealInt(0x10);
		reg_ax = 0x0003; CALLBACK_RunRealInt(0x10);
		reg_ax = 0x1112; reg_bl = 0; CALLBACK_RunRealInt(0x10);
	}
};
static void DBP_Neo98Lines(Program** make) { *make = new DBP_Neo98LinesImpl; }

// NEO98: DOS was asked to run a Windows program. Record it for the host with
// the DOS current directory; the DOS side goes on as if the program had
// returned at once, which is what a DOS box under Win98 does as well.
void DBP_Neo98LaunchWindows(const char* program, const char* arguments, bool wait)
{
	// One request at a time: a batch file that starts two Windows programs
	// inside one frame overwrote the first before the host read it.
	while (DBP_Neo98.windows_seen != DBP_Neo98.windows_request && !first_shell->exit) CALLBACK_Idle();
	safe_strncpy(DBP_Neo98.windows_program, program, sizeof(DBP_Neo98.windows_program));
	safe_strncpy(DBP_Neo98.windows_arguments, arguments, sizeof(DBP_Neo98.windows_arguments));
	char dir[DOS_PATHLENGTH + 4] = { (char)('A' + DOS_GetDefaultDrive()), ':', '\\\\', 0 };
	DOS_GetCurrentDir(0, dir + 3);
	safe_strncpy(DBP_Neo98.windows_directory, dir, sizeof(DBP_Neo98.windows_directory));
	DBP_Neo98.windows_wait = wait;
	DBP_Neo98.windows_done = false;
	DBP_Neo98.windows_exit_code = 0;
	DBP_Neo98.windows_request++;
	// A console program: the DOS side waits, the frames go on meanwhile.
	if (wait) while (!DBP_Neo98.windows_done && !first_shell->exit) CALLBACK_Idle();
}
unsigned DBP_Neo98WindowsExitCode() { return DBP_Neo98.windows_exit_code; }

// NEO98: before every frame. Paste produces make/break events through IRQ1,
// bounded by the hardware FIFO and BIOS buffer. The screen feeds Mark/Copy.
#include "neo98_paste.inc"
void DBP_Neo98FrameHook()
{
	if (!DBP_Neo98.active) return;
	const unsigned room = BIOS_Neo98PasteRoom();
	for (unsigned n = 0; n < DBP_Neo98.paste_rate && n < room && DBP_Neo98.paste_head != DBP_Neo98.paste_tail; n++)
	{
		unsigned char c = (unsigned char)DBP_Neo98.paste_text[DBP_Neo98.paste_head % sizeof(DBP_Neo98.paste_text)];
		if (c == '\\r') { DBP_Neo98.paste_head++; continue; }
		if (!DBP_Neo98PasteCharacter(c)) break;
		DBP_Neo98.paste_head++;
	}
	// While a 3dfx Voodoo has the monitor, the text mode of the VGA behind
	// it is not what the screen shows.
	extern bool voodoo_is_active();
	if (voodoo_is_active() || !DBP_Neo98TextScreen(&DBP_Neo98.text_columns, &DBP_Neo98.text_rows, DBP_Neo98.text_cells, sizeof(DBP_Neo98.text_cells) / 2))
		DBP_Neo98.text_columns = DBP_Neo98.text_rows = 0;
	if (!DBP_Neo98TextCursor(&DBP_Neo98.cursor_column, &DBP_Neo98.cursor_row, &DBP_Neo98.cursor_start, &DBP_Neo98.cursor_end, &DBP_Neo98.cursor_height))
		DBP_Neo98.cursor_height = 0;
}

// NEO98: the drives of the session. Each entry is a host directory behind a
// letter; the Win98 tree is mounted with a memory overlay, so a DOS program
// that writes to C: never changes the tree. A drive whose files the host
// keeps itself (the write layer of a game machine) asks the host for
// everything (neo98_files.inc). C: always exists, because the autoexec of
// DOSBox Pure starts there.
#include "neo98_files.inc"
static void DBP_Neo98Mount()
{
	for (unsigned i = 0; i != DBP_Neo98.drive_count && i != Neo98DbpState::DriveCapacity; i++)
	{
		const Neo98DbpDrive& d = DBP_Neo98.drives[i];
		const char letter = (char)(d.letter & 0x5F);
		if (letter < 'A' || letter > 'Z' || Drives[letter-'A']) continue;
		if (d.files)
		{
			DOS_Drive* kept = new neo98Drive(*d.files, letter);
			Drives[letter-'A'] = kept;
			mem_writeb(Real2Phys(dos.tables.mediaid) + (letter-'A') * 9, kept->GetMediaByte());
			continue;
		}
		if (!d.directory[0]) continue;
		std::string dir = d.directory;
		if (dir[dir.size() - 1] != '/') dir += '/';
		dir_information* dirp = open_directory(dir.c_str());
		if (!dirp) { log_cb(RETRO_LOG_ERROR, "[NEO98] cannot open directory %s for drive %c:\\n", dir.c_str(), letter); continue; }
		close_directory(dirp);
		localDrive* local = new localDrive(dir.c_str(), 512, 32, 32765, 16000, 0xF8);
		DOS_Drive* drive = local;
		if (d.overlay_directory[0])
		{
			// The writes go to the overlay directory of the Windows side; the
			// union drive of DOSBox Pure keeps deletions and renames of the
			// tree below in its modification records.
			std::string over = d.overlay_directory;
			if (over[over.size() - 1] != '/') over += '/';
			if (dir_information* overp = open_directory(over.c_str()))
			{
				close_directory(overp);
				drive = new unionDrive(*local, *new localDrive(over.c_str(), 512, 32, 32765, 16000, 0xF8), true, true);
			}
			else drive = new unionDrive(*local, (const char*)NULL, true, false);
		}
		else if (d.memory_overlay) drive = new unionDrive(*local, (const char*)NULL, true, false);
		Drives[letter-'A'] = drive;
		mem_writeb(Real2Phys(dos.tables.mediaid) + (letter-'A') * 9, drive->GetMediaByte());
	}
	if (!Drives['C'-'A'])
	{
		DOS_Drive* drive = new unionDrive(*new memoryDrive(), (const char*)NULL, true, false);
		Drives['C'-'A'] = drive;
		mem_writeb(Real2Phys(dos.tables.mediaid) + ('C'-'A') * 9, drive->GetMediaByte());
	}
}

// NEO98: the autoexec of the session. The working directory of the PIF, the
// program with its parameters, then NEO98END. Without a program the shell
// stays at the prompt; `exit` ends it because the menu time option is zero.
static void DBP_Neo98Autoexec(std::string& data)
{
	// The search path of a DOS box under Win98, from its AUTOEXEC.BAT: the
	// external commands of the tree (CHOICE, MORE, EDIT, XCOPY ...) run in
	// the machine, only the internal ones come from this shell.
	data += "PATH C:\\\\WINDOWS;C:\\\\WINDOWS\\\\COMMAND;Z:\\\\\\n";
	const char* dir = DBP_Neo98.working_directory;
	if (dir[0] && dir[1] == ':')
	{
		data += (char)(dir[0] & 0x5F); data += ":\\n";
		if (dir[2]) { data += "cd "; data += dir + 2; data += '\\n'; }
	}
	if (DBP_Neo98.lines == 43 || DBP_Neo98.lines == 50) data += "Z:NEO98LIN\\n";
	// The batch file of the PIF, the AUTOEXEC.BAT of MS-DOS mode.
	data += DBP_Neo98.prelude;
	const char* command = DBP_Neo98.command;
	if (!command[0]) return;
	// A batch file needs `call`, otherwise the autoexec never comes back.
	size_t len = strlen(command), name_end = 0;
	while (name_end < len && command[name_end] != ' ') name_end++;
	if (name_end >= 4 && !strncasecmp(command + name_end - 4, ".BAT", 4)) data += "call ";
	data += command;
	data += "\\nZ:NEO98END\\n";
}

static void init_dosbox_load_dosboxconf(''')
    # A drive the host keeps is not walked when the machine starts: upstream
    # looks through every drive for disc images and for the game's name, a
    # local drive for at most 500 directories, any other kind without a
    # limit, which here was the whole of drive C: at every start.
    text = replace_once(text, '''	for (int i = 0; i != ('Z'-'A'); i++)
		if (Drives[i])
			DriveFileIterator(Drives[i], Local::FileIter, i,''', '''	for (int i = 0; i != ('Z'-'A'); i++)
		if (Drives[i] && !dynamic_cast<neo98Drive*>(Drives[i])) // NEO98
			DriveFileIterator(Drives[i], Local::FileIter, i,''')
    text = replace_once(text, '''	check_variables();
	dbp_boot_time = time_cb();
	control->Init();''', '''	check_variables();
	if (DBP_Neo98.active)
	{
		// NEO98: the memory the PIF grants. The machine is built from the
		// section right after this.
		if (Section* dos = control->GetSection("dos"))
		{
			dos->HandleInputline(DBP_Neo98.xms ? "xms=true" : "xms=false");
			dos->HandleInputline(DBP_Neo98.ems ? "ems=true" : "ems=false");
		}
	}
	dbp_boot_time = time_cb();
	control->Init();''')
    text = replace_once(text, '''	extern const char* RunningProgram;
	bool was_game_running = dbp_game_running;''', '''	extern const char* RunningProgram;
	safe_strncpy(DBP_Neo98.running_program, RunningProgram, sizeof(DBP_Neo98.running_program)); // NEO98: for the window title
	bool was_game_running = dbp_game_running;''')
    text = replace_once(text, '''	PROGRAMS_MakeFile("XCOPY.COM", DBP_PureXCopyProgram);
''', '''	PROGRAMS_MakeFile("XCOPY.COM", DBP_PureXCopyProgram);
	PROGRAMS_MakeFile("NEO98END.COM", DBP_Neo98EndProgram);
	PROGRAMS_MakeFile("NEO98LIN.COM", DBP_Neo98Lines);
''')
    text = replace_once(text, '''	if (!dbp_skip_c_mount)
	{
		dbp_legacy_save = false;
		std::string save_file = DBP_GetSaveFile(SFT_GAMESAVE); // this can set dbp_legacy_save to true, needed by DBP_Mount
''', '''	if (!dbp_skip_c_mount && DBP_Neo98.active)
	{
		DBP_Neo98Mount();
	}
	else if (!dbp_skip_c_mount)
	{
		dbp_legacy_save = false;
		std::string save_file = DBP_GetSaveFile(SFT_GAMESAVE); // this can set dbp_legacy_save to true, needed by DBP_Mount
''')
    text = replace_once(text, '''		bool auto_mount = true;
		autoexec->ExecuteDestroy();
		if (!force_puremenu && dbp_menu_time != (signed char)-1 && path_extlen == 3 &&''', '''		bool auto_mount = true;
		autoexec->ExecuteDestroy();
		if (DBP_Neo98.active)
		{
			std::string& data = static_cast<Section_line*>(autoexec)->data;
			data += "@echo off\\n";
			DBP_Neo98Autoexec(data);
		}
		else if (!force_puremenu && dbp_menu_time != (signed char)-1 && path_extlen == 3 &&''')
    text = replace_once(text, '''	if (dbp_message_queue) run_emuthread_notify();

	if (!environ_cb(RETRO_ENVIRONMENT_GET_THROTTLE_STATE, &dbp_throttle))''', '''	if (dbp_message_queue) run_emuthread_notify();
	DBP_Neo98FrameHook(); // NEO98: paste keys, the text screen

	if (!environ_cb(RETRO_ENVIRONMENT_GET_THROTTLE_STATE, &dbp_throttle))''')
    text = replace_once(text, '''			handle_state_exited:
			DBP_Buffer& buf = dbp_buffers[buffer_active];
			if (!dbp_crash_message.empty()) // unexpected shutdown
				DBP_Shutdown();
''', '''			handle_state_exited:
			DBP_Buffer& buf = dbp_buffers[buffer_active];
			if (DBP_Neo98.active)
			{
				// NEO98: the machine has stopped. Report it and keep showing
				// the last picture; the host decides what happens to the window.
				if (!dbp_crash_message.empty())
				{
					DBP_Neo98.crashed = true;
					safe_strncpy(DBP_Neo98.crash_message, dbp_crash_message.c_str(), sizeof(DBP_Neo98.crash_message));
					DBP_Shutdown();
				}
				DBP_Neo98.finished = true;
				if (buf.video) video_cb(buf.video, buf.width, buf.height, buf.width * 4);
				return;
			}
			if (!dbp_crash_message.empty()) // unexpected shutdown
				DBP_Shutdown();
''')
    # 4b. A program that ends inside the first frame. Upstream takes a machine
    # that has exited when the frontend first asks for the av info as a
    # reboot wish of PUREMENU and resets it, which runs the autoexec a
    # second time: a batch file that only starts Windows programs ends in
    # the first frame and started each of them twice. Z:NEO98END
    # ended it on purpose; the host reads the end instead.
    text = replace_once(text, '''	DBP_ThreadControl(TCM_FINISH_FRAME);
	if (dbp_biosreboot || dbp_state == DBPSTATE_EXITED)
	{''', '''	DBP_ThreadControl(TCM_FINISH_FRAME);
	if (dbp_biosreboot || (dbp_state == DBPSTATE_EXITED && !(DBP_Neo98.active && DBP_Neo98.program_finished))) // NEO98
	{''')
    # 5. A second machine in the same process. RetroArch unloads the core's
    # library between two games, so upstream frees the picture buffers in
    # retro_deinit and never looks at their capacity again. Here the core
    # stays linked in: the next retro_init must see an empty buffer, or the
    # scaler writes its first frame through the null pointer.
    text = replace_once(text, '''	for (DBP_Buffer& buf : dbp_buffers) { if (buf.video) { free(buf.video); buf.video = NULL; } }
}''', '''	for (DBP_Buffer& buf : dbp_buffers) { if (buf.video) { free(buf.video); buf.video = NULL; } buf.cap = buf.width = buf.height = 0; } // NEO98: the capacity too
}''')
    # 6. The 3dfx Voodoo drawn by the host (patch_voodoo). With a sink in the
    # shared state the card records what it draws, as it does for
    # upstream's OpenGL renderer, but no OpenGL context is asked for and
    # the record goes to the sink once a frame.
    text = replace_once(text, '''(dbp_hw_render.context_type == RETRO_HW_CONTEXT_NONE ? "1" : "4")''',
                        '''(dbp_hw_render.context_type == RETRO_HW_CONTEXT_NONE && !(DBP_Neo98.active && DBP_Neo98.voodoo_frame) ? "1" : "4")''')
    text = replace_once(text, '''		if (dbp_hw_render.context_type == RETRO_HW_CONTEXT_NONE && (atoi(voodoo_perf) & 0x4))''',
                        '''		if (dbp_hw_render.context_type == RETRO_HW_CONTEXT_NONE && (atoi(voodoo_perf) & 0x4) && !(DBP_Neo98.active && DBP_Neo98.voodoo_frame))''')
    text = replace_once(text, '''	if (voodoo_perf[0] == 'a' || voodoo_perf[0] == '4') // 3dfx wants to use OpenGL, request hardware render context''',
                        '''	if (!(DBP_Neo98.active && DBP_Neo98.voodoo_frame) && (voodoo_perf[0] == 'a' || voodoo_perf[0] == '4')) // NEO98: the host draws the card''')
    text = replace_once(text, '''	if (dbp_opengl_draw && voodoo_ogl_mainthread()) { view_width *= voodoo_ogl_scale; view_height *= voodoo_ogl_scale; }''',
                        '''	extern void voodoo_neo98_frame();
	if (DBP_Neo98.active && DBP_Neo98.voodoo_frame) voodoo_neo98_frame(); // NEO98
	else if (dbp_opengl_draw && voodoo_ogl_mainthread()) { view_width *= voodoo_ogl_scale; view_height *= voodoo_ogl_scale; }''')
    # 7. Saved states. The host calls retro_serialize and
    # retro_unserialize itself, between two frames; what the archive said
    # goes to the shared state, so the host can tell the person why a
    # state was refused instead of pointing at a log line.
    text = replace_once(text, '''	if (dbp_game_running && ar.mode == DBPArchive::MODE_LOAD) dbp_lastmenuticks = DBP_GetTicks(); // force show menu on immediate emulation crash
''', '''	if (dbp_game_running && ar.mode == DBPArchive::MODE_LOAD) dbp_lastmenuticks = DBP_GetTicks(); // force show menu on immediate emulation crash
	if (ar.mode == DBPArchive::MODE_LOAD || ar.mode == DBPArchive::MODE_SAVE) { DBP_Neo98.state_error = ar.had_error; DBP_Neo98.state_error_info = ar.error_info; DBP_Neo98.state_warnings = ar.warnings; } // NEO98
''')
    return text


def patch_paging_busy(text):
    """Whether the machine is inside a page fault that a call of DOS or of
    the BIOS ran into (PageFaultCore runs the program's handler on the host
    stack of that call). """
    return text + """
// NEO98: a state is not taken while a host call waits for the program's page fault handler.
bool PAGING_NEO98_InFault(void) { return pf_queue.used != 0; }
"""


def patch_callback_busy(text):
    """The same for a real-mode interrupt or routine the host called and
    waits for (CALLBACK_RunRealInt, CALLBACK_RunRealFar)."""
    text = replace_once(text, """void CALLBACK_RunRealFar(Bit16u seg,Bit16u off) {""", """int neo98_nested_runs = 0; // NEO98: calls of the host into the program that have not returned
void CALLBACK_RunRealFar(Bit16u seg,Bit16u off) {""")
    text = replace_once(text, """	reg_eip=off;
	SegSet16(cs,seg);
	DOSBOX_RunMachine();
""", """	reg_eip=off;
	SegSet16(cs,seg);
	neo98_nested_runs++;
	DOSBOX_RunMachine();
	neo98_nested_runs--;
""")
    return replace_once(text, """	SegSet16(cs,CB_SEG);
	DOSBOX_RunMachine();
""", """	SegSet16(cs,CB_SEG);
	neo98_nested_runs++;
	DOSBOX_RunMachine();
	neo98_nested_runs--;
""")


def patch_exception_trace(text):
    """NEO98_DOS_STATE_TRACE=1: the exceptions of the processor after a state
    was put in go to the log (the first 400), to see what a program that
    fails after a load ran into. """
    return replace_once(text, """void CPU_Exception(Bitu which,Bitu error ) {
//	LOG_MSG("Exception %d error %x",which,error);
""", """int neo98_exception_log = 0; // NEO98: set by DBPSerialize_All after a load
void CPU_Exception(Bitu which,Bitu error ) {
//	LOG_MSG("Exception %d error %x",which,error);
	if (neo98_exception_log > 0) { neo98_exception_log--; fprintf(stderr, "DOSVM exception %u error %x at %04x:%08x cr2 %08x esp %08x\\n", (unsigned)which, (unsigned)error, (unsigned)SegValue(cs), (unsigned)reg_eip, (unsigned)paging.cr2, (unsigned)reg_esp); }
""")


def patch_serialize(text):
    """The searches of the drives the host keeps are part of a state
    (DBPSerialize_Neo98 in tools/dosbox_pure_files.inc). """
    text = replace_once(text, '''			case __LINE__: DBPSERIALIZE_GET_FVER(DBPSerialize_IDE,     >=8); break;
''', '''			case __LINE__: DBPSERIALIZE_GET_FVER(DBPSerialize_IDE,     >=8); break;
			case __LINE__: DBPSERIALIZE_GET_FUNC(DBPSerialize_Neo98       ); break; // NEO98
''')
    # NEO98_STATE_SECTIONS=1 prints where each part of a state begins, to find
    # what differs between two states.
    text = replace_once(text, '''	void (*func)(DBPArchive& ar); //const char* func_name;
	#define DBPSERIALIZE_GET_FUNC(FUNC) void FUNC(DBPArchive& ar); func = FUNC; //func_name = #FUNC
''', '''	void (*func)(DBPArchive& ar); const char* func_name = ""; // NEO98
	static const bool neo98_sections = (getenv("NEO98_STATE_SECTIONS") != NULL);
	#define DBPSERIALIZE_GET_FUNC(FUNC) void FUNC(DBPArchive& ar); func = FUNC; func_name = #FUNC
''')
    text = replace_once(text, '''		//size_t old_off = ar.GetOffset();
		func(ar);
''', '''		if (neo98_sections && ar.mode == DBPArchive::MODE_SAVE) fprintf(stderr, "DOSVM state section %s at %u\\n", func_name, (unsigned)ar.GetOffset()); // NEO98
		func(ar);
''')
    text = replace_once(text, """void DBPSerialize_All(DBPArchive& ar, bool dos_running, bool game_running)
{
""", """extern int neo98_exception_log; // NEO98: src/cpu/cpu.cpp
extern int neo98_nested_runs;   // NEO98: src/cpu/callback.cpp
bool PAGING_NEO98_InFault(void); // NEO98: src/cpu/paging.cpp
#include "neo98_dbp.h" // NEO98
void DBPSerialize_All(DBPArchive& ar, bool dos_running, bool game_running)
{
	if (ar.mode == DBPArchive::MODE_LOAD && getenv("NEO98_DOS_STATE_TRACE")) neo98_exception_log = 400;
	// NEO98: no state while a call of the host waits for the program on its own stack.
	if (ar.mode == DBPArchive::MODE_SAVE || ar.mode == DBPArchive::MODE_SIZE)
	{
		// (The shell runs a program through CALLBACK_RunRealInt, so one such
		// call is open as long as any program runs; neo98_nested_runs is
		// counted for the one who looks, and is no reason by itself.)
		(void)neo98_nested_runs;
		DBP_Neo98State()->state_busy = PAGING_NEO98_InFault() ? 1 : 0;
		if (DBP_Neo98State()->state_busy) { ar.had_error = DBPArchive::ERR_GAMENOTRUNNING; return; }
	}
""")
    return replace_once(text, '#include <stdio.h>\n', '#include <stdio.h>\n#include <stdlib.h> /* getenv, NEO98 */\n')


def patch_shell(text):
    """No DOSBox banner at the prompt. Win98 prints none for a PIF-started
    prompt either; the MS-DOS window starts with the prompt itself."""
    text = replace_once(text, '''	if (cmd->FindString("/INIT",line,true)) {
		WriteOut(MSG_Get("SHELL_STARTUP_BEGIN"),VERSION);
''', '''	if (cmd->FindString("/INIT",line,true)) {
		extern bool DBP_Neo98Active();
		if (!DBP_Neo98Active()) { // NEO98: no banner
		WriteOut(MSG_Get("SHELL_STARTUP_BEGIN"),VERSION);
''')
    text = replace_once(text, '''		WriteOut(MSG_Get("SHELL_STARTUP_END"));

		strcpy(input_line,line.c_str());''', '''		WriteOut(MSG_Get("SHELL_STARTUP_END"));
		}

		strcpy(input_line,line.c_str());''')
    return text


def patch_shell_cmds(text):
    """A program name in double quotes. COMMAND.COM of MS-DOS 7 takes
    a leading quoted name as the command, spaces included, and drops the
    quotes: `"sb2.exe" -sw` runs SB2.EXE with ` -sw`. The DOSBox shell copied
    the quotes into the name and answered "Illegal command"."""
    text = replace_once(text, '''	char * cmd_write=cmd_buffer;
	while (*line) {
		if (*line == 32) break;''', '''	char * cmd_write=cmd_buffer;
	if (*line == '"') { // NEO98: quoted program name
		const char * close = strchr(line + 1, '"');
		if (close && (size_t)(close - line - 1) < CMD_MAXLINE - 1) {
			memcpy(cmd_write, line + 1, close - line - 1);
			cmd_write += close - line - 1;
			line = (char *)close + 1;
		}
	}
	while (*line) {
		if (*line == 32) break;''')
    return text


def patch_execute(text):
    """A Windows program behind the DOS stub goes to Windows. MS-DOS 7 inside a
    Win98 DOS box hands a PE or NE image to WINOLDAP, which runs it as a
    Windows process; the prompt or the batch file continues at once. The
    core records the request for the host and answers the EXEC call as if the
    program had returned with code 0. """
    text = replace_once(text, '#include "dosbox.h"\n', '''#include "dosbox.h"
// NEO98: see neo98_dbp.h and the patch in tools/build_dosbox_pure.py.
bool DBP_Neo98Active();
void DBP_Neo98LaunchWindows(const char* program, const char* arguments, bool wait);
unsigned DBP_Neo98WindowsExitCode();
''')
    text = replace_once(text, '''			if (imagesize+headersize<512) imagesize = 512-headersize;
		}
	}
''', '''			if (imagesize+headersize<512) imagesize = 512-headersize;
			// NEO98: a PE or NE image behind the stub is a Windows program.
			if (head.reloctable >= 0x40 && DBP_Neo98Active())
			{
				Bit8u newheader[4]; Bit16u n = 4; Bit32u at = 0x3c;
				if (DOS_SeekFile(fhandle,&at,DOS_SEEK_SET) && DOS_ReadFile(fhandle,newheader,&n) && n == 4)
				{
					Bit32u offset = newheader[0] | ((Bit32u)newheader[1] << 8) | ((Bit32u)newheader[2] << 16) | ((Bit32u)newheader[3] << 24);
					Bit8u sig[2]; n = 2;
					if (offset >= 0x40 && offset < 0x10000000 && DOS_SeekFile(fhandle,&offset,DOS_SEEK_SET) && DOS_ReadFile(fhandle,sig,&n) && n == 2 &&
					    ((sig[0] == 'P' && sig[1] == 'E') || (sig[0] == 'N' && sig[1] == 'E')))
					{
						char full[DOS_PATHLENGTH + 4]; if (!DOS_Canonicalize(name, full)) strcpy(full, name);
						char tail[128] = {0};
						PhysPt tailpt = Real2Phys(block.exec.cmdtail);
						Bit8u taillen = mem_readb(tailpt);
						if (taillen > 126) taillen = 126;
						MEM_BlockRead(tailpt + 1, tail, taillen);
						tail[taillen] = 0;
						for (char* t = tail; *t; t++) if (*t == '\\r' || *t == '\\n') *t = 0;
						// The subsystem word of the PE header: 3 is a console program,
						// which runs inside the DOS box under Win98 and is waited for.
						bool console = false;
						if (sig[0] == 'P')
						{
							Bit8u subsystem[2]; n = 2; Bit32u at2 = offset + 0x5c;
							if (DOS_SeekFile(fhandle,&at2,DOS_SEEK_SET) && DOS_ReadFile(fhandle,subsystem,&n) && n == 2) console = subsystem[0] == 3 && subsystem[1] == 0;
						}
						DOS_CloseFile(fhandle);
						DBP_Neo98LaunchWindows(full, tail[0] == ' ' ? tail + 1 : tail, console);
						dos.return_code = console ? (Bit8u)DBP_Neo98WindowsExitCode() : 0; dos.return_mode = RETURN_EXIT;
						return true;
					}
				}
				at = 0; DOS_SeekFile(fhandle,&at,DOS_SEEK_SET);
			}
		}
	}
''')
    return text


def patch_int10(text):
    """The text screen of page 0 for the host: Mark and Copy of the toolbar
    read the characters, not the pixels, as WINOLDAP reads the VM's video
    memory."""
    return text + '''
#include "vga.h"

// NEO98: the text screen of page 0, or false in a graphics mode.
bool DBP_Neo98TextScreen(unsigned *columns, unsigned *rows, unsigned short *cells, unsigned capacity)
{
	if (!CurMode || CurMode->type != M_TEXT || !CurMode->twidth || !CurMode->theight) return false;
	unsigned w = (unsigned)CurMode->twidth, h = (unsigned)CurMode->theight;
	// The rows the BIOS counts: 43 or 50 after an 8x8 font was loaded.
	const unsigned bios_rows = (unsigned)phys_readb(PhysMake(BIOSMEM_SEG, BIOSMEM_NB_ROWS)) + 1;
	if (bios_rows > h && bios_rows <= 60) h = bios_rows;
	if (w * h > capacity) return false;
	// This is a host inspection, not a guest virtual-memory access. DPMI
	// may have paging enabled and a transient VGA mapping while Glide starts.
	// Read the VGA backing store directly; mem_readw(B8000h) instead follows
	// the guest page tables and could invoke a handler without byte reads.
	const Bitu base = vga.svga.bank_read_full;
	for (unsigned i = 0; i < w * h; i++) {
		const Bitu offset = (base + i * 2) & (vga.vmemwrap - 1);
		cells[i] = host_readw(vga.mem.linear + offset);
	}
	*columns = w; *rows = h;
	return true;
}
// NEO98: the cursor of page 0: its cell from the BIOS, its scan lines as the
// VGA draws them and the height of a character cell in scan lines. The BIOS
// word keeps what the program asked for (06h..07h in CGA terms); a VGA BIOS
// scales that to the 16-line cell when it programs CRTC 0Ah/0Bh (cursor
// emulation), so the registers are what the screen shows.
bool DBP_Neo98TextCursor(unsigned *column, unsigned *row, unsigned *start, unsigned *end, unsigned *height)
{
	if (!CurMode || CurMode->type != M_TEXT) return false;
	const Bit16u position = phys_readw(PhysMake(BIOSMEM_SEG, BIOSMEM_CURSOR_POS));
	const Bit16u shape = phys_readw(PhysMake(BIOSMEM_SEG, BIOSMEM_CURSOR_TYPE));
	*column = position & 0xff; *row = position >> 8;
	*start = shape >> 8; *end = shape & 0xff;
	if (IS_EGAVGA_ARCH) { *start = vga.crtc.cursor_start & 0x3f; *end = vga.crtc.cursor_end & 0x1f; }
	*height = phys_readb(PhysMake(BIOSMEM_SEG, BIOSMEM_CHAR_HEIGHT));
	return true;
}
'''


def patch_cache(text):
    """The sidecar files of the writable C: of the Windows side,.neo98-index
    and.neo98-volume, are not DOS files and do not
    appear in a DOS directory."""
    # Upstream erases the synthetic dot entries at the drive root without
    # deleting their records. Linux directory enumeration exposes both.
    text = replace_once(text, '{ if (isroot) filelist.erase(it); goto founddot; }',
                        '{ if (isroot) { delete *it; filelist.erase(it); } goto founddot; }')
    text = replace_once(text, '{ if (isroot) filelist.erase(it); goto founddotdot; }',
                        '{ if (isroot) { delete *it; filelist.erase(it); } goto founddotdot; }')
    return replace_once(text, '''void DOS_Drive_Cache::CreateEntry(CFileInfo* dir, const char* name, bool is_directory) {
	CFileInfo* info = new CFileInfo;''', '''bool DBP_Neo98Active();
void DOS_Drive_Cache::CreateEntry(CFileInfo* dir, const char* name, bool is_directory) {
	if (DBP_Neo98Active() && !strncasecmp(name, ".neo98", 6)) return; // NEO98: the sidecar files of the host volume
	CFileInfo* info = new CFileInfo;''')


# The dynamic recompiler on Apple silicon. A page there is either writable or
# executable for a thread, never both: memory from mmap with MAP_JIT may hold
# code, and pthread_jit_write_protect_np switches the calling thread between
# writing and executing it. The switch is per thread. DOSBox Pure writes code
# on the thread that runs the machine (a block when it is translated) and on
# the thread that applies the core options (the default blocks of cache_init
# and of the cache reset); each of these places switches to writing before and
# back to executing after, so the resting state of every thread stays
# "execute". Linking and invalidating blocks write no code in the dynrec core:
# a block jumps to its successor through the pointer link[n].to of its
# CacheBlockDynRec, and invalidation only clears those records. After writing,
# sys_icache_invalidate makes the instruction cache see the new code. The
# approach follows dosbox-staging (src/cpu/dyn_cache.h: MAP_JIT in cache_init,
# dyn_mem_write, dyn_mem_execute and dyn_cache_invalidate;
# src/cpu/core_dynrec/decoder.h: the calls around CreateCacheBlock),
# GPL-2.0-or-later like DOSBox Pure.
JIT_HELPERS = '''
// NEO98: executable memory on Apple silicon, after dosbox-staging's
// dyn_mem_write, dyn_mem_execute and dyn_cache_invalidate
// (src/cpu/dyn_cache.h, GPL-2.0-or-later). tools/build_dosbox_pure.py.
#if defined(__APPLE__) && defined(__aarch64__)
#define DBP_NEO98_MAP_JIT 1
#include <sys/mman.h>
#include <pthread.h>
#include <libkern/OSCacheControl.h>
static inline void dyn_mem_write(void) { pthread_jit_write_protect_np(0); }
static inline void dyn_mem_execute(void) { pthread_jit_write_protect_np(1); }
static inline void dyn_cache_invalidate(const void *start, size_t size) { sys_icache_invalidate(const_cast<void *>(start), size); }
#else
static inline void dyn_mem_write(void) {}
static inline void dyn_mem_execute(void) {}
static inline void dyn_cache_invalidate(const void *start, size_t size) { __builtin___clear_cache((char *)start, (char *)start + size); }
#endif
'''


def patch_dyn_cache(text):
    """The code cache in MAP_JIT memory, and the switches around the code that
    cache_init and the cache reset write (see JIT_HELPERS)."""
    text = replace_once(text, 'class CodePageHandlerDynRec;\t// forward\n', JIT_HELPERS + '\nclass CodePageHandlerDynRec;\t// forward\n')
    text = replace_once(text, '''#else
			cache_code_start_ptr=(Bit8u*)malloc(CACHE_TOTAL+CACHE_MAXSIZE+PAGESIZE_TEMP-1+PAGESIZE_TEMP);
#endif
			if(!cache_code_start_ptr) E_Exit("Allocating dynamic cache failed");''', '''#elif defined(DBP_NEO98_MAP_JIT)
			cache_code_start_ptr=(Bit8u*)mmap(NULL, CACHE_TOTAL+CACHE_MAXSIZE+PAGESIZE_TEMP-1+PAGESIZE_TEMP,
				PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANON|MAP_JIT, -1, 0);
			if (cache_code_start_ptr == (Bit8u*)MAP_FAILED) cache_code_start_ptr = NULL;
#else
			cache_code_start_ptr=(Bit8u*)malloc(CACHE_TOTAL+CACHE_MAXSIZE+PAGESIZE_TEMP-1+PAGESIZE_TEMP);
#endif
			if(!cache_code_start_ptr) E_Exit("Allocating dynamic cache failed");''')
    text = replace_once(text, '''#if (C_HAVE_MPROTECT)
			if(mprotect(''', '''#if (C_HAVE_MPROTECT) && !defined(DBP_NEO98_MAP_JIT)
			if(mprotect(''')
    # cache_init: the default blocks for the block links and the run code.
    text = replace_once(text, '''		// setup the default blocks for block linkage returns
		cache.pos=&cache_code_link_blocks[0];
#ifndef WORDS_BIGENDIAN''', '''		// setup the default blocks for block linkage returns
		dyn_mem_write(); // NEO98
		cache.pos=&cache_code_link_blocks[0];
#ifndef WORDS_BIGENDIAN''')
    text = replace_once(text, '''		cache_block_closing(link_blocks[1].cache.start, cache.pos-link_blocks[1].cache.start);
#endif

		cache.free_pages=0;''', '''		cache_block_closing(link_blocks[1].cache.start, cache.pos-link_blocks[1].cache.start);
#endif
		dyn_mem_execute(); // NEO98
		dyn_cache_invalidate(cache_code_link_blocks, PAGESIZE_TEMP);

		cache.free_pages=0;''')
    text = replace_once(text, '''#elif defined(WIIU)
#else
		free(cache_code_start_ptr);
#endif''', '''#elif defined(WIIU)
#elif defined(DBP_NEO98_MAP_JIT)
		munmap(cache_code_start_ptr, CACHE_TOTAL+CACHE_MAXSIZE+PAGESIZE_TEMP-1+PAGESIZE_TEMP);
#else
		free(cache_code_start_ptr);
#endif''')
    return replace_once(text, '''		/* Setup the default blocks for block linkage returns */
		cache.pos=&cache_code_link_blocks[0];
		link_blocks[0].cache.start=cache.pos;
		dyn_return(BR_Link1,false);
		cache.pos=&cache_code_link_blocks[32];
		link_blocks[1].cache.start=cache.pos;
		dyn_return(BR_Link2,false);''', '''		/* Setup the default blocks for block linkage returns */
		dyn_mem_write(); // NEO98
		cache.pos=&cache_code_link_blocks[0];
		link_blocks[0].cache.start=cache.pos;
		dyn_return(BR_Link1,false);
		cache.pos=&cache_code_link_blocks[32];
		link_blocks[1].cache.start=cache.pos;
		dyn_return(BR_Link2,false);
		dyn_mem_execute(); // NEO98
		dyn_cache_invalidate(cache_code_link_blocks, 64);''')


def patch_decoder(text):
    """A block is translated with the thread switched to writing and runs only
    after the switch back and the invalidation of everything written, which
    may reach past the size of the block into the reserve at the end of the
    cache (see JIT_HELPERS)."""
    text = replace_once(text, '''	codepage->AddCacheBlock(decode.block);

	InitFlagsOptimization();''', '''	codepage->AddCacheBlock(decode.block);
	dyn_mem_write(); // NEO98

	InitFlagsOptimization();''')
    return replace_once(text, '''	decode.active_block->page.end=(Bit16u)decode.page.index;
//	LOG_MSG''', '''	decode.active_block->page.end=(Bit16u)decode.page.index;
	dyn_mem_execute(); // NEO98
	{
		const Bitu written=(Bitu)(cache.pos-decode.block->cache.start);
		dyn_cache_invalidate(decode.block->cache.start, written>decode.block->cache.size ? written : decode.block->cache.size);
	}
//	LOG_MSG''')


def patch_armv8(text):
    """The instruction cache after each block through the call of macOS; the
    DC CIVAC/IC IVAU loop of the backend is written for Linux."""
    return replace_once(text, '''static void cache_block_closing(const Bit8u* block_start,Bitu block_size) {
	//flush cache
	cache_flush((char *)block_start, (char *)(block_start+block_size));
}''', '''static void cache_block_closing(const Bit8u* block_start,Bitu block_size) {
	//flush cache
#if defined(DBP_NEO98_MAP_JIT)
	dyn_cache_invalidate(block_start, block_size); // NEO98
#else
	cache_flush((char *)block_start, (char *)(block_start+block_size));
#endif
}''')


def patch_voodoo(text):
    """The 3dfx Voodoo drawn by the host (tools/dosbox_pure_voodoo.inc).

    Upstream's OpenGL path records what the card draws on the DOS thread and
    plays the record with OpenGL on the thread of retro_run. The record stays;
    these patches add to it what the pixel pipeline of Gpu::Backend takes:

    1. A vertex also carries its place without upstream's OpenGL correction,
       the iterated Z and 1/w, the iterated alpha (upstream puts zaColor
       there for a depth compare against it) and s/w, t/w and 1/w per TMU.
    2. A command also carries fbzMode unmasked, zaColor, tLOD and tDetail.
    3. A texture upload also carries its mip levels.
    4. A read through the linear frame buffer takes the host's pixels: what
       is recorded goes to the host first, as a card empties its FIFO, and
       the host brings the buffer back (neo98_voodoo_read_pixel).
    """
    text = replace_once(text, '#include "dbp_threads.h"\n', '#include "dbp_threads.h"\n#include "neo98_dbp.h" // NEO98\n')
    text = replace_once(text, """	INT32 smax, tmax;
	UINT32 textureidx;
	rgb_t buf[256*256];
};""", """	INT32 smax, tmax;
	UINT32 textureidx;
	UINT32 neo98_levels; // NEO98: the mip levels in buf, the largest first
	rgb_t buf[256*256 + 256*128];
};""")
    text = replace_once(text, """	float fogblend, mlodblend[2];
	struct { float sw,tw,w; } m[2];
};""", """	float fogblend, mlodblend[2];
	struct { float sw,tw,w; } m[2];
	// NEO98: what the host's pixel pipeline iterates (dosbox_pure_voodoo.inc)
	float neo98_x, neo98_y, neo98_z, neo98_oow, neo98_a;
	struct { float s,t,q; } neo98_m[2];
};""")
    text = replace_once(text, """	UINT32 textureidx[2];
};

struct ogl_clipping""", """	UINT32 textureidx[2];
	UINT32 neo98_fbz_mode, neo98_zacolor, neo98_lod[2], neo98_detail[2]; // NEO98
};

struct ogl_clipping""")
    # The two pixel commands: every field is compared, so the new ones are set.
    text = replace_once(text, """		cmd.geometry.textureidx[0] = cmd.geometry.textureidx[1] = 0;
		vogl->cmdbuf.AddCommand(cmd);""", """		cmd.geometry.textureidx[0] = cmd.geometry.textureidx[1] = 0;
		cmd.geometry.neo98_fbz_mode = cmd.geometry.neo98_zacolor = cmd.geometry.neo98_lod[0] = cmd.geometry.neo98_lod[1] = cmd.geometry.neo98_detail[0] = cmd.geometry.neo98_detail[1] = 0; // NEO98
		vogl->cmdbuf.AddCommand(cmd);""")
    text = replace_once(text, """	cmd.geometry.textureidx[0] = cmd.geometry.textureidx[1] = 0;

	if (memcmp(&cmd.geometry, &vogl->cmdbuf.last_geometry, sizeof(cmd.geometry)))""", """	cmd.geometry.textureidx[0] = cmd.geometry.textureidx[1] = 0;
	cmd.geometry.neo98_fbz_mode = cmd.geometry.neo98_zacolor = cmd.geometry.neo98_lod[0] = cmd.geometry.neo98_lod[1] = cmd.geometry.neo98_detail[0] = cmd.geometry.neo98_detail[1] = 0; // NEO98

	if (memcmp(&cmd.geometry, &vogl->cmdbuf.last_geometry, sizeof(cmd.geometry)))""")
    # The triangle command.
    text = replace_once(text, """	cmd.geometry.textureidx[0] = cmd.geometry.textureidx[1] = 0;
	if (!FBIINIT3_DISABLE_TMUS(reg[fbiInit3].u) && FBZCP_TEXTURE_ENABLE(FBZCOLORPATH))""", """	cmd.geometry.textureidx[0] = cmd.geometry.textureidx[1] = 0;
	cmd.geometry.neo98_fbz_mode = FBZMODE; // NEO98
	cmd.geometry.neo98_zacolor = reg[zaColor].u;
	cmd.geometry.neo98_lod[0] = cmd.geometry.neo98_lod[1] = cmd.geometry.neo98_detail[0] = cmd.geometry.neo98_detail[1] = 0;
	if (!FBIINIT3_DISABLE_TMUS(reg[fbiInit3].u) && FBZCP_TEXTURE_ENABLE(FBZCOLORPATH))""")
    text = replace_once(text, """			cmd.geometry.eff.tex_mode[i] = (TEXMODE & VOODOO_OGL_TEXMODE_USEDBITS);
""", """			cmd.geometry.eff.tex_mode[i] = (TEXMODE & VOODOO_OGL_TEXMODE_USEDBITS);
			cmd.geometry.neo98_lod[i] = tmu.reg[tLOD].u; // NEO98
			cmd.geometry.neo98_detail[i] = tmu.reg[tDetail].u;
""")
    text = replace_once(text, """				//GFX_ShowMsg("[VOGL] Preparing texture #%d with id %d and texkey %08x and size %d,%d", textureidx, vogl->textures.data[textureidx].id, texturekey, smax, tmax);
""", """				neo98_voodoo_levels(tu, tmu, ilod, tformat); // NEO98
""")
    # The vertices of the triangle.
    text = replace_once(text, """		vd.a = (float)(fbi.starta + ((dy * fbi.dady)>>4) + ((dx * fbi.dadx)>>4)) / (float)(1<<20);
""", """		vd.a = (float)(fbi.starta + ((dy * fbi.dady)>>4) + ((dx * fbi.dadx)>>4)) / (float)(1<<20);
		vd.neo98_a = vd.a; // NEO98
		vd.neo98_x = (float)x / 16.0f;
		vd.neo98_y = (float)y / 16.0f;
		vd.neo98_m[0].s = vd.neo98_m[0].t = vd.neo98_m[0].q = vd.neo98_m[1].s = vd.neo98_m[1].t = vd.neo98_m[1].q = 0.0f;
""")
    text = replace_once(text, """		vd.d = (float)d / (float)0xffff;
""", """		vd.d = (float)d / (float)0xffff;
		vd.neo98_z = (float)iterz / 4096.0f; // NEO98: 20.12 to the 16 bit depth
		vd.neo98_oow = (float)((double)iterw / 4294967296.0); // 16.32
""")
    text = replace_once(text, """			const INT64 titert = tmu.startt + ((dy * tmu.dtdy)>>4) + ((dx * tmu.dtdx)>>4);
""", """			const INT64 titert = tmu.startt + ((dy * tmu.dtdy)>>4) + ((dx * tmu.dtdx)>>4);
			// NEO98: s/w and t/w in texels of a texture 256 wide, and 1/w
			vd.neo98_m[i].s = (float)((double)titers / 4294967296.0);
			vd.neo98_m[i].t = (float)((double)titert / 4294967296.0);
			vd.neo98_m[i].q = (TEXMODE_ENABLE_PERSPECTIVE(TEXMODE) ? (float)((double)titerw / 4294967296.0) : 1.0f);
""")
    # Reads through the linear frame buffer.
    text = replace_once(text, """	const ogl_pixels* pixels; UINT32 off; const UINT8* rgba;
""", """	if (neo98_voodoo_sink()) return neo98_voodoo_read_pixel(x, y); // NEO98
	const ogl_pixels* pixels; UINT32 off; const UINT8* rgba;
""")
    text = replace_once(text, """static INLINE void voodoo_ogl_texture_clear(UINT32 tmunum, UINT32 texbase1, UINT32 texbase2)
{""", """#include "neo98_voodoo.inc" // NEO98

static INLINE void voodoo_ogl_texture_clear(UINT32 tmunum, UINT32 texbase1, UINT32 texbase2)
{""")
    # Saved states. Upstream reads the frame buffers back
    # from OpenGL before it saves; here they are the host's, and the host
    # hands them back the way it answers a read through the linear frame
    # buffer (neo98_voodoo_write_back). A load forgets the record and the
    # textures, which belonged to the machine before the load, and has the
    # host take the buffers of the state with the next frame (neo98_restore).
    text = replace_once(text, """		if (vogl && (v_perf & V_PERFFLAG_OPENGL) && (ar.mode == DBPArchive::MODE_SAVE || ar.mode == DBPArchive::MODE_SIZE))
""", """		if (neo98_voodoo_sink() && (ar.mode == DBPArchive::MODE_SAVE || ar.mode == DBPArchive::MODE_SIZE)) neo98_voodoo_write_back(); // NEO98
		if (vogl && (v_perf & V_PERFFLAG_OPENGL) && !neo98_voodoo_sink() && (ar.mode == DBPArchive::MODE_SAVE || ar.mode == DBPArchive::MODE_SIZE)) // NEO98
""")
    text = replace_once(text, """			if (vogl_active && !usevogl) voodoo_ogl_state::Deactivate();
""", """			if (vogl_active && neo98_voodoo_sink()) voodoo_ogl_state::Deactivate(); // NEO98
			neo98_lfb.valid = neo98_lfb.pending = neo98_lfb.scanned = neo98_lfb.wanted = 0;
			if (neo98_voodoo_sink() && ar.mode == DBPArchive::MODE_LOAD) neo98_restore = true;
			if (vogl_active && !usevogl) voodoo_ogl_state::Deactivate();
""")
    return text


# The conditions of Jcc, SETcc and CMOVcc in the order of their low nibble.
CONDITIONS = ['O', 'NO', 'B', 'NB', 'Z', 'NZ', 'BE', 'NBE', 'S', 'NS', 'P', 'NP', 'L', 'NL', 'LE', 'NLE']


def patch_cmov_normal(text, width):
    """CMOVcc Gv,Ev (0F 40 to 0F 4F) in the interpreter. Upstream stops at
    the Pentium MMX; a program built for the Pentium Pro (gcc -march=i686)
    ended as an illegal opcode. The source operand is read whether or not the
    condition holds, as the processor does, so a bad address faults either way."""
    case, reg, ea, load, kind = (('CASE_0F_W', 'rmrw', 'earw', 'LoadMw', 'Bit16u') if width == 16 else
                                 ('CASE_0F_D', 'rmrd', 'eard', 'LoadMd', 'Bit32u'))
    get_reg, get_ea = ('GetRMrw', 'GetEArw') if width == 16 else ('GetRMrd', 'GetEArd')
    lines = ''.join(
        f"\t{case}(0x{0x40 + i:02x})\t\t\t\t\t\t\t\t\t\t\t\t/* CMOV{name} (NEO98) */\n"
        f"\t\t{{\n\t\t\t{get_reg};\n"
        f"\t\t\tif (rm >= 0xc0) {{{get_ea};if (TFLG_{name}) *{reg}=*{ea};}}\n"
        f"\t\t\telse {{GetEAa;const {kind} neo98_value={load}(eaa);if (TFLG_{name}) *{reg}=neo98_value;}}\n"
        f"\t\t\tbreak;\n\t\t}}\n" for i, name in enumerate(CONDITIONS))
    marker = f"\t{case}(0x80)\t\t\t\t\t\t\t\t\t\t\t\t/* JO */\n"
    return replace_once(text, marker, lines + marker)


def patch_fpu(text):
    """The x87 instructions of the Pentium Pro: FCMOVcc (DA C0 to DA DF,
    DB C0 to DB DF) and FCOMI, FUCOMI, FCOMIP, FUCOMIP (DB E8 to DB F7,
    DF E8 to DF F7), which compare into the flags of the processor."""
    text = replace_once(text, '#include "fpu_instructions.h"\n', """#include "fpu_instructions.h"
// NEO98: the comparisons of the Pentium Pro set ZF, PF and CF as FCOM sets
// C3, C2 and C0, clear OF, SF and AF, and leave the status word alone.
#include "../cpu/lazyflags.h"
static void FPU_NEO98_FCOMI(Bitu st, Bitu other, bool unordered) {
	const Bit16u saved = fpu.sw;
	if (unordered) FPU_FUCOM(st, other); else FPU_FCOM(st, other);
	const Bit16u compared = fpu.sw;
	fpu.sw = saved;
	FillFlags();
	SETFLAGBIT(ZF, compared & 0x4000);
	SETFLAGBIT(PF, compared & 0x0400);
	SETFLAGBIT(CF, compared & 0x0100);
	SETFLAGBIT(OF, false);
	SETFLAGBIT(SF, false);
	SETFLAGBIT(AF, false);
}
""")
    text = replace_once(text, """	switch(group){
	case 0x05:
		switch(sub){
		case 0x01:		/* FUCOMPP */""", """	switch(group){
	case 0x00: if (TFLG_B) FPU_FST(STV(sub),TOP); break;	/* FCMOVB (NEO98) */
	case 0x01: if (TFLG_Z) FPU_FST(STV(sub),TOP); break;	/* FCMOVE */
	case 0x02: if (TFLG_BE) FPU_FST(STV(sub),TOP); break;	/* FCMOVBE */
	case 0x03: if (TFLG_P) FPU_FST(STV(sub),TOP); break;	/* FCMOVU */
	case 0x05:
		switch(sub){
		case 0x01:		/* FUCOMPP */""")
    text = replace_once(text, """	switch (group) {
	case 0x04:
		switch (sub) {
		case 0x00:				//FNENI""", """	switch (group) {
	case 0x00: if (TFLG_NB) FPU_FST(STV(sub),TOP); break;	/* FCMOVNB (NEO98) */
	case 0x01: if (TFLG_NZ) FPU_FST(STV(sub),TOP); break;	/* FCMOVNE */
	case 0x02: if (TFLG_NBE) FPU_FST(STV(sub),TOP); break;	/* FCMOVNBE */
	case 0x03: if (TFLG_NP) FPU_FST(STV(sub),TOP); break;	/* FCMOVNU */
	case 0x05: FPU_NEO98_FCOMI(TOP,STV(sub),true); break;	/* FUCOMI */
	case 0x06: FPU_NEO98_FCOMI(TOP,STV(sub),false); break;	/* FCOMI */
	case 0x04:
		switch (sub) {
		case 0x00:				//FNENI""")
    text = replace_once(text, """	case 0x04:
		switch(sub){
			case 0x00:     /* FNSTSW AX*/""", """	case 0x05: FPU_NEO98_FCOMI(TOP,STV(sub),true); FPU_FPOP(); break;	/* FUCOMIP (NEO98) */
	case 0x06: FPU_NEO98_FCOMI(TOP,STV(sub),false); FPU_FPOP(); break;	/* FCOMIP */
	case 0x04:
		switch(sub){
			case 0x00:     /* FNSTSW AX*/""")
    return text


def patch_dyn_fpu(text):
    """The same x87 instructions in the recompiler: the handlers of the
    interpreter, called with the flags of the instructions before kept."""
    for number, condition in (('2', 'decode.modrm.reg < 4'),
                              ('3', '(decode.modrm.reg < 4 || decode.modrm.reg == 5 || decode.modrm.reg == 6)'),
                              ('7', '(decode.modrm.reg == 5 || decode.modrm.reg == 6)')):
        text = replace_once(text, f"static void dyn_fpu_esc{number}(){{\n\tdyn_get_modrm();  \n", f"""static void dyn_fpu_esc{number}(){{
	dyn_get_modrm();
	if (decode.modrm.mod == 3 && {condition}) {{ // NEO98: FCMOVcc, FCOMI and their kin
		AcquireFlags(FMASK_TEST);
		gen_call_function_I((void*)&FPU_ESC{number}_Normal,decode.modrm.val);
		return;
	}}
""")
    return text


def patch_dyn_opcodes(text):
    """CMOVcc in the recompiler: the condition as for a conditional jump,
    and the move skipped when it does not hold."""
    return replace_once(text, "static void dyn_loop(LoopTypes type) {\n", """// NEO98: CMOVcc Gv,Ev
static void dyn_cmov(BranchTypes btype) {
	dyn_get_modrm();
	AcquireFlags(FMASK_TEST);
	dyn_branchflag_to_reg(btype);
	const Bit8u* skip=gen_create_branch_on_zero(FC_RETOP,true);
	if (decode.modrm.mod<3) {
		dyn_fill_ea(FC_ADDR);
		dyn_read_word(FC_ADDR,FC_OP1,decode.big_op);
	} else {
		MOV_REG_WORD_TO_HOST_REG(FC_OP1,decode.modrm.rm,decode.big_op);
	}
	MOV_REG_WORD_FROM_HOST_REG(FC_OP1,decode.modrm.reg,decode.big_op);
	gen_fill_branch(skip);
}

static void dyn_loop(LoopTypes type) {
""")


def patch_dyn_decoder_cmov(text):
    marker = ("\t\t\t\t// short conditional jumps\n"
              "\t\t\t\tcase 0x80:case 0x81:case 0x82:case 0x83:case 0x84:case 0x85:case 0x86:case 0x87:\t\n")
    return replace_once(text, marker, """				// NEO98: conditional moves
				case 0x40:case 0x41:case 0x42:case 0x43:case 0x44:case 0x45:case 0x46:case 0x47:
				case 0x48:case 0x49:case 0x4a:case 0x4b:case 0x4c:case 0x4d:case 0x4e:case 0x4f:
					dyn_cmov((BranchTypes)(dual_code&0xf));
					break;

""" + marker)


def patch_cpu_type(name):
    """The processor types pentium_pro and pentium_ii: what a program asks
    CPUID for before it uses the instructions above. Family 6 with FPU, time
    stamp counter and CMOV, the Pentium II with MMX as well (patch_config);
    memory privilege checks as the Pentium."""
    def cpu_h(text):
        return replace_once(text, "#define CPU_ARCHTYPE_PENTIUMSLOW	0x50\n",
                            "#define CPU_ARCHTYPE_PENTIUMSLOW	0x50\n#define CPU_ARCHTYPE_PENTIUM_PRO		0x60 // NEO98\n"
                            "#define CPU_ARCHTYPE_PENTIUM_II		0x65 // NEO98\n")

    def cpu_cpp(text):
        text = replace_once(text, """			reg_edx=0x00000011;	/* FPU+TimeStamp/RDTSC */
""", """			reg_edx=0x00000011;	/* FPU+TimeStamp/RDTSC */
		} else if (CPU_ArchitectureType==CPU_ARCHTYPE_PENTIUM_PRO) { // NEO98
			reg_eax=0x611;		/* intel pentium pro */
			reg_ebx=0;			/* Not Supported */
			reg_ecx=0;			/* No features */
			reg_edx=0x00008011;	/* FPU+TimeStamp/RDTSC+CMOV */
		} else if (CPU_ArchitectureType==CPU_ARCHTYPE_PENTIUM_II) { // NEO98
			reg_eax=0x633;		/* intel pentium ii */
			reg_ebx=0;			/* Not Supported */
			reg_ecx=0;			/* No features */
			reg_edx=0x00808011;	/* FPU+TimeStamp/RDTSC+CMOV+MMX */
""")
        text = replace_once(text, """			CPU_ArchitectureType = CPU_ARCHTYPE_PENTIUMSLOW;
""", """			CPU_ArchitectureType = CPU_ARCHTYPE_PENTIUMSLOW;
		} else if (cputype == "pentium_pro") { // NEO98
			CPU_ArchitectureType = CPU_ARCHTYPE_PENTIUM_PRO;
		} else if (cputype == "pentium_ii") { // NEO98
			CPU_ArchitectureType = CPU_ARCHTYPE_PENTIUM_II;
""")
        # The reset value of DR6 is the Pentium's from the Pentium on.
        return replace_once(text, """		if (CPU_ArchitectureType==CPU_ARCHTYPE_PENTIUMSLOW) {
#else""", """		if (CPU_ArchitectureType>=CPU_ARCHTYPE_PENTIUMSLOW) { // NEO98: the Pentium Pro too
#else""")

    def paging(text):
        marker = "case CPU_ARCHTYPE_PENTIUMSLOW:\n"
        assert text.count(marker) == 3, 'the Pentium cases of paging.cpp'
        return text.replace(marker, marker.rstrip('\n') + " case CPU_ARCHTYPE_PENTIUM_PRO: case CPU_ARCHTYPE_PENTIUM_II: // NEO98\n")

    def dosbox(text):
        marker = '"pentium_slow", "'
        assert text.count(marker) == 2, 'the processor types of dosbox.cpp'
        return text.replace(marker, '"pentium_slow", "pentium_pro", "pentium_ii", "')

    def options(text):
        return replace_once(text, """			{ "pentium_slow", "Pentium (slow) - 586 instruction set with memory privilege checks" },
""", """			{ "pentium_slow", "Pentium (slow) - 586 instruction set with memory privilege checks" },
			{ "pentium_pro", "Pentium Pro (slow) - 686 instruction set with CMOV" },
			{ "pentium_ii", "Pentium II (slow) - 686 instruction set with CMOV and MMX" },
""")
    return {'cpu_h': cpu_h, 'cpu_cpp': cpu_cpp, 'paging': paging, 'dosbox': dosbox, 'options': options}[name]


def chain(*functions):
    def run(text):
        for function in functions:
            text = function(text)
        return text
    return run


def patch_config(text):
    """MMX on. Upstream ships the instructions (core_normal/prefix_0f_mmx.h,
    core_dynrec/dyn_mmx.h) and builds them only where it tested them."""
    return replace_once(text, "#define C_MMX 0 ", "#define C_MMX 1 ")


PATCHES = {
    'include/config.h': patch_config,
    'src/cpu/core_normal/prefix_0f.h': lambda text: patch_cmov_normal(text, 16),
    'src/cpu/core_normal/prefix_66_0f.h': lambda text: patch_cmov_normal(text, 32),
    'src/fpu/fpu.cpp': patch_fpu,
    'src/cpu/core_dynrec/dyn_fpu.h': patch_dyn_fpu,
    'src/cpu/core_dynrec/decoder_opcodes.h': patch_dyn_opcodes,
    'include/cpu.h': patch_cpu_type('cpu_h'),
    'src/cpu/cpu.cpp': chain(patch_cpu_type('cpu_cpp'), patch_exception_trace),
    'src/cpu/paging.cpp': chain(patch_cpu_type('paging'), patch_paging_busy),
    'src/cpu/callback.cpp': patch_callback_busy,
    'src/dosbox.cpp': patch_cpu_type('dosbox'),
    'core_options.h': patch_cpu_type('options'),
    'src/hardware/voodoo.cpp': patch_voodoo,
    'src/cpu/dyn_cache.h': patch_dyn_cache,
    'src/cpu/core_dynrec/decoder.h': chain(patch_decoder, patch_dyn_decoder_cmov),
    'src/cpu/core_dynrec/risc_armv8le.h': patch_armv8,
    'dosbox_pure_libretro.cpp': patch_libretro,
    'src/shell/shell.cpp': patch_shell,
    'src/shell/shell_cmds.cpp': patch_shell_cmds,
    'src/dos/dos_execute.cpp': patch_execute,
    'src/ints/int10.cpp': patch_int10,
    'src/dos/drive_cache.cpp': patch_cache,
    'src/dbp_serialize.cpp': patch_serialize,
}


def patch_mouse(text):
    # A Voodoo game keeps VGA in text mode but sets its own INT33 range.
    # Respect that range independent of which CPU mode dispatches the event.
    text = replace_once(text, 'Bit16s min_x,max_x,min_y,max_y;',
                        'Bit16s min_x,max_x,min_y,max_y;\n\tbool neo98_absolute_range;')
    text = replace_once(text, 'CurMode && CurMode->type == M_TEXT && AllowINT33RMAccess()',
                        'CurMode && CurMode->type == M_TEXT && !mouse.neo98_absolute_range')
    text = replace_once(text, 'x*real_readw(BIOSMEM_SEG,BIOSMEM_NB_COLS)*8',
                        'x*phys_readw(PhysMake(BIOSMEM_SEG,BIOSMEM_NB_COLS))*8')
    text = replace_once(text, 'y*(IS_EGAVGA_ARCH?(real_readb(BIOSMEM_SEG,BIOSMEM_NB_ROWS)+1):25)*8',
                        'y*(IS_EGAVGA_ARCH?(phys_readb(PhysMake(BIOSMEM_SEG,BIOSMEM_NB_ROWS))+1):25)*8')
    text = replace_once(text, 'mouse.mode = mode;',
                        'mouse.mode = mode;\n\tmouse.neo98_absolute_range = false;')
    for axis in ('x', 'y'):
        text = replace_once(text, 'mouse.min_%s=min;' % axis,
                            'mouse.neo98_absolute_range = true;\n\t\t\tmouse.min_%s=min;' % axis)
    return text


PATCHES['src/ints/mouse.cpp'] = patch_mouse


def patch_serial(text):
    """A 16550 whose line is the host's (tools/dosbox_pure_serial.inc) for
    every port the host is attached to, whatever the core's own setting for
    it says, and a notice to it on every register write: that is when the
    machine takes the port. """
    text = replace_once(text, '#include "nullmodem.h"\n', '#include "nullmodem.h"\n#include "neo98_serial.inc" // NEO98\n')
    text = replace_once(text, """	if(serialports[i]==0) return;
	
#if SERIAL_DEBUG
		const char* const dbgtext[]={"THR","IER","FCR",""", """	if(serialports[i]==0) return;
	DBP_Neo98SerialTouched(i); // NEO98
	
#if SERIAL_DEBUG
		const char* const dbgtext[]={"THR","IER","FCR",""")
    return replace_once(text, """			if (type=="dummy") {
				serialports[i] = new CSerialDummy (i, &cmd);""", """			Neo98DbpState* neo98 = DBP_Neo98State(); // NEO98
			if (neo98->serial_attached && neo98->serial_attached(neo98->serial_owner, (unsigned)i)) {
				serialports[i] = new CSerialNeo98 (i, &cmd);
			}
			else if (type=="dummy") {
				serialports[i] = new CSerialDummy (i, &cmd);""")


PATCHES['src/hardware/serialport/serialport.cpp'] = patch_serial


def patch_bios_names(text):
    """The identification of the system BIOS a DOS program reads
    and (MSD, a game's setup): the machine is the port's, as its
    devices are on the Windows side (src/neo98_hardware.h). The
    words "IBM COMPATIBLE" stay; programs look for them."""
    text = replace_once(text, '"IBM COMPATIBLE 486 BIOS COPYRIGHT The DOSBox Team."',
                        '"IBM COMPATIBLE 486 BIOS COPYRIGHT NEO98." /* NEO98 */')
    return replace_once(text, '"DOSBox FakeBIOS v1.0"', '"NEO98 BIOS v1.0" /* NEO98 */')


def patch_vesa_names(text):
    """The vendor, product and revision VBE function 4F00h reports. The OEM
    string stays: it names the chip the core is (S3 Trio64), and programs
    choose their driver by it."""
    text = replace_once(text, 'string_vendorname[]="DOSBox Development Team"', 'string_vendorname[]="NEO98" /* NEO98 */')
    text = replace_once(text, 'string_productname[]="DOSBox - The DOS Emulator"',
                        'string_productname[]="NEO98 Display Adapter" /* NEO98 */')
    return replace_once(text, 'string_productrev[]="DOSBox " VERSION', 'string_productrev[]="NEO98 1.0" /* NEO98 */')


PATCHES['src/ints/bios.cpp'] = patch_bios_names
PATCHES['src/ints/int10_vesa.cpp'] = patch_vesa_names


# Native continuation fields live in the versioned NEO98 provider head,
# never in DBPSerialize_All's core archive. Preserve the previous archive
# identity for this explicitly compatible extension; the build identity
# still includes all patched code and the new helpers.
# The two display-name-only patches introduced by the hardware work also
# leave the legacy archive untouched. Keep those names out of its schema
# identity when this branch is integrated with that work.
ARCHIVE_PATCHES = dict(PATCHES)
EXTRA['neo98_resume.h'] = ROOT / 'tools/dosbox_pure_resume.h'
EXTRA['neo98_resume.inc'] = ROOT / 'tools/dosbox_pure_resume.inc'
INCLUDED_BY['neo98_resume.inc'] = 'src/cpu/callback.cpp'
EXTRA['neo98_dynrec_unwind.h'] = ROOT / 'tools/dosbox_pure_dynrec_unwind.h'
INCLUDED_BY['neo98_dynrec_unwind.h'] = 'src/cpu/core_dynrec.cpp'
EXPORTS += ['_DBP_Neo98ResumeSize', '_DBP_Neo98ResumeSave', '_DBP_Neo98ResumeLoad', '_DBP_Neo98ResumeLegacySafe']


def patch_callback_resume(text):
    text = replace_once(text, '#include "callback.h"\n', '#include "callback.h"\n#include "neo98_resume.h"\n')
    # Each native callback restores its own return address after its guest
    # routine returns. Those addresses must belong to the saved call chain.
    for function, tag in (('CALLBACK_Idle', '0x300'), ('CALLBACK_RunRealFar', '0x200'), ('CALLBACK_RunRealInt', '0x100 + intnum')):
        start = text.index('void ' + function + '(')
        end = text.index('\n}', start)
        part = text[start:end]
        anchor = 'Bit32u oldeip=reg_eip;' if function == 'CALLBACK_Idle' else 'Bit16u oldcs=SegValue(cs);'
        fields = 'auto cs = oldcs; auto ip = oldeip; '
        if function == 'CALLBACK_Idle':
            fields += 'auto flags = oldIF; ar << cs << ip << flags; if (apply && !ar.had_error) oldIF = flags; '
        else:
            fields += 'ar << cs << ip; '
        fields += 'if (apply && !ar.had_error) { oldcs = cs; oldeip = ip; }'
        part = replace_once(part, anchor, anchor + '\n\tNeo98ResumeFrame neo98_frame(' + tag + ', [&](DBPArchive& ar, bool apply) { ' + fields + ' });')
        text = text[:start] + part + text[end:]
    text = text.replace('neo98_nested_runs++;\n\tDOSBOX_RunMachine();\n\tneo98_nested_runs--;',
                        'Neo98NestedRun neo98_run(neo98_nested_runs);\n\tDOSBOX_RunMachine();')
    return text + '\n#include "neo98_resume.inc"\n'


def patch_shutdown_resume(text):
    text = replace_once(text, '#include "shell.h"\n', '#include "shell.h"\n#include "neo98_resume.h"\n')
    text = replace_once(text, '\tlooprecursion++;\n', '\tNeo98ScopeCount<Bit32u> neo98_depth(looprecursion);\n')
    text = replace_once(text, '\tif (--looprecursion == 0)\n', '\tif (looprecursion == 1)\n')
    text = replace_once(text, '\t} while (!ret);\n\tPAGE_FAULT_CATCH', '''\t} while (!ret);
\t// A forced stop did not complete pending guest callbacks or faults.
\t// Unwind their native callers instead of running their remaining work.
\tif (loop == DBP_ShutdownCPU::Loop && first_shell && first_shell->exit)
\t\tthrow Neo98DosShutdown();
\tPAGE_FAULT_CATCH''')
    return text + '''
void DOSBOX_NEO98_Shutdown()
{
#ifdef C_DBP_PAGE_FAULT_QUEUE_WIPE
    DBP_FreePageFaultWipe();
    DOSBOX_IsWipingPageFaultQueue = false;
#endif
}
'''


def patch_shell_shutdown(text):
    text = replace_once(text, '#include "shell.h"\n', '#include "shell.h"\n#include "neo98_resume.h"\n')
    return replace_once(text, '\tfirst_shell->Run();\n\tdelete first_shell;',
                        '\ttry { first_shell->Run(); }\n\tcatch (const Neo98DosShutdown&) { PAGING_NEO98_Shutdown(); DOSBOX_NEO98_Shutdown(); }\n\tdelete first_shell;')


def patch_paging_shutdown(text):
    return text + '''
// The guest is stopped and all native fault callers have unwound.
void PAGING_NEO98_Shutdown()
{
    pf_queue.used = 0;
    paging_prevent_exception_jump = false;
}
'''


def patch_dynrec_shutdown(text):
    # The FDE is valid for this reviewed emitter ABI only. A new register
    # allocation or prologue must be reviewed with its unwind description.
    architecture = PATCHES['src/cpu/core_dynrec/risc_armv8le.h'](
        (SOURCE / 'src/cpu/core_dynrec/risc_armv8le.h').read_text())
    for required in ('#define FC_ADDR HOST_r19', '#define FC_REGS_ADDR HOST_r20',
                     '#define FC_SEGS_ADDR HOST_r21', '#define readdata_addr HOST_r22',
                     'cache_addd( 0xa9bd7bfd )', 'cache_addd( 0x910003fd )',
                     'STP64_IMM(FC_ADDR, FC_REGS_ADDR, HOST_sp, 16)',
                     'STP64_IMM(FC_SEGS_ADDR, readdata_addr, HOST_sp, 32)',
                     'LDP64_IMM(FC_ADDR, FC_REGS_ADDR, HOST_sp, 16)',
                     'LDP64_IMM(FC_SEGS_ADDR, readdata_addr, HOST_sp, 32)',
                     'cache_addd( 0xa8c37bfd )'):
        assert required in architecture, 'ARM64 dynrec unwind ABI changed: ' + required
    start = architecture.index('static void gen_run_code(void)')
    end = architecture.index('#ifdef DRC_FLAGS_INVALIDATION', start)
    assert hashlib.sha256(architecture[start:end].encode()).hexdigest() == \
        '69733f87a3ec3cac32ea4ce6872cb31c78151264e3f2ee78f23ae427a4a642f0', \
        'ARM64 dynrec unwind ABI changed: complete entry/return emitters'
    text = replace_once(text, '#include "dyn_cache.h"',
                        '#include "dyn_cache.h"\n#include "neo98_dynrec_unwind.h"\nstatic Neo98DynrecUnwind neo98_dynrec_unwind;')
    text = replace_once(text, 'else if (!enable_cache && cache_initialized) cache_close();',
                        'else if (!enable_cache && cache_initialized) { neo98_dynrec_unwind.Reset(); cache_close(); }')
    return replace_once(text,
        '\n}\n\n//void CPU_Core_Dynrec_Cache_Close',
        '\n\tif (cache_initialized) neo98_dynrec_unwind.Set(cache_code, CACHE_TOTAL + CACHE_MAXSIZE);\n}\n\n//void CPU_Core_Dynrec_Cache_Close')


def patch_console_resume(text):
    text = replace_once(text, '#include "dos_inc.h"\n', '#include "dos_inc.h"\n#include "neo98_resume.h"\n')
    return replace_once(text, '\tBit16u count=0;\n\tINT10_SetCurMode();\n', '''\tBit16u count=0;
    INT10_SetCurMode();
    const Bit16u neo98_capacity = *size;
    Neo98ResumeFrame neo98_frame(0x400, [&](DBPArchive& ar, bool apply) {
        Bit16u ax = oldax, consumed = count, request = *size;
        Bit8u cache = readcache;
        ar << ax << consumed << cache << request;
        if (consumed > request) { ar.had_error = DBPArchive::ERR_LAYOUT; return; }
        if (request > neo98_capacity) { ar.had_error = DBPArchive::ERR_GAMENOTRUNNING; return; }
        std::vector<Bit8u> prefix(consumed);
        if (ar.mode != DBPArchive::MODE_LOAD && consumed) memcpy(prefix.data(), data, consumed);
        if (consumed) ar.SerializeBytes(prefix.data(), consumed);
        if (apply && !ar.had_error) {
            oldax = ax; count = consumed; readcache = cache; *size = request;
            if (consumed) memcpy(data, prefix.data(), consumed);
        }
    });
''')


def patch_dos_resume(text):
    text = replace_once(text, '#include "dosbox.h"\n', '#include "dosbox.h"\n#include "neo98_resume.h"\n')
    text = replace_once(text, '\t\t\tBit8u read=0;Bit8u c;Bit16u n=1;\n', '''\t\t\tBit8u read=0;Bit8u c=0;Bit16u n=1;
            Neo98ResumeFrame neo98_frame(0x500, [&](DBPArchive& ar, bool apply) {
                auto address = data; auto room = free; auto used = read; auto character = c; auto amount = n;
                ar << address << room << used << character << amount;
                if (used > room || amount > 1) ar.had_error = DBPArchive::ERR_LAYOUT;
                if (apply && !ar.had_error) { data = address; free = room; read = used; c = character; n = amount; }
            });
''')
    return replace_once(text, '\t\t\tBit16u toread=DOS_GetAmount();\n', '''\t\t\tBit16u toread=DOS_GetAmount();
            Neo98ResumeFrame neo98_frame(0x600, [&](DBPArchive& ar, bool apply) {
                auto amount = toread; ar << amount; if (apply && !ar.had_error) toread = amount;
            });
''')


def patch_vesa_resume(text):
    text = replace_once(text, '#include "dosbox.h"\n', '#include "dosbox.h"\n#include "neo98_resume.h"\n')
    text = replace_once(text, 'Bit8u VESA_SetPalette(PhysPt data,Bitu index,Bitu count,bool wait) {', '''Bit8u VESA_SetPalette(PhysPt data,Bitu index,Bitu count,bool wait) {
    Neo98ResumeFrame neo98_frame(0x700, [&](DBPArchive& ar, bool apply) {
        auto address = data; auto first = index; auto amount = count;
        ar << address << first << amount;
        if (first > 255 || amount > 256 - first) ar.had_error = DBPArchive::ERR_LAYOUT;
        if (apply && !ar.had_error) { data = address; index = first; count = amount; }
    });
''')
    return replace_once(text, 'Bit8u VESA_SetDisplayStart(Bit16u x,Bit16u y,bool wait) {', '''Bit8u VESA_SetDisplayStart(Bit16u x,Bit16u y,bool wait) {
    // The only work after waiting for retrace is returning VESA_SUCCESS.
    Neo98ResumeFrame neo98_frame(0x701, [](DBPArchive&, bool) {});
''')


def patch_bios_resume(text):
    text = replace_once(text, '#include "dosbox.h"\n', '#include "dosbox.h"\n#include "neo98_resume.h"\n')
    return replace_once(text, '\t\t\tdouble timeout=PIC_FullIndex()+((double)count/1000.0)+1.0;\n', '''\t\t\tdouble timeout=PIC_FullIndex()+((double)count/1000.0)+1.0;
            Neo98ResumeFrame neo98_frame(0x800, [&](DBPArchive& ar, bool apply) {
                auto deadline = timeout; ar << deadline;
                if (!(deadline >= 0 && deadline < 1e15)) ar.had_error = DBPArchive::ERR_LAYOUT;
                if (apply && !ar.had_error) timeout = deadline;
            });
''')


def patch_resume_busy(text):
    text = replace_once(text, '#include "neo98_dbp.h" // NEO98\n', '#include "neo98_dbp.h" // NEO98\n#include "neo98_resume.h"\n')
    return replace_once(text, 'PAGING_NEO98_InFault() ? 1 : 0;', '(PAGING_NEO98_InFault() || !Neo98ResumeSafe()) ? 1 : 0;')


def patch_paste_keyboard(text):
    return text + '\nunsigned KEYBOARD_Neo98Free() { return KEYBUFSIZE - keyb.used; }\n'


def patch_paste_bios(text):
    return text + """
unsigned BIOS_Neo98PasteRoom()
{
    const unsigned start = machine == MCH_PCJR ? 0x1e : mem_readw(BIOS_KEYBOARD_BUFFER_START);
    const unsigned end = machine == MCH_PCJR ? 0x3e : mem_readw(BIOS_KEYBOARD_BUFFER_END);
    const unsigned head = mem_readw(BIOS_KEYBOARD_BUFFER_HEAD), tail = mem_readw(BIOS_KEYBOARD_BUFFER_TAIL);
    if (end <= start || head < start || head >= end || tail < start || tail >= end) return 0;
    return ((head + end - start - tail - 2) % (end - start)) / 2;
}
bool BIOS_Neo98CapsLock() { return (mem_readb(BIOS_KEYBOARD_FLAGS1) & 0x40) != 0; }
"""


PATCHES['src/hardware/keyboard.cpp'] = patch_paste_keyboard
PATCHES['src/ints/bios_keyboard.cpp'] = patch_paste_bios
PATCHES['src/cpu/callback.cpp'] = chain(PATCHES['src/cpu/callback.cpp'], patch_callback_resume)
PATCHES['src/dos/dev_con.h'] = patch_console_resume
PATCHES['src/dos/dos.cpp'] = patch_dos_resume
PATCHES['src/dbp_serialize.cpp'] = chain(PATCHES['src/dbp_serialize.cpp'], patch_resume_busy)
PATCHES['src/ints/int10_vesa.cpp'] = chain(PATCHES['src/ints/int10_vesa.cpp'], patch_vesa_resume)
PATCHES['src/ints/bios.cpp'] = chain(PATCHES['src/ints/bios.cpp'], patch_bios_resume)
PATCHES['src/dosbox.cpp'] = chain(PATCHES['src/dosbox.cpp'], patch_shutdown_resume)
PATCHES['src/shell/shell.cpp'] = chain(PATCHES['src/shell/shell.cpp'], patch_shell_shutdown)
PATCHES['src/cpu/paging.cpp'] = chain(PATCHES['src/cpu/paging.cpp'], patch_paging_shutdown)
PATCHES['src/cpu/core_dynrec.cpp'] = patch_dynrec_shutdown

def patch_adc(text):
    text = replace_once(text, 'struct SB_INFO {', 'struct SB_INFO {\n    struct { bool active, paused, autoinit; Bitu left, block, batch; } adc;')
    text = replace_once(text, 'static SB_INFO sb;', 'static SB_INFO sb;\nstatic void DSP_ADC_Stop();')
    text = replace_once(text, 'static void DSP_Reset(void) {', 'static void DSP_Reset(void) {\n\tDSP_ADC_Stop();')
    start=text.index('static void DSP_ADC_CallBack(')
    end=text.index('static void DSP_ChangeRate(',start)
    text=text[:start]+'#include "neo98_adc.inc"\n\n'+text[end:]
    start=text.index('\tcase 0x24:\t/* Singe Cycle 8-Bit DMA ADC */')
    end=text.index('\tcase 0x14:',start)
    text=text[:start]+'\tcase 0x24: /* Single-cycle ADC, count is length minus one. */\n        DSP_ADC_Start(1+sb.dsp.in.data[0]+(sb.dsp.in.data[1]<<8),false);\n        break;\n    case 0x2c: /* Auto-init ADC uses the block size set by 48h. */\n        DSP_SB2_ABOVE;\n        DSP_ADC_Start(sb.dma.autosize ? sb.dma.autosize : 65536,true);\n        break;\n'+text[end:]
    text=replace_once(text, 'DSP_AddData(0x7f);   // fake silent input for Creative parrot','DSP_AddData(DSP_ADC_Direct());')
    text=replace_once(text, '\tcase 0x2c:\n\tcase 0x98:', '\tcase 0x98:')
    text=replace_once(text, '\tcase 0xd0:\t/* Halt 8-bit DMA */', '\tcase 0xd0:\t/* Halt 8-bit DMA */\n        if(sb.adc.active){sb.adc.paused=true;PIC_RemoveEvents(DSP_ADC_Event);break;}')
    text=replace_once(text, '\tcase 0xd4:\t/* Continue DMA 8-bit*/', '\tcase 0xd4:\t/* Continue DMA 8-bit*/\n        if(sb.adc.active){sb.adc.paused=false;DSP_ADC_Schedule();break;}')
    text=replace_once(text, '\t\tsb.dma.autoinit=false;\t\t//Should stop itself', '\t\tsb.dma.autoinit=false;\t\t//Should stop itself\n        sb.adc.autoinit=false;')
    text=replace_once(text, 'DSP_FinishReset, DSP_RaiseIRQEvent);', 'DSP_FinishReset, DSP_RaiseIRQEvent, DSP_ADC_Event);')
    # A DAC command cancels an ADC transfer, as a DSP reset does.
    for name in ('DSP_PrepareDMA_Old', 'DSP_PrepareDMA_New'):
        begin=text.index('static void '+name+'(');brace=text.index('{',begin)
        text=text[:brace+1]+'\n    DSP_ADC_Stop();'+text[brace+1:]
    return text

PATCHES['src/hardware/sblaster.cpp'] = patch_adc

def patched_sources():
    return {name: function((SOURCE / name).read_text()) for name, function in PATCHES.items()}


def sources():
    """The translation units of the upstream Makefile: *.cpp, src/*.cpp,
    src/*/*.cpp and src/*/*/*.cpp, relative to the tree."""
    names = []
    for pattern in ('*.cpp', 'src/*.cpp', 'src/*/*.cpp', 'src/*/*/*.cpp'):
        names += [str(p.relative_to(SOURCE)) for p in SOURCE.glob(pattern)]
    return sorted(names)


def identity(patched):
    digest = hashlib.sha256()
    for name in sorted(patched):
        digest.update(name.encode())
        digest.update(hashlib.sha256(patched[name].encode()).digest())
    digest.update(INTERFACE.read_bytes())
    for name in sorted(EXTRA):
        digest.update(EXTRA[name].read_bytes())
    digest.update(json.dumps(FLAGS).encode())
    digest.update(json.dumps(sources()).encode())
    try:
        digest.update(subprocess.check_output([CXX, '--version'], text=True, stderr=subprocess.DEVNULL).encode())
    except Exception:
        digest.update(b'unknown clang++')
    digest.update((SOURCE / 'neo98-import.json').read_bytes())
    return digest.hexdigest()


# These complete source hashes pin only the reviewed, archive-neutral
# continuation addition. Editing a native field, a callback, or the helper
# invalidates the legacy key until compatibility is deliberately reviewed.
RESUME_COMPAT_SOURCES = {
    'src/cpu/core_dynrec.cpp': '65338673776370021502cb3e21ecaa160825d081a371eb54ee151a2e9c58beba',
    'src/cpu/callback.cpp': '19bcaa8647f3b90c330a0ba5d1d548fdc80c0614d97d0d0f67a5f86855adbd5c',
    'src/dos/dev_con.h': 'c648b92b2d3fedf90641175019440e402debcf52cb45e1bb2f3a858c7bcf319a',
    'src/dos/dos.cpp': '5929f50ce5a4b85d591df5eb9baf9247e915002e398d0e2f9e0d15393fe145ee',
    'src/dbp_serialize.cpp': '02896c85d4a7cdadde1a065bff669affca5034dcbd67e52dc7b469c7fd6d212a',
    'src/ints/bios.cpp': '2b1ab5d7f3c7e165bfaea3c954d99ce59d307d691d28c7257aed1b9e5a1ff762',
    'src/ints/int10_vesa.cpp': '7ec31908a96c699720e5ee45487f5e132f5d2de35ae9c7867a0eed64336fdd41',
    'src/cpu/paging.cpp': '8856f0b56a6251a655528d4ec02198f673db71d679ea0c159423d9a7913a08f7',
    'src/dosbox.cpp': '903adfc62153a1766886e20d06d92c53d9cabd221eb86af0e0b962ccdbdf560e',
    'src/shell/shell.cpp': '6ae55f4a72251f29c6a7d1de97b868c895b6102d7ab3bb8890d866ff2097dab4',
}
RESUME_COMPAT_EXTRA = {
    'neo98_dynrec_unwind.h': '3fff7084504d002a4c68b84b722447022a88dcb3756565c57cd6d8d6bd8ad8c4',
    'neo98_resume.h': '66da3d718ddac1adf913c59d0ecf96358772a253ffc8b3eaff0bb7bea4817caf',
    'neo98_resume.inc': '1d937175a7dc852c09284831129a8627e09f5361e34abad27ca623170f5bd6c9',
}


def archive_identity_sources():
    patched = patched_sources()
    extra = dict(EXTRA)
    helper_matches = all(hashlib.sha256(EXTRA[name].read_bytes()).hexdigest() == expected
                         for name, expected in RESUME_COMPAT_EXTRA.items())
    if helper_matches:
        for name, expected in RESUME_COMPAT_SOURCES.items():
            if hashlib.sha256(patched[name].encode()).hexdigest() != expected:
                break
        else:
            for name in RESUME_COMPAT_SOURCES:
                if name in ARCHIVE_PATCHES:
                    patched[name] = ARCHIVE_PATCHES[name]((SOURCE / name).read_text())
                else:
                    del patched[name]
            for name in RESUME_COMPAT_EXTRA:
                del extra[name]
    # Undo exactly the known branding literals. Additional native changes
    # leave a difference and therefore stay in the strict archive hash.
    names = {
        'src/ints/bios.cpp': (
            ('"IBM COMPATIBLE 486 BIOS COPYRIGHT NEO98." /* NEO98 */', '"IBM COMPATIBLE 486 BIOS COPYRIGHT The DOSBox Team."'),
            ('"NEO98 BIOS v1.0" /* NEO98 */', '"DOSBox FakeBIOS v1.0"')),
        'src/ints/int10_vesa.cpp': (
            ('string_vendorname[]="NEO98" /* NEO98 */', 'string_vendorname[]="DOSBox Development Team"'),
            ('string_productname[]="NEO98 Display Adapter" /* NEO98 */', 'string_productname[]="DOSBox - The DOS Emulator"'),
            ('string_productrev[]="NEO98 1.0" /* NEO98 */', 'string_productrev[]="DOSBox " VERSION')),
    }
    for name, replacements in names.items():
        normalized = patched[name]
        for current, prior in replacements:
            normalized = normalized.replace(current, prior)
        if normalized == (SOURCE / name).read_text():
            del patched[name]
    return patched, extra


def state_identity():
    """What a saved state of the DOS machine depends on:
    the patched sources, the interface and the files
    built into it, the flags and the import. Not the compiler's version, and
    nothing of NEO98 beside the core: a state is laid out by the structures
    of these sources. Twelve hex digits; the host is compiled with them
    (tools/native_app.py) and writes them into every state."""
    patched, extra = archive_identity_sources()
    digest = hashlib.sha256()
    for name in sorted(patched):
        digest.update(name.encode())
        digest.update(hashlib.sha256(patched[name].encode()).digest())
    digest.update(INTERFACE.read_bytes())
    for name in sorted(extra):
        digest.update(extra[name].read_bytes())
    digest.update(json.dumps(FLAGS).encode())
    digest.update(json.dumps(sources()).encode())
    digest.update((SOURCE / 'neo98-import.json').read_bytes())
    return digest.hexdigest()[:12]


def clone_tree(target):
    shutil.rmtree(target, ignore_errors=True)
    target.parent.mkdir(parents=True, exist_ok=True)
    # The resolved path: where third_party/dosbox-pure is a link (a worktree
    # that shares main's imports), cp -R would copy the link, and the patches
    # would then be written into the pinned source itself.
    origin = SOURCE.resolve()
    if subprocess.run(['cp', '-Rc', str(origin), str(target)], capture_output=True).returncode:
        subprocess.run(['cp', '-R', str(origin), str(target)], check=True, capture_output=True)
    if target.is_symlink() or target.resolve() == origin:
        raise SystemExit(f'{target} is not a copy of {origin}; refusing to patch the pinned source')


def compile_one(source, objects, environment):
    """One translation unit. The object is named after the text it was
    compiled from, so a change to one patched file recompiles that file alone
    and the rest is reused from the previous build."""
    relative = source
    digest = hashlib.sha256()
    digest.update(json.dumps(FLAGS).encode())
    digest.update((OUT / 'source' / relative).read_bytes())
    for name in PATCHES:
        if name.endswith('.h') or name == 'dosbox_pure_libretro.cpp':
            digest.update((OUT / 'source' / name).read_bytes())
    digest.update((OUT / 'source/neo98_dbp.h').read_bytes())
    for name in sorted(EXTRA):
        # The code of the Voodoo is included by one file alone.
        if name in INCLUDED_BY and relative != INCLUDED_BY[name]:
            continue
        digest.update((OUT / 'source' / name).read_bytes())
    target = objects / (relative.replace('/', '~') + '.' + digest.hexdigest()[:16] + '.o')
    if target.exists():
        return relative, 0, '', target
    command = [CXX, *FLAGS, '-I' + str(OUT / 'source/include'), '-I' + str(OUT / 'source'),
               '-c', str(OUT / 'source' / relative), '-o', str(target)]
    result = subprocess.run(command, capture_output=True, text=True, env=environment, cwd=OUT / 'source')
    return relative, result.returncode, result.stderr, target


def library(force=False, jobs=None):
    """The built archive, rebuilt when the stamp no longer matches."""
    from unicorn_dependencies import ensure
    ensure('dosbox-pure')
    patched = patched_sources()
    stamp_path, result = OUT / 'stamp.json', OUT / 'lib' / LIBRARY_NAME
    want = identity(patched)
    if not force and result.exists() and (OUT / 'lib' / DYLIB_NAME).exists():
        try:
            if json.loads(stamp_path.read_text())['identity'] == want:
                return result
        except (OSError, ValueError, KeyError):
            pass
    source, objects = OUT / 'source', OUT / 'objects'
    clone_tree(source)
    for name, text in patched.items():
        (source / name).write_text(text)
    shutil.copy2(INTERFACE, source / 'neo98_dbp.h')
    for name, origin in EXTRA.items():
        shutil.copy2(origin, source / name)
    objects.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ)
    # Indirect host builds need the same control as the --jobs entry point.
    workers = max(1, jobs or int(os.environ.get('NEO98_DOSBOX_JOBS', min(4, os.cpu_count() or 1))))
    failed = []
    built = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        for relative, code, stderr, target in pool.map(lambda s: compile_one(s, objects, environment), sources()):
            if code:
                failed.append((relative, stderr))
            else:
                built.append(target)
    if failed:
        for relative, stderr in failed:
            print(f'--- {relative}\n{stderr}', file=sys.stderr)
        raise SystemExit(f'{len(failed)} translation units of DOSBox Pure failed')
    result.parent.mkdir(parents=True, exist_ok=True)
    if result.exists():
        result.unlink()
    archiver = 'llvm-ar' if sys.platform.startswith('linux') else 'ar'
    subprocess.run([archiver, 'rcs', str(result), *map(str, built)], check=True,
                   env=neo98.tool_environment() if sys.platform.startswith('linux') else environment)
    dylib = OUT / 'lib' / DYLIB_NAME
    exports = OUT / 'lib' / 'exports.txt'
    if sys.platform.startswith('linux'):
        exports.write_text('{ global: ' + ' '.join(name[1:] + ';' for name in EXPORTS) + ' local: *; };\n')
    else:
        exports.write_text('\n'.join(EXPORTS) + '\n')
    temporary = dylib.with_suffix(f'.{os.getpid()}.dylib')
    link_flags = ['-shared', '-fuse-ld=lld', '-Wl,-Bsymbolic', '-Wl,--version-script=' + str(exports)] if sys.platform.startswith('linux') else [
        '-dynamiclib', '-Wl,-exported_symbols_list,' + str(exports), '-install_name', '@rpath/' + DYLIB_NAME]
    subprocess.run([CXX, *link_flags, '-pthread', '-flto', '-O2', *map(str, built), '-o', str(temporary)], check=True,
                   env=neo98.tool_environment() if sys.platform.startswith('linux') else environment)
    os.replace(temporary, dylib)
    stamp_path.write_text(json.dumps({'identity': want,
                                      'version': json.loads((SOURCE / 'neo98-import.json').read_text())['version'],
                                      'patched': sorted(patched), 'objects': len(built)}, indent=1) + '\n')
    return result


def core_dylib(force=False, jobs=None):
    """The dynamic library of the core, built with the archive."""
    library(force=force, jobs=jobs)
    return OUT / 'lib' / DYLIB_NAME


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--force', action='store_true', help='rebuild even when the stamp matches')
    parser.add_argument('--jobs', type=int, default=None,
                        help='parallel compilers, NEO98_DOSBOX_JOBS or at most four by default')
    arguments = parser.parse_args()
    print(library(force=arguments.force, jobs=arguments.jobs))
