# The FreeDOS boot sector

Some Windows 98 Second Edition CDs can't boot on their own. They contain
everything Setup needs, but no boot image. For those, Neo98's setup builds a
small boot floppy itself, from the DOS files on the user's CD and a 512-byte
boot sector from the FreeDOS project. No second Windows image, FreeDOS
installation or download is needed.

## How Setup starts from such a CD

1. The FreeDOS boot sector on the floppy loads `WINBOOT.SYS` from the user's
   CD, renamed to `IO.SYS`. From there on, the DOS that comes with Windows 98
   runs.
2. DOS prepares drive C: with `FDISK /MBR` and `SYS A: C:`, both from the
   CD, and restarts.
3. The second start comes from drive C:. DOS loads the CD-ROM driver, finds
   the CD and starts Windows Setup.

The DOS files come out of the cabinet files on the CD (`BASE5.CAB` and the
`EBD.CAB` inside it). Everything is staged in the setup's private work
folder; nothing is written into the app. The code is in `tools/win98_boot.py`.

## Source and license

The boot sector is `oemboot.asm` from the
[FreeDOS kernel](https://github.com/FDOS/kernel), release `ke2046`, commit
`5ffb5502d39a10a30f5b8a9e8beeba0bf30245d3`. It is licensed under the GNU
General Public License, version 2 or any later version. The unmodified source,
its macro file `magic.mac` and the license are in `tools/freedos_boot`, next
to the assembled binary, and they ship with the app and with every release.

To rebuild the binary and check that it matches:

```sh
nasm -DISFAT12 -DWINBOOT -f bin -Itools/freedos_boot/ \
  tools/freedos_boot/oemboot.asm -o /tmp/win98-fat12.bin
cmp tools/freedos_boot/win98-fat12.bin /tmp/win98-fat12.bin
```

`WINBOOT` selects the boot convention of Windows 9x. Setup checks the
binary's SHA-256 before it uses it:

```text
16fc322080d3363e920edd677824d3f5c0d880620decd78d0fba7ed6b1ccfcf2  win98-fat12.bin
e1d63e42c9b54bdb151f81f23e0c4739bd7ebc327ae64838d98e953bb7283c0d  oemboot.asm
8723d23e082ba5ee5c5f4fd1ad22c441097f84b5328fd079b2004090186b1090  magic.mac
0f1e68d9b4a580cdcca4c5f4e3b8046f7a8759da05edd109cff081bc484b3c4b  COPYING
```

`python3 tools/test_win98_boot.py -v` checks the files, the checksums and the
reproducible build. With `NEO98_BOOT_TEST_ISO` pointing to a CD image, it also
boots that CD in QEMU up to the start of Windows Setup, on a temporary disk.
