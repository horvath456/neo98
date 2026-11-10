// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Andreas Horvath
#pragma once
// A resource whose length depends on the language of the installation. The
// bytes behind `size` are zero, so a text in it ends with a zero.
template<unsigned Capacity> struct Win98LocalizedBytes
{
    unsigned char bytes[Capacity] = {};
    unsigned size = 0;
    operator const unsigned char *() const { return bytes; }
};
// The bytes and the length of a resource, whether it was read from the
// installation at startup or generated into the build as an array.
template<unsigned N> const unsigned char *ResourceBytes(const unsigned char (&bytes)[N]) { return bytes; }
template<unsigned N> unsigned ResourceSize(const unsigned char (&)[N]) { return N; }
template<unsigned N> const unsigned char *ResourceBytes(const Win98LocalizedBytes<N> &resource) { return resource.bytes; }
template<unsigned N> unsigned ResourceSize(const Win98LocalizedBytes<N> &resource) { return resource.size; }
