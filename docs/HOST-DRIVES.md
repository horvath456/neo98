# Drives that are folders of the host

Drive C: and every other drive of Neo98 is a folder of the host. Windows
programs see it as a FAT32 drive; programs of the host may read and write
the same folder while Windows runs. This page says how the two stay in
agreement. The code is `tools/host_volume.h` (the folders) and
`src/files.cpp` (the drive as Windows programs see it).

## What a folder holds

A host folder cannot hold everything a FAT32 directory holds, so every
folder of a drive carries a small file `.neo98-index` beside its entries:
one line per entry with the DOS attribute byte, the creation time FAT
keeps and the 8.3 alias. Access and write times and the size come from the
host file itself. A file put into the folder by hand has no line yet; it
gets one the next time Windows looks into the folder.

## Drive C: over another folder

Drive C: is normally the Windows folder itself. It can also be a folder of
its own above the Windows folder (`--drive-c` beside `--system-tree`, and
the hard disk of a game machine): the Windows folder below is then never
written, and everything Windows changes lands in the folder above, the
layer. A file of the folder below that Windows deletes stays there; the
layer's `.neo98-index` keeps a line for it marked as removed (a tombstone),
so it stays deleted after a restart as well.

## Changes made by the host

Neo98 does not watch the folders. It remembers what it read of a folder
together with the folder's inode and the time its entries last changed,
and compares the two whenever Windows asks for a name that is not there and
whenever a program starts to list a directory. A file the host added,
removed or renamed in the meantime changes that time, and the folder is
read again. A name Windows finds is checked against the host file as well,
so a file the host deleted is gone for Windows at once.

This holds for the folder below a layer and for the layer's own folder:

| The host... | Windows sees, at its next look |
| --- | --- |
| puts a new file into the drive's folder | the file |
| writes a file under a name Windows deleted before | the new file, also when the deleted file was one of the folder below a layer: the file in the layer's folder ends the tombstone |
| deletes a file | that the file is gone |
| changes a file | the new size, times and contents |

A look costs one `stat` of the folder per name that is not found, the same
as before the layer's own folder was compared too:
`tools/test_host_recreated_file.py` measures 2000 calls of
`GetFileAttributes` on a missing file at about 100 ms on a Linux PC.

## Tests

`tools/test_host_recreated_file.py` writes files on the host while a
program of its own looks for them: a name asked for in vain, a file Windows
wrote and deleted, a file of the start Windows deleted (in C:\ and in
C:\WINDOWS) and a deleted file in a folder Windows made, each found by
`GetFileAttributes`, `FindFirstFile` by name and by a pattern, and read
through `CreateFile`; once with drive C: as the Windows folder and once
with a layer above it.
