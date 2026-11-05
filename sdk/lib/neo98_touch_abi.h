/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NEO98_TOUCH_ABI_H
#define NEO98_TOUCH_ABI_H
#define NEO98_TOUCH_VERSION 1u
#define NEO98_TOUCH_MAX_CONTACTS 32u
#define NEO98_TOUCH_QUEUE_SIZE 64u
#define NEO98_TOUCH_DOWN 1u
#define NEO98_TOUCH_MOVE 2u
#define NEO98_TOUCH_UP 3u
#define NEO98_TOUCH_CANCEL 4u
#define NEO98_TOUCH_CANCEL_ALL 5u
#define NEO98_TOUCH_PRESSURE_VALID 1u
#define NEO98_TOUCH_CAP_MULTITOUCH 1u
/* A direct touchscreen is currently recognized by the host. */
#define NEO98_TOUCH_CAP_TOUCHSCREEN_PRESENT 2u
/* Fixed 32-bit fields, identical in x86 and ARM64. Positions are signed
 * client pixels; pressure is 0..1024 when PRESSURE_VALID is set. */
typedef struct NEO98_TOUCH_EVENT {
    unsigned id, phase;
    int x, y;
    unsigned time, pressure, flags, sequence;
} NEO98_TOUCH_EVENT;
typedef struct NEO98_TOUCH_CAPS {
    unsigned size, version, max_contacts, flags;
} NEO98_TOUCH_CAPS;
#ifdef __cplusplus
static_assert(sizeof(NEO98_TOUCH_EVENT) == 32, "Touch event ABI");
static_assert(sizeof(NEO98_TOUCH_CAPS) == 16, "Touch capabilities ABI");
#endif
#endif
