# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (C) 2026 Andreas Horvath
from pathlib import Path
ROOT=next(p for p in Path(__file__).resolve().parents if(p/'neo98.py').is_file());OUT=ROOT/'build/multiwait-family'
IMPORTS={'KERNEL32':dict(ExitProcess=4,GetCommandLineA=0,CreateEventA=16,SetEvent=4,ResetEvent=4,CreateSemaphoreA=16,ReleaseSemaphore=12,CreateMutexA=12,ReleaseMutex=4,CreateThread=24,Sleep=4,CloseHandle=4,WaitForSingleObject=8,WaitForMultipleObjects=16,WaitForMultipleObjectsEx=20,GetLastError=0,GetProcAddress=8,SetLastError=4,GetModuleHandleA=4,DuplicateHandle=28,GetCurrentProcess=0,CreateProcessA=40,GetExitCodeProcess=8),'USER32':dict(GetWindowTextA=12,GetQueueStatus=4,DispatchMessageA=4,GetInputState=0,SendMessageA=16,ShowWindow=8,SetFocus=4,SetWindowTextA=8,MsgWaitForMultipleObjects=20,MsgWaitForMultipleObjectsEx=20,WaitMessage=0,PeekMessageA=20,PostMessageA=16,RegisterClassA=4,CreateWindowExA=48,DefWindowProcA=16,DestroyWindow=4,SetTimer=16,KillTimer=8)}
