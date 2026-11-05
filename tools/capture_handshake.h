/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Handshake for the screenshots of tools/test_common_app.cpp.

   The guest stops at the window it has finished drawing and waits here. The
   host composes the frame, takes its picture and answers with the private
   message NEO98_CAPTURED. A pause on the wall clock is no synchronisation:
   under load the picture was taken before the window was drawn, or the window
   was gone again before the host looked.

   Everything the guest posted to itself before the wait is still delivered, so
   the window keeps behaving as it would without the handshake. MESSAGE is the
   message structure of the including fixture; it declares GetMessageA,
   TranslateMessage and DispatchMessageA over that structure itself. */
#ifndef NEO98_CAPTURE_HANDSHAKE_H
#define NEO98_CAPTURE_HANDSHAKE_H
#define NEO98_CAPTURED 0x8f00u
#define NEO98_WAIT_FOR_CAPTURE(MESSAGE)                                                            \
    do                                                                                             \
    {                                                                                              \
        MESSAGE neo98_capture_message;                                                             \
        while (GetMessageA(&neo98_capture_message, 0, 0, 0))                                       \
        {                                                                                          \
            if (neo98_capture_message.message == NEO98_CAPTURED)                                   \
                break;                                                                             \
            TranslateMessage(&neo98_capture_message);                                              \
            DispatchMessageA(&neo98_capture_message);                                              \
        }                                                                                          \
    } while (0)
#endif
