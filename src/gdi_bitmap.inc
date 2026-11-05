// SPDX-License-Identifier: GPL-2.0-or-later
// Included inside Gdi's object namespace. Native storage/address providers;
// allocation, descriptor layout and copies live in win98_bitmap.h.
struct BitmapStore
{
    using U = unsigned;
    // Every DDB takes two ranges, its header and its bits. Win98 keeps the bits
    // in 32-bit memory and has no count of its own; 400 ranges allowed about
    // two hundred bitmaps for the whole system, and a second Dev-C++ ran out of
    // them in CreateCompatibleBitmap, which the thunk answers with error 87
    // (Failure in win98_bitmap_thunk.h). The counts are a port limit.
    // These internal bitmap handles are WORD offsets, 4000h + 4*slot.
    static constexpr U LocalCapacity = (0x10000-0x4000)/4;
    static constexpr U GlobalCount = 4096, Limit = 128u << 20;
    struct Range
    {
        U base = 0, size = 0, guest = 0;
        unsigned char *data = nullptr;
        BitmapIO *io = nullptr;
    };
    Range *ranges=nullptr;U range_capacity=0;
    ~BitmapStore(){delete[] ranges;}
    struct Global
    {
        U range = 0, locks = 0;
    } globals[GlobalCount];
    struct Local
    {
        U range = 0, native = 0, flags = 0;
        unsigned char bytes[0x22] = {};
    } locals[LocalCapacity];
    Win98Bitmap::State state;
    BitmapIO *io = nullptr;
    Range *read_cache = nullptr, *write_cache = nullptr;
    // Region boolean operations alternate between input and scratch ranges.
    // A single last-read pointer made each switch scan the whole mapping table.
    Range *recent[4] = {};
    U next_recent = 0;
    U allocated = 0;
    U next_base = 0x10000, free_hint = 0; // placement past the last range, first free slot
    bool failed = false;

