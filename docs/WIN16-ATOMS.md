# The local atoms of 16-bit programs

KERNEL's local atoms (InitAtomTable, AddAtom, FindAtom, DeleteAtom,
GetAtomName, GetAtomHandle: KERNEL.68 to 73) belong to the segment in DS
when they are called. KRNL386 keeps them in that segment's local heap:

- the word at DS:8 points to the table, a word with the number of buckets
  (37 when InitAtomTable is called with zero, or when AddAtom or FindAtom
  finds no table) followed by the first entry of each bucket;
- an entry is a fixed block of the local heap: a word pointing to the next
  entry of the bucket, a word of usage (saturating at FFFFh), a byte of
  length and the name as it was first given, with its terminating zero;
- the bucket comes from a hash over the upper case of the name, and names
  are compared without regard to case;
- a string atom is the near pointer of its entry shifted right by two, ORed
  with C000h, and GetAtomHandle is the atom shifted left by two again;
- an integer atom (a far pointer with selector zero, or a name "#nnn") is
  its number when that is between 1 and BFFFh, and has no entry.

Programs use this layout directly. The SETUPX of Windows 95 (it imports
AddAtom, FindAtom, DeleteAtom and GetAtomName; that of Windows 98 none of
them) keeps the source and target names of its copy queue as local atoms
and reads them with `LocalLock(atom << 2) + 5` out of its own segment. With
the atoms in one table outside the segment, as the port first had them,
every name SETUPX read was empty: no file of a queue was found in a
cabinet, and SETUPX asked for the installation disk, for example after OK
in the Network applet ([NETWORK-APPLET.md](NETWORK-APPLET.md)).

The port runs KRNL386's algorithm (`src/win98_atoms.h`, which also serves
the 32-bit tables of `src/x86_atoms.cpp`) over the caller's data segment,
staged and written back with the other local heap operations
(`Application::Win16LocalOperation`, `src/ne_loader.cpp`). The global
atoms of USER (GlobalAddAtom and the registered names) stay in their own
tables.

## Test

`tools/test_win16_local_atoms_host.py` with
`tools/local_atoms_win16_fixture.asm`: the table at DS:8, the entry behind
GetAtomHandle with its length, usage and name, the case of the names,
DeleteAtom, GetAtomName and the integer atoms.
