# SPDX-License-Identifier: GPL-2.0-or-later
IMPORTS={'KERNEL32':{'ExitProcess':4,'RaiseException':16,'SetUnhandledExceptionFilter':4,'UnhandledExceptionFilter':4,'SetErrorMode':4,'GetCommandLineA':0,'RtlUnwind':16}}
EXTRA_SOURCES=['kernel_exceptions_probe.S']