    void Fault(const char *why)
    {
        if (!failed && io)
            io->Fault(why);
        failed = true;
    }
    U Map(unsigned char *data, U size, BitmapIO *external = nullptr, U guest = 0)
    {
        if (!size)
            return 0;
        U slot = free_hint;
        while (slot < range_capacity && ranges[slot].base)
            ++slot;
        if (slot == range_capacity) {
            if(range_capacity>~0u/2||size_t(range_capacity?range_capacity*2:128)>size_t(-1)/sizeof(Range))return 0;
            const U count=range_capacity?range_capacity*2:128;
            auto *more=new(std::nothrow) Range[count];if(!more)return 0;
            for(U i=0;i<range_capacity;++i)more[i]=ranges[i];
            // Only these private address caches retain Range pointers.
            read_cache=write_cache=nullptr;for(auto *&entry:recent)entry=nullptr;
            delete[] ranges;ranges=more;range_capacity=count;
        }
        // Behind everything placed so far is free, so the common case needs no
        // search. Only when that end reaches the top of the address space does
        // the first-fit search over all ranges run.
        unsigned long long base = next_base;
        if (base + size > 0xfffffff0ull)
        {
            base = 0x10000;
            bool moved;
            do
            {
                moved = false;
                for (U i=0;i<range_capacity;++i)
                    if (const auto &r=ranges[i]; r.base && base < (unsigned long long)r.base + r.size && base + size > r.base)
                    {
                        base = ((unsigned long long)r.base + r.size + 15) & ~15ull;
                        moved = true;
                    }
            } while (moved);
            if (base + size > 0xfffffff0ull)
                return 0;
        }
        else
            next_base = U((base + size + 15) & ~15ull);
        free_hint=slot+1;
        ranges[slot] = {U(base), size, guest, data, external};
        return slot + 1;
    }
    void Unmap(U range)
    {
        if (!range)
            return;
        auto *removed = &ranges[range - 1];
        if (read_cache == removed) read_cache = nullptr;
        if (write_cache == removed) write_cache = nullptr;
        for (auto *&entry : recent) if (entry == removed) entry = nullptr;
        ranges[range - 1] = {};
        if (range - 1 < free_hint)
            free_hint = range - 1;
    }
    Range *Address(U address, U bytes, Range *&cache)
    {
        auto contains = [&](const Range &r)
        {
            return r.base && address >= r.base && address - r.base < r.size &&
                   bytes <= r.size - (address - r.base);
        };
        if (cache && contains(*cache))
            return cache;
        for (auto *entry : recent)
            if (entry && contains(*entry)) return cache = entry;
        for (U i=0;i<range_capacity;++i)
            if (auto &r=ranges[i]; contains(r))
            {
                recent[next_recent++ & 3] = &r;
                return cache = &r;
            }
        Fault("GDI bitmap provider address outside mapped storage");
        return nullptr;
    }
    U Read(U address, U bytes)
    {
        unsigned char data[4] = {};
        auto *r = Address(address, bytes, read_cache);
        if (!r)
            return 0;
        const U offset = address - r->base;
        if (r->io)
        {
            if (!r->io->Read(r->guest + offset, data, bytes))
                Fault("GDI bitmap source access failed");
        }
        else
            __builtin_memcpy(data, r->data + offset, bytes);
        U value = 0;
        for (U i = 0; i < bytes; ++i)
            value |= U(data[i]) << (8 * i);
        return value;
    }
    void Write(U address, U value, U bytes)
    {
        auto *r = Address(address, bytes, write_cache);
        if (!r)
            return;
        unsigned char data[4];
        for (U i = 0; i < bytes; ++i)
            data[i] = value >> (8 * i);
        const U offset = address - r->base;
        if (r->io)
        {
            if (!r->io->Write(r->guest + offset, data, bytes))
                Fault("GDI bitmap destination access failed");
        }
        else
            __builtin_memcpy(r->data + offset, data, bytes);
    }
    // Win98Bitmap::Copy moves a whole block through here. Every word through
    // Read16 and Write16 cost a range search and a call each; a DDB of a
    // memory DC went that way twice per blit (MahJong 2000).
    bool CopyBlock(U destination, U source, U bytes)
    {
        if (!bytes)
            return true;
        auto *from = Address(source, bytes, read_cache);
        auto *to = from ? Address(destination, bytes, write_cache) : nullptr;
        if (!from || !to)
            return false; // the word loop reports and handles it as before
        const U from_offset = source - from->base, to_offset = destination - to->base;
        if (!from->io && !to->io)
        {
            __builtin_memmove(to->data + to_offset, from->data + from_offset, bytes);
            return true;
        }
        unsigned char buffer[4096];
        for (U done = 0; done < bytes;)
        {
            const U part = bytes - done < sizeof buffer ? bytes - done : U(sizeof buffer);
            const unsigned char *data = nullptr;
            if (from->io)
            {
                if (!from->io->Read(from->guest + from_offset + done, buffer, part))
                {
                    Fault("GDI bitmap source access failed");
                    return true;
                }
                data = buffer;
            }
            else
                data = from->data + from_offset + done;
            if (to->io)
            {
                if (!to->io->Write(to->guest + to_offset + done, data, part))
                {
                    Fault("GDI bitmap destination access failed");
                    return true;
                }
            }
            else
                __builtin_memcpy(to->data + to_offset + done, data, part);
            done += part;
        }
        return true;
    }
    U Read8(U p) { return Read(p, 1); }
    U Read16(U p) { return Read(p, 2); }
    U Read32(U p) { return Read(p, 4); }
    U ReadPointer(U p) { return Read32(p); }
    void Write8(U p, U v) { Write(p, v, 1); }
    void Write16(U p, U v) { Write(p, v, 2); }
    void Write32(U p, U v) { Write(p, v, 4); }
    void WritePointer(U p, U v) { Write32(p, v); }
    Local *Record(U handle)
    {
        if (handle < 0x4000 || (handle - 0x4000) % 4)
            return nullptr;
        const U index = (handle - 0x4000) / 4;
        return index < LocalCapacity && locals[index].native ? &locals[index] : nullptr;
    }
    U Object(U handle)
    {
        auto *r = Record(handle);
        if (!r)
        {
            Fault("Invalid original GDI bitmap handle");
            return 0;
        }
        return ranges[r->range - 1].base;
    }
    U Native(U handle)
    {
        auto *r = Record(handle);
        return r ? r->native : 0;
    }
    U LocalAllocate(U, U bytes)
    {
        if (bytes != 0x22 && bytes != 0x18)
            return 0;
        for (U i = 0; i < LocalCapacity; ++i)
            if (!locals[i].native)
            {
                const Kind kind = bytes == 0x22 ? BitmapObject : BrushObject;
                const U native = Allocate(kind);
                if (!native)
                    return 0;
                auto &r = locals[i];
                r = {};
                r.range = Map(r.bytes, sizeof r.bytes);
                if (!r.range)
                {
                    Free(native);
                    return 0;
                }
                r.native = native;
                const U handle = 0x4000 + 4 * i;
                if (kind == BitmapObject)
                    Find(native, kind)->original_bitmap = handle;
                else
                    Find(native, kind)->original_brush = handle;
                return handle;
            }
        return 0;
    }
    void MarkLocal(U handle, U mask)
    {
        if (auto *r = Record(handle))
            r->flags |= mask;
    }
    void LocalFree(U handle)
    {
        auto *r = Record(handle);
        if (!r)
            return;
        const U native = r->native;
        auto *o = Find(native, BitmapObject);
        if (!o)
            o = Find(native, BrushObject);
        if (o->allocation)
        {
            allocated -= U(o->bitmap.stride) * U(o->bitmap.height);
            ::free(o->allocation);
            o->allocation = nullptr;
            o->host_copy_current = false;
        }
        Unmap(r->range);
        *r = {};
        o->original_bitmap = 0;
        o->original_brush = 0;
        Free(native);
        if (shutdown_pending)
            Shutdown();
    }
    U GlobalAllocate(U, U bytes)
    {
        // Bounded native provider; new ARM storage is initialized deterministically.
        if (!bytes || bytes > Limit - allocated)
            return 0;
        for (U i = 0; i < GlobalCount; ++i)
            if (!globals[i].range)
            {
                auto *data = static_cast<unsigned char *>(::malloc(bytes));
                if (!data)
                    return 0;
                __builtin_memset(data, 0, bytes);
                const U r = Map(data, bytes);
                if (!r)
                {
                    ::free(data);
                    return 0;
                }
                globals[i] = {r, 0};
                allocated += bytes;
                return i + 1;
            }
        return 0;
    }
    U GlobalLock(U handle)
    {
        if (!handle || handle > GlobalCount || !globals[handle - 1].range)
            return 0;
        auto &g = globals[handle - 1];
        ++g.locks;
        return ranges[g.range - 1].base;
    }
    void GlobalUnlock(U handle)
    {
        if (handle && handle <= GlobalCount && globals[handle - 1].locks)
            --globals[handle - 1].locks;
    }
    void GlobalFree(U handle)
    {
        if (!handle || handle > GlobalCount)
            return;
        auto &g = globals[handle - 1];
        if (!g.range)
            return;
        auto &r = ranges[g.range - 1];
        allocated -= r.size;
        ::free(r.data);
        Unmap(g.range);
        g = {};
    }
    bool GlobalSmartPageLock(U handle)
    {
        return handle && handle <= GlobalCount && globals[handle - 1].range;
    }
    U GetCurrentTask() { return 1; }
    U GetExpectedVersion(U) { return io ? io->ExpectedVersion() : 0x400; }
    bool DeviceCapable(U) { return false; }
    Win98Bitmap::DeviceResult RealizeBitmap(U, const Win98Bitmap::Bitmap &, U) { return {}; }
    U DeviceHandle(U) { return 0; }
    U DeviceTable(U) { return 0; }
    U DriverBits(U, U, U, U, U)
    {
        Fault("GDI device bitmap provider unavailable");
        return 0;
    }
    void DestroyDevice(U, U, U) { Fault("GDI device bitmap destruction unavailable"); }
    void ProbeBits(U, U) {} // Flat mappings have no 64-KiB selector limit.
    void FreeSelector(U) { Fault("GDI selector-owned bitmap unavailable"); }
    void DibSectionCleanup(U) { Fault("Original GDI DIBSection cleanup unavailable"); }
    void ArithmeticFault() { Fault("Original GDI bitmap allocation divide error"); }
    U HeapFloor() { return 0x3fff; }
    void InvalidBitmap(U) {}
    U CallerModule() { return 0; } // Native ownership is the application's GDI table.
    U LockBitmap(U handle) { return Win98Bitmap::Lock(*this, handle); }
    void UnlockBitmap(U handle) { Win98Bitmap::Unlock(*this, handle); }
    U CreateBitmap(U p)
    {
        return Win98Bitmap::Create(*this, state,
                                   {Read16(p), Read16(p + 2), Read16(p + 4), Read16(p + 6), Read8(p + 8),
                                    Read8(p + 9), ReadPointer(p + 0x0a)});
    }
    U SetBitmapBits(U handle, U count, U bits)
    {
        const U result = Win98Bitmap::SetBits(*this, handle, count, bits);
        if (auto *o = Find(Native(handle), BitmapObject)) Refresh(*o);
        return result;
    }
    U DriverCopyBitmap(U, U, U)
    {
        Fault("GDI device bitmap copy unavailable");
        return 0;
    }
    bool BitmapDeviceMatchesScreen(U) { return false; }
    U CreateScreenBitmap(U, U) { return 0; }
    bool DibSectionPattern(U, U) { return false; }
    void DestroyBrush(U handle)
    {
        if (!Win98PatternBrush::UnrealizedSoftwareDestroy(*this, handle))
            Fault("Original GDI driver brush destruction unavailable");
    }
    void AttachBrush(U handle)
    {
        auto *o = Find(Native(handle), BrushObject);
        const U storage = Read16(Object(handle) + 0x14);
        const U p = GlobalLock(storage);
        if (!p)
            return;
        const U width = Read16(p + 2), height = Read16(p + 4), stride = Read16(p + 6);
        const U planes = Read8(p + 8), depth = Read8(p + 9);
        // The original constructor accepts other dimensions/formats; native
        // realization currently handles the first 8x8 pixels of these formats.
        if (planes == 1 && height >= 8 && width >= 8 && (depth == 1 || depth == 32))
        {
            if (depth == 1)
            {
                unsigned char realized[0x2e] = {};
                const U range = Map(realized, sizeof realized);
                if (range)
                {
                    Win98PatternBrush::RealizeMono8(*this, p, ranges[range - 1].base, 0, 0);
                    for (U y = 0; y < 8; ++y)
                        for (U x = 0; x < 8; ++x)
                            o->pattern[y * 8 + x] = (realized[0x0e + y * 4] >> (7 - x)) & 1;
                    Unmap(range);
                    o->monochrome_pattern = o->pattern_ready = true;
                }
            }
            else
            {
                const U bits = ReadPointer(p + 0x0a);
                for (U y = 0; y < 8; ++y)
                    for (U x = 0; x < 8; ++x)
                        o->pattern[y * 8 + x] = Read32(bits + y * stride + x * 4);
                o->pattern_ready = true;
            }
            o->brush.pattern = o->pattern;
        }
        GlobalUnlock(storage);
    }
    void References(U handle, U count) { Write16(Object(handle) + 0x10, count); }
    U Destroy(U handle) { return Record(handle) ? Win98Bitmap::Destroy(*this, state, handle) : 0; }
    bool Initialize()
    {
        if (state.empty_bitmap)
            return true;
        const Win98Bitmap::Bitmap empty{0, 1, 1, 2, 1, 1, 0};
        state.empty_bitmap = Win98Bitmap::Create(*this, state, empty);
        return state.empty_bitmap && Attach(state.empty_bitmap);
    }
    bool Attach(U handle)
    {
        auto *o = Find(Native(handle), BitmapObject);
        if (!o)
            return false;
        if (o->bitmap.bits)
            return true;
        const U p = Win98Bitmap::Lock(*this, handle);
        if (!p)
            return false;
        const U width = Read16(p + 2), height = Read16(p + 4), stride = Read16(p + 6);
        const U planes = Read8(p + 8), depth = Read8(p + 9);
        const U bytes = Read32(p + 0x0e) * planes;
        const bool renderable =
            planes == 1 &&
            (depth == 1 || depth == 4 || depth == 8 || depth == 16 || depth == 24 || depth == 32) &&
            stride >= (width * depth + 7) / 8;
        if (renderable)
        {
            const U bits = ReadPointer(p + 0x0a);
            if (Read16(p + 0x16))
            {
                if (bytes > Limit - allocated)
                {
                    Win98Bitmap::Unlock(*this, handle);
                    return false;
                }
                o->allocation = static_cast<unsigned char *>(::malloc(bytes));
                if (!o->allocation)
                {
                    Win98Bitmap::Unlock(*this, handle);
                    return false;
                }
                __builtin_memset(o->allocation, 0, bytes);
                allocated += bytes;
                o->bitmap = {o->allocation, int(width), int(height), int(stride), int(depth)};
            }
            else
            {
                auto *r = Address(bits, bytes, read_cache);
                if (!r || r->io)
                {
                    Win98Bitmap::Unlock(*this, handle);
                    return false;
                }
                o->bitmap = {r->data + bits - r->base, int(width), int(height), int(stride), int(depth)};
            }
        }
        Win98Bitmap::Unlock(*this, handle);
        Sync(*o, false);
        return !failed;
    }
    // DDB pixels live in guest memory, with segmented bitmaps split into
    // 64-KiB chunks. Win98Bitmap::Bits skips segment gaps. The rasterizer
    // uses a linear host copy, synchronized here. Retain it while only the
    // host writes; guest SetBitmapBits calls Refresh. This avoids copying an
    // entire bitmap for every small blit.
    void Sync(Gdi::Object &o, bool write)
    {
        if (!o.original_bitmap || !o.allocation)
            return;
        if (!write && o.host_copy_current)
            return;
        const U bytes = U(o.bitmap.stride) * U(o.bitmap.height);
        const U range = Map(o.allocation, bytes);
        if (!range)
        {
            Fault("GDI bitmap linear view mapping exhausted");
            return;
        }
        if (write)
            Win98Bitmap::SetBits(*this, o.original_bitmap, bytes, ranges[range - 1].base);
        else
            Win98Bitmap::GetBits(*this, o.original_bitmap, bytes, ranges[range - 1].base);
        Unmap(range);
        o.host_copy_current = !failed;
    }
    // Guest writes invalidate the host copy; reload it.
    void Refresh(Gdi::Object &o)
    {
        o.host_copy_current = false;
        Sync(o, false);
    }
    // A requested teardown that has to wait for an application object stays
    // pending, and the last object that goes carries it out. Without that the
    // stock bitmap outlived a process whose own objects were released after
    // the store teardown, for instance the halftone pattern brush USER keeps
    // for the whole run.
    bool shutdown_pending = false, shutting_down = false;
    void Shutdown()
    {
        shutdown_pending = true;
        if (shutting_down)
            return;
        for (const auto &r : locals)
            if (r.native)
            {
                auto *o = Find(r.native, BitmapObject);
                if (!o)
                    o = Find(r.native, BrushObject);
                if (o->references)
                    return;
            }
        // System stock object lifetime ends only after all application bitmaps.
        for (U i = 0; i < LocalCapacity; ++i)
            if (locals[i].native && 0x4000 + 4 * i != state.empty_bitmap)
                return;
        const U empty = state.empty_bitmap;
        state.empty_bitmap = 0;
        shutting_down = true;
        if (empty)
            Destroy(empty);
        shutting_down = false;
        state = {};
        shutdown_pending = false;
    }
} bitmap_store;

// The pixels of the screen while a program holds the primary surface locked
// (SetScreenGuard). Null
// without a lock, so a drawing call pays one load and one compare.
const unsigned char *screen_guard = nullptr;
void (*screen_guard_sink)(bool write) = nullptr;
void (*surface_access_sink)(const unsigned char *, bool) = nullptr;
inline void GuardScreen(const Object *object, bool write)
{
    if (object && surface_access_sink) surface_access_sink(object->bitmap.bits, write);
    if (screen_guard && object && object->bitmap.bits == screen_guard && screen_guard_sink)
        screen_guard_sink(write);
}

struct BitmapAccess
{
    Object *object;
    bool write;
    BitmapAccess(Object *o, bool writable) : object(o), write(writable)
    {
        GuardScreen(object, writable);
        if (object)
            bitmap_store.Sync(*object, false);
    }
    ~BitmapAccess()
    {
        if (object && write)
            bitmap_store.Sync(*object, true);
    }
};
