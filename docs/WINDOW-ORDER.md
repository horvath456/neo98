# The order of windows

Top-level windows stand in one list, front to back. The topmost windows
(`WS_EX_TOPMOST`) come first, as a band of their own, then the others. An
owned window stands above its owner: USER's owner pass
(`src/win98_owner_zorder.h`) moves the windows a window owns along with it
whenever it changes its place.

## Owners and the topmost band

`SetWindowPos` with `HWND_TOPMOST` or `HWND_NOTOPMOST` changes the band of
more than the one window, as Windows documents it: "When a non-topmost
window is made topmost, its owned windows are also made topmost. Its owners,
however, are not changed. ... When a topmost window is made non-topmost, its
owners and its owned windows are also made non-topmost windows."

| Request | The window | Its owned windows | Its owners |
| --- | --- | --- | --- |
| `HWND_TOPMOST` | topmost, at the top of the band | topmost, above it | as they were |
| `HWND_NOTOPMOST` | ordinary, at the top of the others | ordinary, above it | ordinary, below it |

Neo98 moved only the window: an owner made topmost went to the top of the
band and its owned window stayed outside it, below the owner, where an
`HWND_TOP` of any other window could even come between them. The owned
windows change band before the owner pass orders them (`OwnedBand` in
`src/user.cpp`), and a window that changes band has its owned windows put
above it again; the owners of a window leaving the band stand right below
it. An owned window made topmost after its owner leaves the owner in the
band.

`tools/test_owned_topmost_host.py` (`tools/owned_topmost_fixture.c`) follows
a plain window, an owner and its owned popup through three cases: the owner
made topmost and then the owned window, the owned window alone, and the
owner made topmost and the owned window made non-topmost.
