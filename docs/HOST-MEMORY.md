# What Windows programs cost the host in memory

A Windows 98 desktop keeps many processes: the shell, every open program,
drivers that run 16-bit code inside them. This page says what one of them
costs the computer Neo98 runs on, what goes back when a program frees or
ends something, and how to measure both. The numbers are from Linux x64,
where the x86 code runs on the processor itself.

## One process

| | before | now |
| --- | --- | --- |
| the smallest program with a window | 46.5 MB | 4.8 MB |
| its private pages alone | 35.2 MB | 1.2 MB |
| 64 such programs, resident memory of the host | 3.1 GB | 383 MB |
| the shell (Explorer as the desktop) | 433 MB | 191 MB |
| the shell with ten small programs (Notepad, Calculator, Paint) | 964 MB | 300 MB |
| after 32 programs ended, more than before they started | 169 MB, every time | 3 MB |

A Windows process reserves far more than it touches: 32 MiB for its heap
arena, 2 MiB for its stack and a megabyte of gates before its first
instruction. Three rules keep that from costing memory.

**A guest page costs memory when it is written.** The private pages of a
process are a file in memory (`tools/host_direct_cpu.inc`). Mapping a range
used to zero it, which made the host back every reserved page. A new page of
the file reads as zero by itself, and `Unmap` gives the pages of a range back
to the host (a hole in the file), so a range that is mapped a second time
reads as zero too. What a program frees with `VirtualFree` or by ending a
thread is free for the host at once, not when the process ends.

**A table costs what is used of it.** The processor keeps a byte for every
page of the 4 GiB in four tables; they are anonymous memory, backed where a
guest has pages. The process object itself is 5.2 MB of tables with room
for the worst case (128 threads, 16384 regions of virtual memory). After its
construction, `Application::ReturnZeroPages` hands every page of it that
holds only zeros back to the host; such a page reads as zero as before and is
backed again when something is written to it. 1 MB of the object stays.

**The end of a process returns everything.** The object goes back whole, the
file of private pages is closed, and the heap of the host is trimmed
(`malloc_trim`), because the blocks a process had there leave free pages
between blocks that live on.

What is left of the 4.8 MB: 1.2 MB of private pages (a megabyte of them the
gates), another megabyte for the gates as the processor prepares them, 1 MB
of the process object (thread records and other tables whose initial
values are not zero), 1.2 MB on the heap of the host (import records, the
VGA memory of the system VM), 0.3 MB of the system VM. The gates are the
same bytes in every process until a process binds a fast gate into them;
sharing them between processes would save 2 MB each and is not done.

A process that ends leaves its shared heap behind until the session needs
the record (there are 96): Windows 98 keeps a destroyed shared heap readable,
and programs read it. That is why the host holds a few megabytes more after
the first programs have come and gone, and no more after later ones.

`tools/test_process_cost_host.py` holds the bounds and prints the tables:

```sh
build/analysis-venv/bin/python tools/test_process_cost_host.py
build/analysis-venv/bin/python tools/test_process_cost_host.py --table small shell --detail
```

It reads `/proc/PID/smaps_rollup` of the host and the blocks of each of its
memory files. The resident count alone leaves out pages of a memory file
that are mapped nowhere at the moment, which is the case for the prepared
gates of every process whose turn it is not.

## Changing from one process to the next

The processes take their turns at the same 32-bit addresses. A change maps
each run of pages of the new process that has one protection with one call,
so the number of runs is what a change costs.

A readable page is executable from the start on the native processor. The
execute right used to be given at the first jump into a page, as it is for
the emulating engines, where an executable page makes every write to it
expensive. The native processor has no such cost, and each page that became
executable alone split a run in three: a program that calls into 32
separate pages of data ended with 82 runs instead of 19. With
`NEO98_NATIVE_STATS=1` a processor prints its runs when it ends.

`Cpu::Protect` makes one call for a range instead of one for each of its
pages.

## 16-bit memory

A 32-bit process that uses an original 16-bit driver (a film through
MCIAVI.DRV, a printer driver) has a 16-bit global heap of its own, with one
selector of its local descriptor table for every block and one for every
64 KiB of a larger block. The table has 2048 entries. What is freed has to
come back, or a program that plays films all day runs out:

- `mciSendString("close ...")` closes the device of a 16-bit driver the way
  `mciSendCommand(MCI_CLOSE)` does, with MCI_CLOSE_DRIVER and CloseDriver.
  It used to forget the device only, and MCIAVI kept the thread that plays
  the film with everything the film had taken.
- A freed selector leaves the descriptor table the processor loads. LAR and
  LSL fail on it, and a data segment register that still holds it is loaded
  as the null selector at the next entry.
- The selectors of a block larger than 64 KiB take the lowest run of free
  entries instead of new ones at the end of the table.
- A block allocated with GMEM_SHARE belongs to the module whose code asked
  for it and is freed when that module is unloaded, as KRNL386 does
  (GlobalFreeAll from the last FreeLibrary). AVIFILE.DLL relies on it.
- The 16-bit stack every thread gets that reaches 16-bit code goes back
  when the thread ends.

A thread identity is a word. After 65535 threads of a session the count
starts again and passes over the identities still in use; it used to stop
there, and with it every CreateThread.

`tools/test_reopen_host.py` opens and closes a film, a sound and a 16-bit
program fifty times each in one process and starts 70000 threads, and
compares the descriptors (counted with LAR by the program itself) and the
memory of the host after the second round and after the last.

## Device bitmaps

The bits of all device bitmaps of a session are memory of the host process.
Their bound is a quarter of the memory of the host, between 128 MiB and
2 GiB (`BitmapStore`, `src/gdi_bitmap.inc`); it was 128 MiB on every host.
A bitmap larger than 64 KiB takes its bits twice, once for the block of the
original GDI code and once for the copy the renderer draws in.
`NEO98_BITMAP_MEMORY=<megabytes>` sets the bound. A `CreateCompatibleBitmap`
beyond it answers NULL with ERROR_NOT_ENOUGH_MEMORY and the program goes on
(`tools/test_bitmap_memory_host.py`). Windows 98 has no such number: the
bits are global memory there, and the memory of the machine is the bound.
DIB sections are counted apart, in the shared address space of the guest.
