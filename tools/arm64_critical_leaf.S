// SPDX-License-Identifier: GPL-2.0-or-later
// Windows mutex record: recursion +4, owner +8, available +16.
// x0 and LR stay intact. Only volatile x9-x14 and NZCV are changed.
.text
.macro validate slow
    lsr x9, x0, #32
    cbnz x9, \slow
    ldrb w9, [x0]
    cmp w9, #4
    b.ne \slow
    ldr w9, [x0, #4]
    tst w9, #31
    b.ne \slow
    mov w14, #0xc8010000
    sub w14, w9, w14
    lsr w14, w14, #26
    cbnz w14, \slow
    ldrb w14, [x9]
    cmp w14, #4
    b.ne \slow
    ldr w10, [x9, #16]
    ldr w11, [x9, #4]
    ldr w12, [x9, #8]
.endm
.global critical_enter
critical_enter:
    validate enter_slow
    sub x13, x18, #0x7dc
    ldr w13, [x13]
    cmp w10, #1
    b.eq enter_take
    cmp w12, w13
    b.ne enter_slow
enter_take:
    mov w12, w13
    sub w10, w10, #1
    add w11, w11, #1
enter_commit:
    str w11, [x9, #4]
    str w12, [x9, #8]
    add x14, x9, #16
    stlr w10, [x14]
    ret
.balign 16
enter_slow:
    svc #0
    ret
.balign 16
.global critical_leave
critical_leave:
    validate leave_slow
    cbz w11, leave_slow
    sub w11, w11, #1
    add w10, w10, #1
    cbnz w11, leave_commit
    // A final leave with waiters is handed to the host before any store.
    // The provider changes the counters and transfers ownership exactly once.
    cbz w10, leave_slow
    mov w14, #0x80000000
    cmp w10, w14
    b.hi leave_slow
    mov w12, wzr
leave_commit:
    str w11, [x9, #4]
    str w12, [x9, #8]
    add x14, x9, #16
    stlr w10, [x14]
    ret
.balign 16
leave_slow:
    svc #0
    ret
.balign 16
