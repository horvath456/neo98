// SPDX-License-Identifier: GPL-2.0-or-later
// The records of build/generated/arm64_api_table.inc, which tools/arm64_sdk.py
// writes from sdk/api: how each described function, callback, struct and
// message crosses between an ARM64 program (Win64 ABI) and the 32-bit
// providers. docs/ARM64-APPS.md.
#pragma once
namespace Arm64Api
{
// How one value crosses. The numbers are the ones of KIND in the generator.
enum Kind : unsigned char
{
    Void = 0,
    Int32 = 1,    // any integer of up to 32 bits
    Pointer = 2,  // to the system the low half, from it zero-extended
    Handle = 3,   // sign-extended from the system, the rule of Windows for handles
    SignedPtr = 4,   // LONG_PTR, LPARAM, LRESULT
    UnsignedPtr = 5, // UINT_PTR, WPARAM, SIZE_T
    Int64 = 6,    // two slots; an 8-byte struct by value is the same
    Float32 = 7,
    Float64 = 8,  // two slots
    FunctionPointer = 9, // a callback pointer; auxiliary is the callback kind
    StructPointer = 10, // to a struct whose layouts differ; auxiliary is the struct
    Varargs = 11, // everything after the named parameters, one slot each
    Size = 12,    // SIZE_T: like UnsignedPtr, but a value 32 bits cannot hold goes to the system as FFFFFFFFh
};
// Keep: the system holds on to the struct after the call; the processor
// gives it a lasting 32-bit form (a shadow) and carries what the system
// writes there back to the program. Drop: the call that ends that when it
// returns zero. For a callback parameter or a message, Keep says the value
// is the address of such a shadow.
enum Direction : unsigned char { In = 1, Out = 2, InOut = 3, Keep = 4, Drop = 8 };
// One step of a struct conversion.
enum FieldKind : unsigned char
{
    Copy = 0,       // `value` bytes, the same on both sides
    ZeroExtend = 1, // 4 bytes in Win32, 8 in Win64
    SignExtend = 2,
    FunctionField = 3, // a callback pointer; `value` is the callback kind
    SizeField = 4,  // the size of the struct itself (cbSize)
    DeepField = 5,  // a pointer to a struct that crosses with this one; `value` is that struct
};
struct Field
{
    unsigned char kind;
    unsigned short offset32, offset64;
    unsigned short value;
};
struct Struct
{
    const char *name;
    unsigned short size32, size64, first_field, field_count;
};
struct Param
{
    unsigned char kind;
    unsigned short auxiliary;
    unsigned char direction;
    // For a struct pointer: the parameter that holds the element count of an
    // array. For an integer: the struct pointer whose size it states. For a
    // callback pointer with direction 1: the parameter whose value selects
    // the kind (the hook type). Or FFh. An integer with direction Keep is a
    // pointer the program hands back as it received it in an open callback.
    unsigned char count_parameter;
    // A callback pointer or a shadow is one only when this parameter, masked,
    // has this value (the callback of waveOutOpen is a procedure, a window,
    // a thread or an event). FFh: always.
    unsigned char when_parameter;
    unsigned when_mask, when_value;
};
struct Callback
{
    const char *name;
    unsigned char result;
    unsigned short first_param, param_count;
    // 1: the parameters are (hwnd, message, wParam, lParam) and the message
    // table applies. 2: (code, wParam, lParam) and the code table applies.
    unsigned char message;
};
struct Message
{
    unsigned short message;
    unsigned char lparam; // 1: the struct pointer is lParam, 0: wParam
    unsigned char direction;
    unsigned short structure;
};
// lParam of a hook procedure of kind `callback` when its code is `value`.
struct Code
{
    unsigned short callback;
    int value;
    unsigned char direction;
    unsigned short structure;
};
// The kind of procedure SetWindowsHookEx takes for a hook type.
struct HookKind
{
    int type;
    unsigned short callback;
};
// What GetObject fills for a GDI object of this type. Several rows of one
// type are ordered from the largest struct to the smallest.
struct Object
{
    unsigned short type, structure;
};
struct Function
{
    const char *dll, *name, *provider;
    unsigned char result;
    unsigned short first_param, param_count;
    // Bit 0: bits 8.. name the parameter that holds the message, followed by
    // wParam and lParam. Bit 1: Get/SetWindowLongPtr. Bit 2: Get/SetClassLongPtr.
    unsigned flags;
};
// Bit 3: the result is the number of bytes the system wrote into the
// converted struct of the call; the program sees the size of its own layout.
// Bit 4: (handle, bytes, buffer) of GetObject: the struct follows from the
// type of the object.
// Bit 5: the processor of the process answers the function itself, from the
// registers of the program; no provider and no 32-bit frame (the exports of
// Win64 for exceptions and unwinding, src/arm64_seh.inc).
enum : unsigned
{
    MessageFunction = 1,
    WindowLongFunction = 2,
    ClassLongFunction = 4,
    SizeResult = 8,
    ObjectFunction = 16,
    CoreFunction = 32,
};
} // namespace Arm64Api
