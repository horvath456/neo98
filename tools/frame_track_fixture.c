/* SPDX-License-Identifier: GPL-2.0-or-later */
/* USER's nonclient hit test, WM_SETCURSOR, the SCROLLBAR control and the
 * message order of the move, size and scrollbar trackers. Error codes
 * 43800..44199 are reserved for this fixture. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API U RegisterClassA(U *);
API U CreateWindowExA(U, const char *, const char *, U, int, int, int, int, U, U, U, U);
API U DefWindowProcA(U, U, U, U);
API U DestroyWindow(U);
API U IsWindow(U);
API U ShowWindow(U, U);
API U GetWindowRect(U, int *);
API U GetClientRect(U, int *);
API U ScreenToClient(U, int *);
API U SendMessageA(U, U, U, U);
API U PostMessageA(U, U, U, U);
API U GetMessageA(U *, U, U, U);
API U TranslateMessage(U *);
API U DispatchMessageA(U *);
API U LoadCursorA(U, U);
API U SetCursor(U);
API U GetCursor(void);
API int GetSystemMetrics(int);
API U GetWindowLongA(U, int);
API U SetScrollInfo(U, int, U *, U);
API U GetScrollInfo(U, int, U *);
API U SetScrollPos(U, int, int, U);
API U GetScrollPos(U, int);
API U SetScrollRange(U, int, int, int, U);
API U GetScrollRange(U, int, int *, int *);
API U EnableScrollBar(U, int, U);
API U MoveWindow(U, int, int, int, int, U);

static void check(int ok, U code) { if (!ok) ExitProcess(code); }

/* The recorded message order. The runner drives the input and then posts
 * WM_APP with a phase number; the fixture checks its own trace and clears it. */
#define TRACE 256
static U trace[TRACE], used;
static void record(U value) { if (used < TRACE) trace[used++] = value; }
static U track_window, parent_refusal, child_window, control_window, grip_window;
static U scroll_owner_code, scroll_owner_position, scroll_owner_control, scroll_owner_count;

static int seen(U value)
{
    U i;
    for (i = 0; i < used; ++i) if (trace[i] == value) return 1;
    return 0;
}
/* The index of the first occurrence, or -1. */
static int first(U value)
{
    U i;
    for (i = 0; i < used; ++i) if (trace[i] == value) return (int)i;
    return -1;
}

static U __stdcall track_proc(U h, U m, U w, U l)
{
    if (h == track_window)
    {
        switch (m)
        {
        case 0x0112: record(0x1000000u | (w & 0xffff)); break;   /* WM_SYSCOMMAND */
        case 0x0231: record(0x231); break;                       /* WM_ENTERSIZEMOVE */
        case 0x0232: record(0x232); break;                       /* WM_EXITSIZEMOVE */
        case 0x0024: record(0x24); break;                        /* WM_GETMINMAXINFO */
        case 0x0214: record(0x2000000u | (w & 0xffff)); break;   /* WM_SIZING */
        case 0x0216: record(0x216); break;                       /* WM_MOVING */
        case 0x0046: record(0x46); break;                        /* WM_WINDOWPOSCHANGING */
        case 0x0047: record(0x47); break;                        /* WM_WINDOWPOSCHANGED */
        case 0x0003: record(3); break;                           /* WM_MOVE */
        case 0x0005: record(5); break;                           /* WM_SIZE */
        case 0x0114: case 0x0115:
            record(0x3000000u | ((m & 1) << 16) | (w & 0xffff));
            scroll_owner_code = w & 0xffff;
            scroll_owner_position = w >> 16;
            scroll_owner_control = l;
            ++scroll_owner_count;
            break;
        default: break;
        }
    }
    if (h == parent_refusal && m == 0x20) return 1;              /* WM_SETCURSOR refused */
    if (h == parent_refusal && (m == 0x0114 || m == 0x0115))
    {
        scroll_owner_code = w & 0xffff;
        scroll_owner_position = w >> 16;
        scroll_owner_control = l;
        ++scroll_owner_count;
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static U hit(U h, int x, int y)
{
    return DefWindowProcA(h, 0x84, 0, ((U)y << 16) | ((U)x & 0xffff));
}

/* Phase 0: the complete result set over a sizable window, a dialog frame and a
 * maximized window. */
static void hit_test_matrix(void)
{
    int r[4], client[4], origin[2];
    const int frame_x = GetSystemMetrics(32), frame_y = GetSystemMetrics(33);
    const int size_x = GetSystemMetrics(30);
    U h = CreateWindowExA(0, "TrackPort", "sizable", 0x10cf0000 | 0x00300000, 60, 70, 420, 320, 0, 0, 0, 0);
    check(h, 43800);
    check(GetWindowRect(h, r), 43801);
    check(r[2] - r[0] == 420 && r[3] - r[1] == 320, 43802);
    /* Outside the window at all. */
    check(hit(h, r[0] - 1, r[1]) == 0, 43803);
    /* The sizing border and its corners. One caption button long each. */
    check(hit(h, r[0], r[1]) == 13, 43804);                       /* HTTOPLEFT */
    check(hit(h, r[2] - 1, r[1]) == 14, 43805);                   /* HTTOPRIGHT */
    check(hit(h, r[0], r[3] - 1) == 16, 43806);                   /* HTBOTTOMLEFT */
    check(hit(h, r[2] - 1, r[3] - 1) == 17, 43807);               /* HTBOTTOMRIGHT */
    check(hit(h, (r[0] + r[2]) / 2, r[1]) == 12, 43808);          /* HTTOP */
    check(hit(h, (r[0] + r[2]) / 2, r[3] - 1) == 15, 43809);      /* HTBOTTOM */
    check(hit(h, r[0], (r[1] + r[3]) / 2) == 10, 43810);          /* HTLEFT */
    check(hit(h, r[2] - 1, (r[1] + r[3]) / 2) == 11, 43811);      /* HTRIGHT */
    /* The corner is exactly one caption button past the frame. */
    check(hit(h, r[0] + frame_x + size_x - 1, r[1] + 1) == 13, 43812);
    check(hit(h, r[0] + frame_x + size_x, r[1] + 1) == 12, 43813);
    /* The caption: its icon, its text and its buttons. */
    check(hit(h, r[0] + frame_x + 2, r[1] + frame_y + 2) == 3, 43814);   /* HTSYSMENU */
    check(hit(h, (r[0] + r[2]) / 2, r[1] + frame_y + 2) == 2, 43815);    /* HTCAPTION */
    check(hit(h, r[2] - frame_x - 2, r[1] + frame_y + 2) == 20, 43816);  /* HTCLOSE */
    check(hit(h, r[2] - frame_x - size_x - 2, r[1] + frame_y + 2) == 9, 43817);  /* HTMAXBUTTON */
    check(hit(h, r[2] - frame_x - 2 * size_x - 2, r[1] + frame_y + 2) == 8, 43818); /* HTMINBUTTON */
    /* The client area and the two bars around it. */
    check(GetClientRect(h, client), 43819);
    origin[0] = origin[1] = 0;
    check(ScreenToClient(h, origin), 43820);
    check(hit(h, -origin[0] + client[2] / 2, -origin[1] + client[3] / 2) == 1, 43821);
    check(hit(h, -origin[0] + client[2] + 2, -origin[1] + client[3] / 2) == 7, 43822);
    check(hit(h, -origin[0] + client[2] / 2, -origin[1] + client[3] + 2) == 6, 43823);
    /* The square between the two bars sizes, so it is the bottom right corner. */
    check(hit(h, -origin[0] + client[2] + 2, -origin[1] + client[3] + 2) == 17, 43824);
    /* A maximized window does not size, so the same square is only HTSIZE. */
    check(ShowWindow(h, 3), 43825);
    check(GetClientRect(h, client), 43826);
    origin[0] = origin[1] = 0;
    check(ScreenToClient(h, origin), 43827);
    check(hit(h, -origin[0] + client[2] + 2, -origin[1] + client[3] + 2) == 4, 43828);
    check(DestroyWindow(h), 43829);

    /* A dialog frame has no sizing border: every edge is HTBORDER. */
    h = CreateWindowExA(0, "TrackPort", "dialog", 0x80c80000, 60, 70, 300, 220, 0, 0, 0, 0);
    check(h, 43830);
    check(GetWindowRect(h, r), 43831);
    check(hit(h, r[0], r[1]) == 18, 43832);
    check(hit(h, (r[0] + r[2]) / 2, r[1]) == 18, 43833);
    check(hit(h, r[2] - 1, r[3] - 1) == 18, 43834);
    check(hit(h, (r[0] + r[2]) / 2, r[1] + GetSystemMetrics(8) + 2) == 2, 43835);
    check(hit(h, r[0] - 1, r[1]) == 0, 43836);
    check(DestroyWindow(h), 43837);

    /* A window with a menu bar reports HTMENU over it. */
    h = CreateWindowExA(0, "TrackPort", "popup", 0x90000000, 60, 70, 300, 220, 0, 0, 0, 0);
    check(h, 43838);
    check(GetWindowRect(h, r), 43839);
    check(hit(h, (r[0] + r[2]) / 2, (r[1] + r[3]) / 2) == 1, 43840);
    check(DestroyWindow(h), 43841);
}

/* Phase 1: DefWindowProc's WM_SETCURSOR choice. */
static void set_cursor_choice(void)
{
    const U beam = LoadCursorA(0, 32513), arrow = LoadCursorA(0, 32512);
    const U we = LoadCursorA(0, 32644), nwse = LoadCursorA(0, 32642);
    const U ns = LoadCursorA(0, 32645), nesw = LoadCursorA(0, 32643);
    U cls[10] = {0, (U)track_proc, 0, 0, 0, 0, 0, 0, 0, (U)"CursorPort"};
    U h, child;
    check(beam && arrow && we && nwse && ns && nesw, 43850);
    check(beam != arrow && we != nwse, 43851);
    cls[6] = beam;                                  /* hCursor */
    check(RegisterClassA(cls), 43852);
    h = CreateWindowExA(0, "CursorPort", "cursor", 0x10cf0000, 40, 40, 300, 220, 0, 0, 0, 0);
    check(h, 43853);
    /* The client area takes the class cursor. */
    SetCursor(arrow);
    check(!DefWindowProcA(h, 0x20, h, 1 | (0x200u << 16)), 43854);
    check(GetCursor() == beam, 43855);
    /* The frame takes the sizing arrow the hit code names. */
    check(!DefWindowProcA(h, 0x20, h, 10 | (0x200u << 16)) && GetCursor() == we, 43856);
    check(!DefWindowProcA(h, 0x20, h, 12 | (0x200u << 16)) && GetCursor() == ns, 43857);
    check(!DefWindowProcA(h, 0x20, h, 13 | (0x200u << 16)) && GetCursor() == nwse, 43858);
    check(!DefWindowProcA(h, 0x20, h, 16 | (0x200u << 16)) && GetCursor() == nesw, 43859);
    check(!DefWindowProcA(h, 0x20, h, 2 | (0x200u << 16)) && GetCursor() == arrow, 43860);
    /* A parent that answers takes the decision away from the child. */
    parent_refusal = h;
    child = CreateWindowExA(0, "CursorPort", "", 0x50000000, 10, 10, 60, 40, h, 0, 0, 0);
    check(child, 43861);
    SetCursor(arrow);
    check(DefWindowProcA(child, 0x20, child, 1 | (0x200u << 16)) == 1, 43862);
    check(GetCursor() == arrow, 43863);
    parent_refusal = 0;
    check(DefWindowProcA(child, 0x20, child, 1 | (0x200u << 16)) == 0 && GetCursor() == beam, 43864);
    check(DestroyWindow(h), 43865);
    child_window = 0;
}

/* Phase 2: the built-in SCROLLBAR control. */
static void scrollbar_control(void)
{
    U info[7];
    int low = 0, high = 0, r[4];
    U owner = CreateWindowExA(0, "TrackPort", "bars", 0x10cf0000, 40, 40, 320, 240, 0, 0, 0, 0);
    check(owner, 43870);
    parent_refusal = owner;
    control_window = CreateWindowExA(0, "SCROLLBAR", "", 0x50000001, 10, 10, 17, 160, owner, 0, 0, 0);
    check(control_window, 43871);
    /* SB_CTL is bar 2 and travels as SBM_ messages to the control. */
    check(SetScrollRange(control_window, 2, 0, 200, 0), 43872);
    check(GetScrollRange(control_window, 2, &low, &high) && !low && high == 200, 43873);
    check(SetScrollPos(control_window, 2, 50, 0) == 0 && GetScrollPos(control_window, 2) == 50, 43874);
    info[0] = 28; info[1] = 0x17;
    check(GetScrollInfo(control_window, 2, info) && info[3] == 200 && info[5] == 50, 43875);
    info[0] = 28; info[1] = 7; info[2] = 0; info[3] = 99; info[4] = 10; info[5] = 40;
    check(SetScrollInfo(control_window, 2, info, 1) == 40, 43876);
    check(GetScrollPos(control_window, 2) == 40, 43877);
    /* The arrow state is the control's own and accumulates with OR. */
    check(EnableScrollBar(control_window, 2, 1), 43878);
    check(!EnableScrollBar(control_window, 2, 1), 43879);
    check(EnableScrollBar(control_window, 2, 2), 43880);
    check(!EnableScrollBar(control_window, 2, 3), 43881);
    check(EnableScrollBar(control_window, 2, 0), 43882);
    /* The dialog manager must leave the caret keys to it. */
    check(SendMessageA(control_window, 0x87, 0, 0) == 1, 43883);
    /* The caret keys scroll the parent, with the control in lParam. */
    scroll_owner_count = 0;
    SendMessageA(control_window, 0x100, 0x28, 0);            /* VK_DOWN */
    check(scroll_owner_count == 1 && scroll_owner_code == 1, 43884);
    check(scroll_owner_control == control_window, 43885);
    SendMessageA(control_window, 0x100, 0x21, 0);            /* VK_PRIOR */
    check(scroll_owner_code == 2, 43886);
    SendMessageA(control_window, 0x100, 0x24, 0);            /* VK_HOME */
    check(scroll_owner_code == 6, 43887);
    SendMessageA(control_window, 0x100, 0x23, 0);            /* VK_END */
    check(scroll_owner_code == 7, 43888);
    SendMessageA(control_window, 0x101, 0x23, 0);            /* key up closes it */
    check(scroll_owner_code == 8, 43889);
    /* WM_ENABLE(FALSE) disables both arrows. */
    SendMessageA(control_window, 0xa, 0, 0);
    check(!EnableScrollBar(control_window, 2, 3), 43890);
    SendMessageA(control_window, 0xa, 1, 0);
    check(!EnableScrollBar(control_window, 2, 0), 43891);
    /* SBS_SIZEGRIP claims the sizing corner and pins itself to one metric. */
    grip_window = CreateWindowExA(0, "SCROLLBAR", "", 0x50000010 | 4, 10, 10, 80, 80, owner, 0, 0, 0);
    check(grip_window, 43892);
    check(SendMessageA(grip_window, 0x84, 0, 0) == 17, 43893);
    check(GetWindowRect(grip_window, r), 43894);
    check(r[2] - r[0] == GetSystemMetrics(2) && r[3] - r[1] == GetSystemMetrics(3), 43895);
    parent_refusal = 0;
    check(DestroyWindow(owner), 43896);
    control_window = grip_window = 0;
}

/* Phase 3 and up: the trackers, driven by the runner's input. */
static void validate(U phase)
{
    if (phase == 3)
    {
        /* A size drag of the bottom right corner. */
        check(first(0x100f008u) >= 0, 43900);                 /* SC_SIZE|WMSZ_BOTTOMRIGHT */
        check(first(0x231) > first(0x100f008u), 43901);       /* WM_ENTERSIZEMOVE after it */
        check(first(0x24) > first(0x231), 43902);             /* WM_GETMINMAXINFO inside */
        check(first(0x2000008u) > first(0x231), 43903);       /* WM_SIZING carries WMSZ */
        check(!seen(0x216), 43904);                           /* no WM_MOVING while sizing */
        check(first(0x46) > first(0x2000008u), 43905);        /* the commit follows the drag */
        check(first(5) > first(0x46), 43906);                 /* WM_SIZE after WINDOWPOSCHANGING */
        check(first(0x232) > first(5), 43907);                /* WM_EXITSIZEMOVE last */
        check(first(0x232) > first(0x47), 43908);
    }
    else if (phase == 4)
    {
        /* A move drag of the caption. */
        check(first(0x100f012u) >= 0, 43910);                 /* SC_MOVE|HTCAPTION */
        check(first(0x231) > first(0x100f012u), 43911);
        check(first(0x216) > first(0x231), 43912);            /* WM_MOVING, not WM_SIZING */
        check(!seen(0x2000008u), 43913);
        check(first(3) > first(0x46), 43914);                 /* WM_MOVE after the commit */
        check(first(0x232) > first(3), 43915);
    }
    else if (phase == 5)
    {
        /* An arrow press that repeated, then released. */
        check(first(0x3010001u) >= 0, 43920);                 /* WM_VSCROLL SB_LINEDOWN */
        check(trace[used - 1] == 0x3010008u, 43921);          /* SB_ENDSCROLL closes it */
        {
            U i, repeats = 0;
            for (i = 0; i < used; ++i) if (trace[i] == 0x3010001u) ++repeats;
            check(repeats >= 2, 43922);                       /* the repeat timer fired */
        }
    }
    else if (phase == 6)
    {
        /* A page press. */
        check(first(0x3010003u) >= 0, 43930);                 /* SB_PAGEDOWN */
        check(trace[used - 1] == 0x3010008u, 43931);
    }
    else if (phase == 7)
    {
        /* A thumb drag: SB_THUMBTRACK while it moves, then SB_THUMBPOSITION
         * and SB_ENDSCROLL, in that order and nothing after. */
        check(first(0x3010005u) >= 0, 43940);
        check(first(0x3010004u) > first(0x3010005u), 43941);
        check(trace[used - 1] == 0x3010008u, 43942);
        check(trace[used - 2] == 0x3010004u, 43943);
    }
    used = 0;
}

void run_frame_track_test(void)
{
    U message[7];
    U cls[10] = {0, (U)track_proc, 0, 0, 0, 0, 0, 0, 0, (U)"TrackPort"};
    check(RegisterClassA(cls), 43799);
    hit_test_matrix();
    set_cursor_choice();
    scrollbar_control();
    /* The tracking window the runner drives. */
    track_window = CreateWindowExA(0, "TrackPort", "tracking", 0x10cf0000 | 0x00300000,
                                   80, 90, 400, 300, 0, 0, 0, 0);
    check(track_window, 43950);
    {
        U info[7];
        info[0] = 28; info[1] = 7; info[2] = 0; info[3] = 400; info[4] = 20; info[5] = 100;
        SetScrollInfo(track_window, 1, info, 1);
        SetScrollInfo(track_window, 0, info, 1);
    }
    check(ShowWindow(track_window, 5) == 0 || IsWindow(track_window), 43951);
    used = 0;
    /* The runner posts WM_APP with a phase number after each input sequence,
     * and WM_APP + 99 when it is done. */
    while (GetMessageA(message, 0, 0, 0))
    {
        if (message[1] == 0x8000 && message[2] == 99) break;
        if (message[1] == 0x8000) { validate(message[2]); continue; }
        TranslateMessage(message);
        DispatchMessageA(message);
    }
    check(DestroyWindow(track_window), 43952);
    ExitProcess(0);
}
#ifdef FRAME_TRACK_STANDALONE
void mainCRTStartup(void) { run_frame_track_test(); }
#endif
