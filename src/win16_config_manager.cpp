// SPDX-License-Identifier: GPL-2.0-or-later
// The protected-mode API of CONFIGMG for 16-bit code.
// CONFIGMG is a VxD, native here by the rule of PLAN.md section 0. A 16-bit
// module finds its entry with `int 2Fh` AX=1684h BX=33h (SYSDM,
// SETUPX ) and calls it far with the function number in AX. The
// callers are the small C wrappers the DDK library links into each module
// (SYSDM : `enter 8,0`, AX from [bp-8], `call far [bp-6]`); their
// arguments sit where the wrapper's own arguments are, at SS:BP+4, in the C
// order, and the answer (CONFIGRET) comes back in AX. The function numbers
// are the ones CFGMGR32.DLL passes for the same functions (CM_Get_Parent 3,
// CM_Get_DevNode_Status 36h, CM_Get_Hardware_Profile_InfoA 52h,...).
// The devnodes are the device instances of HKLM\Enum in the order of
// Registry::Devnodes (src/config_manager.cpp), below the root devnode
// HTREE\ROOT\0. Handles start at C29A1000h in steps of 80h and remain stable
// when other nodes are inserted or deleted. HKEY_DYN_DATA\Config Manager\Enum
// publishes each live handle. A device of another
// enumerator whose instance names its parent ("ROOT&*PNP0600&000100", the
// usual form of ESDI, SCSI and FLOP) hangs below that devnode; everything
// else hangs below the root. Every devnode is started and without problem:
// the machine Win98 was installed on is the machine this port presents.
// The resources. A devnode with resources has two logical
// configurations, the allocated one and the one of boot time, both with the
// values of Neo98Hardware::DeviceResources (src/neo98_hardware.h); it has no
// basic configuration, so the page Resources of the Device Manager shows the
// values and offers no change, as for a device whose settings are fixed. The
// arbitrators answer with what these devnodes hold: the lists of allocated
// memory and I/O ranges (SYSDM Windows 98 tests a range against it and takes
// "not available" as "in use"; deletes every range a devnode names
// and lists the rest as in use by an unknown device) and the masks of the
// free DMA channels and interrupt requests (invert them).
#include "neo98_hardware.h"
#include "host_devices.h"
#include "win16_machine.h"
#include "x86_app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
namespace X86
{
namespace
{
constexpr unsigned CrSuccess = 0, CrInvalidPointer = 3, CrInvalidDevnode = 5, CrNoSuchDevnode = 13,
                   CrFailure = 19, CrBufferSmall = 26, CrRegistryError = 29, CrNoMoreHwProfiles = 35,
                   CrNoSuchValue = 37;
constexpr unsigned CrInvalidResDes = 6, CrInvalidLogConf = 7, CrNoMoreLogConf = 14, CrNoMoreResDes = 15,
                   CrInvalidRangeList = 17;
constexpr unsigned RootHandle = 0xc29a0f80u, FirstHandle = 0xc29a1000u, HandleStep = 0x80;
// ResType_Mem, ResType_IO, ResType_DMA and ResType_IRQ of configmg.h.
constexpr unsigned TypeMemory = 1, TypePorts = 2, TypeDma = 3, TypeIrq = 4;
// ALLOC_LOG_CONF and BOOT_LOG_CONF, the two a devnode has here.
constexpr unsigned AllocatedConfiguration = 2, BootConfiguration = 3;
// A logical configuration is C2A00000h + devnode * 100h + kind * 10h, its
// k-th resource descriptor that plus k + 1.
constexpr unsigned ConfigurationBase = 0xc2a00000u, ConfigurationLimit = ConfigurationBase + 0x100u * 65536;
constexpr unsigned ResourceCapacity = sizeof Neo98Hardware::Resources().list / sizeof(Neo98Hardware::Resource);
unsigned TypeOf(char kind)
{
    return kind == 'M' ? TypeMemory : kind == 'P' ? TypePorts : kind == 'D' ? TypeDma : kind == 'Q' ? TypeIrq : 0;
}
// The range lists of CM_Create_Range_List: sorted, disjoint ranges.
struct RangeList
{
    bool used = false;
    unsigned count = 0;
    unsigned first[64] = {}, last[64] = {};
    void Delete(unsigned from, unsigned to)
    {
        unsigned kept = 0, a[65], b[65];
        for (unsigned i = 0; i < count; ++i)
        {
            if (last[i] < from || first[i] > to)
            {
                a[kept] = first[i], b[kept++] = last[i];
                continue;
            }
            if (first[i] < from) a[kept] = first[i], b[kept++] = from - 1;
            if (last[i] > to) a[kept] = to + 1, b[kept++] = last[i];
        }
        if (kept > 64) kept = 64;
        count = kept;
        memcpy(first, a, kept * sizeof *a);
        memcpy(last, b, kept * sizeof *b);
    }
    bool Add(unsigned from, unsigned to)
    {
        if (from > to) return false;
        // Ranges that touch become one.
        for (unsigned i = 0; i < count; ++i)
            if (!(last[i] < from && from - last[i] > 1) && !(first[i] > to && first[i] - to > 1))
            {
                if (first[i] < from) from = first[i];
                if (last[i] > to) to = last[i];
            }
        Delete(from, to);
        if (count == 64) return false;
        unsigned at = count++;
        for (; at && first[at - 1] > from; --at)
            first[at] = first[at - 1], last[at] = last[at - 1];
        first[at] = from, last[at] = to;
        return true;
    }
    bool Overlaps(unsigned from, unsigned to) const
    {
        for (unsigned i = 0; i < count; ++i)
            if (!(last[i] < from || first[i] > to)) return true;
        return false;
    }
};
constexpr unsigned RangeListBase = 0xc2b00000u, RangeLists = 16;
RangeList range_lists[RangeLists];
RangeList *ListOf(unsigned handle)
{
    const unsigned slot = (handle - RangeListBase) >> 8;
    return handle >= RangeListBase && slot < RangeLists && range_lists[slot].used ? &range_lists[slot] : nullptr;
}
unsigned NewList()
{
    for (unsigned i = 0; i < RangeLists; ++i)
        if (!range_lists[i].used)
        {
            range_lists[i] = RangeList();
            range_lists[i].used = true;
            return RangeListBase + (i << 8);
        }
    // A caller that never frees its lists (SYSDM empties them
    // instead) gets the oldest again.
    static unsigned next = 0;
    const unsigned slot = next++ % RangeLists;
    range_lists[slot] = RangeList();
    range_lists[slot].used = true;
    return RangeListBase + (slot << 8);
}
bool SameText(const char *a, const char *b)
{
    for (;; ++a, ++b)
    {
        unsigned x = (unsigned char)*a, y = (unsigned char)*b;
        if (x >= 'a' && x <= 'z') x -= 32;
        if (y >= 'a' && y <= 'z') y -= 32;
        if (x != y) return false;
        if (!x) return true;
    }
}
} // namespace

struct Application::ConfigTree
{
    unsigned count = 0;
    Registry::U keys[Registry::DevnodeCapacity] = {};
    unsigned parent[Registry::DevnodeCapacity] = {};
    unsigned handles[Registry::DevnodeCapacity] = {};
    // Index of the devnode for a handle; ~0u for the root, count for none.
    unsigned Index(unsigned handle) const
    {
        if (handle == RootHandle)
            return ~0u;
        for (unsigned i = 0; i < count; ++i) if (handles[i] == handle) return i;
        return count;
    }
    unsigned Handle(unsigned index) const { return index == ~0u ? RootHandle : handles[index]; }
};

// The devnode tree, built again for every call: the registry is the truth.
bool Application::ConfigTreeBuild(ConfigTree &tree)
{
    auto &store = registry.store;
    Registry::U enumeration = 0;
    bool created = false;
    if (store.Open(store.roots[2], "Enum", false, enumeration, created))
        return false;
    tree.count = Registry::Devnodes(store, enumeration, tree.keys, Registry::DevnodeCapacity);
    for (unsigned i = 0; i < tree.count; ++i) tree.handles[i] = Registry::DevnodeHandle(store, tree.keys[i]);
    for (unsigned i = 0; i < tree.count; ++i)
    {
        tree.parent[i] = ~0u;
        if (const unsigned parent = store.nodes[tree.keys[i]].devnode_parent) {
            const unsigned index = tree.Index(parent);
            if (index != tree.count) tree.parent[i] = index;
            continue;
        }
        char name[64];
        snprintf(name, sizeof name, "%s", store.nodes[tree.keys[i]].name);
        const auto device = store.nodes[tree.keys[i]].parent, enumerator = store.nodes[device].parent;
        if (SameText(store.nodes[enumerator].name, "ROOT"))
            continue;
        // "ROOT&*PNP0600&000100": enumerator, device, instance 0001 and a slot.
        char *first = strchr(name, '&'), *second = first ? strchr(first + 1, '&') : nullptr;
        if (!first || !second || strlen(second + 1) < 4)
            continue;
        *first = *second = 0;
        second[5] = 0;
        char path[200];
        snprintf(path, sizeof path, "%s\\%s\\%s", name, first + 1, second + 1);
        for (unsigned k = 0; k < tree.count; ++k)
        {
            const auto instance = tree.keys[k], owner = store.nodes[instance].parent;
            char candidate[200];
            snprintf(candidate, sizeof candidate, "%s\\%s\\%s", store.nodes[store.nodes[owner].parent].name,
                     store.nodes[owner].name, store.nodes[instance].name);
            if (k != i && SameText(candidate, path))
            {
                tree.parent[i] = k;
                break;
            }
        }
    }
    return true;
}

// "ENUMERATOR\DEVICE\INSTANCE", upper case as CONFIGMG keeps it.
void Application::ConfigDeviceId(const ConfigTree &tree, unsigned index, char (&out)[200])
{
    if (index == ~0u)
    {
        snprintf(out, sizeof out, "HTREE\\ROOT\\0");
        return;
    }
    auto &store = registry.store;
    const auto instance = tree.keys[index], device = store.nodes[instance].parent;
    snprintf(out, sizeof out, "%s\\%s\\%s", store.nodes[store.nodes[device].parent].name, store.nodes[device].name,
             store.nodes[instance].name);
    for (char *p = out; *p; ++p)
        if (*p >= 'a' && *p <= 'z')
            *p = char(*p - 32);
}

// The resources of a devnode, null for one without.
static const Neo98Hardware::Resources *ResourcesOf(const char *id)
{
    for (const auto &entry : Neo98Hardware::DeviceResources)
        if (SameText(entry.device, id))
            return &entry;
    return nullptr;
}

// The far entry `int 2Fh` 1684h/33h hands out.
bool Application::Win16ConfigManagerCall(unsigned &result, unsigned &words,unsigned function32,unsigned arguments32)
{
    words = 0; // the C convention: the caller removes its arguments
    const bool native32=function32!=~0u;
    const unsigned function=native32?function32:Register(Reg::Eax)&0xffff;
    const unsigned stack=native32?arguments32:Win16Linear(Register(Reg::Ss),(Register(Reg::Ebp)&0xffff)+4);
    auto arg = [&](unsigned i) { return Read32(stack + i * 4); };
    auto flat = [&](unsigned far) { return native32?far:far >> 16 ? Win16Linear(far >> 16, far & 0xffff) : 0u; };
    if(function==0x0b && getenv("NEO98_TRACE_CONFIGMG"))
        fprintf(stderr,"CONFIGMG Create_DevNode output=%08x id=%s parent=%08x flags=%08x\n",
                arg(0),ReadString(flat(arg(1))),arg(2),arg(3));
    ConfigTree tree;
    result = CrFailure;
    if (!ConfigTreeBuild(tree))
        return true;
    auto store_handle = [&](unsigned far, unsigned index) {
        const unsigned p = flat(far);
        if (!p) return CrInvalidPointer;
        Write32(p, tree.Handle(index));
        return CrSuccess;
    };
    auto set_ulong = [&](unsigned far, unsigned value) {
        const unsigned p = flat(far);
        if (p) Write32(p, value);
        return p != 0;
    };
    auto resources = [&](unsigned index) -> const Neo98Hardware::Resources * {
        if (index >= tree.count) return nullptr;
        char name[200];
        ConfigDeviceId(tree, index, name);
        return ResourcesOf(name);
    };
    // The descriptor a handle names: the entry of the table, null for none.
    auto descriptor = [&](unsigned handle) -> const Neo98Hardware::Resource * {
        if (handle < ConfigurationBase || handle >= ConfigurationLimit || !(handle & 15)) return nullptr;
        const auto *list = resources(tree.Index(FirstHandle + ((handle - ConfigurationBase) >> 8) * HandleStep));
        const unsigned k = (handle & 15) - 1;
        return list && k < ResourceCapacity && list->list[k].kind ? &list->list[k] : nullptr;
    };
    switch (function)
    {
    case 0x71: // CM_Get_Device_Interface_List, CONFIGMG ANSI MULTI_SZ
    case 0x72: // CM_Get_Device_Interface_List_Size, bytes including the final NUL
    {
        const unsigned guid=flat(arg(function==0x72?1:0));unsigned char bytes[16]={};
        bool present=HostDevices::HasBattery()&&Read(guid,bytes,sizeof bytes)&&!memcmp(bytes,HostDevices::BatteryGuidBytes,16);
        const unsigned filter=flat(arg(function==0x72?2:1));
        if(present&&filter&&*ReadString(filter)&&!SameText(ReadString(filter),HostDevices::BatteryId))present=false;
        char list[128]={};if(present)snprintf(list,sizeof list,"\\DosDevices\\%s",HostDevices::BatteryInterface);
        const unsigned needed=present?unsigned(strlen(list))+2:1;
        if(function==0x72){result=set_ulong(arg(0),needed)?CrSuccess:CrInvalidPointer;break;}
        const unsigned out=flat(arg(2));
        if(arg(3)<needed){result=CrBufferSmall;break;}
        result=out&&Store(out,list,needed)?CrSuccess:CrInvalidPointer;break;
    }
    case 0x13: // CM_Query_Arbitrator_Free_Data(pData, DataLen, dnDevInst, ResourceID, flags)
    {
        const unsigned data = flat(arg(0)), length = arg(1), type = arg(3);
        if (!data)
        {
            result = CrInvalidPointer;
            break;
        }
        unsigned used = 0;
        RangeList *list = nullptr;
        unsigned handle = 0;
        if (type == TypeMemory || type == TypePorts)
        {
            handle = NewList();
            list = ListOf(handle);
        }
        else if (type != TypeDma && type != TypeIrq)
        {
            result = 11; // CR_INVALID_RESOURCEID
            break;
        }
        for (unsigned i = 0; i < tree.count; ++i)
            if (const auto *entry = resources(i))
                for (const auto &one : entry->list)
                    if (TypeOf(one.kind) == type)
                    {
                        if (list) list->Add(one.first, one.last);
                        else used |= 1u << one.first;
                    }
        unsigned char answer[24] = {};
        const unsigned value = list ? handle : ~used & 0xffff;
        for (unsigned i = 0; i < 4; ++i)
            answer[i] = (unsigned char)(value >> (8 * i));
        const unsigned size = list ? 4 : type == TypeDma ? 2 : 24;
        if (length < size)
        {
            result = CrBufferSmall;
            break;
        }
        Store(data, answer, size);
        result = CrSuccess;
        break;
    }
    case 0x1a: // CM_Get_First_Log_Conf(plcLogConf, dnDevInst, flags)
    {
        const unsigned index = tree.Index(arg(1)), kind = arg(2) & 7;
        if (index == tree.count)
        {
            result = CrInvalidDevnode;
            break;
        }
        if (index == ~0u || !resources(index) || (kind != AllocatedConfiguration && kind != BootConfiguration))
        {
            result = CrNoMoreLogConf;
            break;
        }
        set_ulong(arg(0), ConfigurationBase + ((tree.Handle(index) - FirstHandle) / HandleStep << 8) + (kind << 4));
        result = CrSuccess;
        break;
    }
    case 0x1b: // CM_Get_Next_Log_Conf(plcLogConf, lcLogConf, flags): one of each kind
        result = CrNoMoreLogConf;
        break;
    case 0x1f: // CM_Get_Next_Res_Des(prdResDes, rdResDes, ForResource, pResourceID, flags)
    {
        const unsigned from = arg(1), wanted = arg(2);
        if (from < ConfigurationBase || from >= ConfigurationLimit)
        {
            result = CrInvalidResDes;
            break;
        }
        const auto *list = resources(tree.Index(FirstHandle + ((from - ConfigurationBase) >> 8) * HandleStep));
        result = CrNoMoreResDes;
        for (unsigned k = from & 15; list && k < ResourceCapacity && list->list[k].kind; ++k)
            if (!wanted || TypeOf(list->list[k].kind) == wanted)
            {
                set_ulong(arg(0), (from & ~15u) + k + 1);
                set_ulong(arg(3), TypeOf(list->list[k].kind));
                result = CrSuccess;
                break;
            }
        break;
    }
    case 0x21: // CM_Get_Res_Des_Data_Size(pulSize, rdResDes, flags)
    case 0x22: // CM_Get_Res_Des_Data(rdResDes, Buffer, BufferLen, flags)
    {
        const auto *one = descriptor(function == 0x21 ? arg(1) : arg(0));
        if (!one)
        {
            result = CrInvalidResDes;
            break;
        }
        // Mem_Des_s and one Mem_Range_s, IO_Des_s and one IO_Range_s,
        // DMA_Des_s, IRQ_Des_s.
        unsigned char data[36] = {};
        unsigned size = 0;
        auto put16 = [&](unsigned at, unsigned v) { data[at] = (unsigned char)v, data[at + 1] = (unsigned char)(v >> 8); };
        auto put32 = [&](unsigned at, unsigned v) { put16(at, v), put16(at + 2, v >> 16); };
        switch (TypeOf(one->kind))
        {
        case TypeMemory:
            put16(0, 1), put16(2, 20), put32(4, one->first), put32(8, one->last);
            put32(16, ~0u), put32(20, one->last - one->first + 1), put32(24, one->first), put32(28, one->last);
            size = 36;
            break;
        case TypePorts:
            put16(0, 1), put16(2, 12), put16(4, one->first), put16(6, one->last);
            put16(12, 0xffff), put16(14, one->last - one->first + 1), put16(16, one->first), put16(18, one->last);
            size = 24;
            break;
        case TypeDma:
            data[0] = one->first > 3 ? 1 : 0; // fDD_WORD for the 16-bit channels
            data[1] = (unsigned char)one->first;
            data[2] = (unsigned char)(1u << one->first);
            size = 4;
            break;
        default:
            put16(2, one->first), put16(4, 1u << one->first);
            size = 8;
            break;
        }
        if (function == 0x21)
        {
            result = set_ulong(arg(0), size) ? CrSuccess : CrInvalidPointer;
            break;
        }
        const unsigned buffer = flat(arg(1));
        if (!buffer)
        {
            result = CrInvalidPointer;
            break;
        }
        if (arg(2) < size)
        {
            result = CrBufferSmall;
            break;
        }
        Store(buffer, data, size);
        result = CrSuccess;
        break;
    }
    case 0x24: // CM_Create_Range_List(prlh, flags)
        result = set_ulong(arg(0), NewList()) ? CrSuccess : CrInvalidPointer;
        break;
    case 0x25: // CM_Add_Range(ulStartValue, ulEndValue, rlh, flags)
    case 0x26: // CM_Delete_Range(ulStartValue, ulEndValue, rlh, flags)
    case 0x27: // CM_Test_Range_Available(ulStartValue, ulEndValue, rlh, flags)
    {
        RangeList *list = ListOf(arg(2));
        if (!list)
            result = CrInvalidRangeList;
        else if (arg(0) > arg(1))
            result = 18; // CR_INVALID_RANGE
        else if (function == 0x25)
            result = list->Add(arg(0), arg(1)) ? CrSuccess : 2; // CR_OUT_OF_MEMORY
        else if (function == 0x26)
            list->Delete(arg(0), arg(1)), result = CrSuccess;
        else
            result = list->Overlaps(arg(0), arg(1)) ? CrFailure : CrSuccess;
        break;
    }
    case 0x28: // CM_Dup_Range_List(rlhOld, rlhNew, flags)
    {
        RangeList *from = ListOf(arg(0)), *to = ListOf(arg(1));
        if (from && to) *to = *from;
        result = from && to ? CrSuccess : CrInvalidRangeList;
        break;
    }
    case 0x29: // CM_Free_Range_List(rlh, flags)
        if (RangeList *list = ListOf(arg(0)))
            list->used = false, result = CrSuccess;
        else
            result = CrInvalidRangeList;
        break;
    case 0x2a: // CM_Invert_Range_List(rlhOld, rlhNew, ulMaxValue, flags)
    {
        RangeList *from = ListOf(arg(0)), *to = ListOf(arg(1));
        if (!from || !to)
        {
            result = CrInvalidRangeList;
            break;
        }
        RangeList inverted;
        inverted.used = true;
        inverted.Add(0, arg(2));
        for (unsigned i = 0; i < from->count; ++i)
            inverted.Delete(from->first[i], from->last[i]);
        *to = inverted;
        result = CrSuccess;
        break;
    }
    case 0x2b: // CM_Intersect_Range_List(rlhOld1, rlhOld2, rlhNew, flags)
    {
        RangeList *a = ListOf(arg(0)), *b = ListOf(arg(1)), *to = ListOf(arg(2));
        if (!a || !b || !to)
        {
            result = CrInvalidRangeList;
            break;
        }
        RangeList both;
        both.used = true;
        for (unsigned i = 0; i < a->count; ++i)
            for (unsigned k = 0; k < b->count; ++k)
            {
                const unsigned from = a->first[i] > b->first[k] ? a->first[i] : b->first[k];
                const unsigned end = a->last[i] < b->last[k] ? a->last[i] : b->last[k];
                if (from <= end) both.Add(from, end);
            }
        *to = both;
        result = CrSuccess;
        break;
    }
    case 0x2c: // CM_First_Range(rlh, pulStart, pulEnd, preElement, flags)
    case 0x2d: // CM_Next_Range(preElement, pulStart, pulEnd, flags)
    {
        // The element is the handle of the list plus the number of the range.
        const bool first = function == 0x2c;
        const unsigned element = flat(first ? arg(3) : arg(0));
        const unsigned at = first ? arg(0) : element ? Read32(element) + 1 : 0;
        RangeList *list = ListOf(at & ~255u);
        if (!list || !element)
        {
            result = list ? CrInvalidPointer : CrInvalidRangeList;
            break;
        }
        if ((at & 255) >= list->count)
        {
            result = CrFailure;
            break;
        }
        set_ulong(arg(1), list->first[at & 255]);
        set_ulong(arg(2), list->last[at & 255]);
        Write32(element, at);
        result = CrSuccess;
        break;
    }
    case 0x3b: // CM_Get_Alloc_Log_Conf(pccBuffer, dnDevInst, flags)
    {
        // CMCONFIG: nine memory windows, twenty port ranges, seven interrupt
        // requests and seven DMA channels with their counts.
        const unsigned index = tree.Index(arg(1)), buffer = flat(arg(0));
        if (index == tree.count)
        {
            result = CrInvalidDevnode;
            break;
        }
        if (!buffer)
        {
            result = CrInvalidPointer;
            break;
        }
        unsigned char config[216] = {};
        auto put16 = [&](unsigned at, unsigned v) { config[at] = (unsigned char)v, config[at + 1] = (unsigned char)(v >> 8); };
        auto put32 = [&](unsigned at, unsigned v) { put16(at, v), put16(at + 2, v >> 16); };
        unsigned memory = 0, ports = 0, irqs = 0, dmas = 0;
        if (const auto *entry = resources(index))
            for (const auto &one : entry->list)
                switch (TypeOf(one.kind))
                {
                case TypeMemory:
                    put32(2 + memory * 4, one.first), put32(38 + memory * 4, one.last - one.first + 1);
                    ++memory;
                    break;
                case TypePorts:
                    put16(94 + ports * 2, one.first), put16(134 + ports * 2, one.last - one.first + 1);
                    ++ports;
                    break;
                case TypeIrq:
                    config[176 + irqs++] = (unsigned char)one.first;
                    break;
                case TypeDma:
                    config[192 + dmas++] = (unsigned char)one.first;
                    break;
                default:
                    break;
                }
        put16(0, memory), put16(92, ports), put16(174, irqs), put16(190, dmas);
        Store(buffer, config, sizeof config);
        result = CrSuccess;
        break;
    }
    case 0x6a: // CM_Get_Set_Log_Conf_Priority(pulPriority, lcLogConf, flags): the get form, flags 0
    {
        const unsigned handle = arg(1);
        if (handle < ConfigurationBase || handle >= ConfigurationLimit || !resources(tree.Index(FirstHandle + ((handle - ConfigurationBase) >> 8) * HandleStep)))
        {
            result = CrInvalidLogConf;
            break;
        }
        // LCPRI_BOOTCONFIG for the boot configuration, LCPRI_NORMAL otherwise.
        if (!arg(2))
            set_ulong(arg(0), ((handle >> 4) & 7) == BootConfiguration ? 1 : 0x3000);
        result = CrSuccess;
        break;
    }
    case 0x70: // CM_Detect_Resource_Conflict(dn, ResourceID, pData, DataLen, pbConflictDetected, flags)
    {
        // No two devnodes share a value and none can be changed: a range a
        // page asks about is the devnode's own (SYSDM Windows 98 reads a byte).
        const unsigned detected = flat(arg(4));
        if (tree.Index(arg(0)) == tree.count)
        {
            result = CrInvalidDevnode;
            break;
        }
        if (detected)
        {
            const unsigned char none = 0;
            Store(detected, &none, 1);
        }
        result = detected ? CrSuccess : CrInvalidPointer;
        break;
    }
    case 0x00: // CM_Get_Version: Win98 SE CONFIGMG returns 4.11
        result = 0x040b;
        break;
    case 0x02: // CM_Locate_DevNode(pdn, pDeviceID, flags)
    {
        const unsigned id = flat(arg(1));
        const char *wanted = id ? ReadString(id) : nullptr;
        if (!wanted || !*wanted || SameText(wanted, "HTREE\\ROOT\\0"))
        {
            result = store_handle(arg(0), ~0u);
            break;
        }
        result = CrNoSuchDevnode;
        for (unsigned i = 0; i < tree.count; ++i)
        {
            char name[200];
            ConfigDeviceId(tree, i, name);
            if (SameText(name, wanted))
            {
                result = store_handle(arg(0), i);
                break;
            }
        }
        break;
    }
    case 0x0b: // CM_Create_DevNode(pdn, pszDeviceID, dnParent, flags)
    {
        const unsigned output=flat(arg(0)), input=flat(arg(1));
        if(!output || !input || !virtual_memory.Access(output,4,true)) { result=CrInvalidPointer; break; }
        // Win98 rejects the NT GENERATE_ID bit (Wine's second create).
        if(arg(3)&~3u) { result=4; break; } // CR_INVALID_FLAG
        const unsigned parent=tree.Index(arg(2));
        if(parent==tree.count) { result=CrInvalidDevnode; break; }
        // The phantom path creates registry/device identity without loading
        // a driver. Installing and starting a new physical device is separate.
        if(arg(3)!=2) { result=CrFailure; break; }
        char prefix[200], suffix[200], path[405];
        const char *id=ReadString(input);
        if(!*id || strlen(id)>=sizeof prefix) { result=30; break; } // CR_INVALID_DEVICE_ID
        strcpy(prefix,id);
        ConfigDeviceId(tree,parent,suffix);
        for(char *p=suffix;*p;++p) if(*p=='\\')*p='&';
        if(strlen(prefix)+strlen(suffix)>=200) { result=30; break; }
        snprintf(path,sizeof path,"Enum\\%s%s",prefix,suffix);
        auto &store=registry.store;
        Registry::U key=0; bool created=false;
        if(!store.Open(store.roots[2],path,false,key,created)) {
            Write32(output,Registry::DevnodeHandle(store,key));
            result=16; break; // CR_ALREADY_SUCH_DEVINST also returns the existing node
        }
        if(tree.count==Registry::DevnodeCapacity || store.next_devnode==65536) { result=2; break; }
        if(store.Open(store.roots[2],path,true,key,created)) { result=CrRegistryError; break; }
        store.nodes[key].devnode_parent=arg(2);
        const unsigned handle=Registry::DevnodeHandle(store,key);
        Registry::PublishDevnodes(store);
        Write32(output,handle);
        result=CrSuccess;
        break;
    }
    case 0x03: // CM_Get_Parent(pdn, dn, flags)
    case 0x04: // CM_Get_Child
    case 0x05: // CM_Get_Sibling
    {
        const unsigned index = tree.Index(arg(1));
        if (index == tree.count)
        {
            result = CrInvalidDevnode;
            break;
        }
        unsigned found = tree.count;
        if (function == 0x03)
            found = index == ~0u ? tree.count : tree.parent[index];
        else if (function == 0x04)
        {
            for (unsigned i = 0; i < tree.count && found == tree.count; ++i)
                if (tree.parent[i] == index)
                    found = i;
        }
        else if (index != ~0u)
        {
            for (unsigned i = index + 1; i < tree.count && found == tree.count; ++i)
                if (tree.parent[i] == tree.parent[index])
                    found = i;
        }
        result = found == tree.count ? CrNoSuchDevnode : store_handle(arg(0), found);
        break;
    }
    case 0x06: // CM_Get_Device_ID_Size(pulLen, dn, flags)
    case 0x07: // CM_Get_Device_ID(dn, buffer, length, flags)
    {
        const unsigned index = tree.Index(function == 0x06 ? arg(1) : arg(0));
        if (index == tree.count)
        {
            result = CrInvalidDevnode;
            break;
        }
        char name[200];
        ConfigDeviceId(tree, index, name);
        const unsigned length = unsigned(strlen(name));
        if (function == 0x06)
        {
            result = set_ulong(arg(0), length) ? CrSuccess : CrInvalidPointer;
            break;
        }
        const unsigned buffer = flat(arg(1)), capacity = arg(2);
        if (!buffer)
        {
            result = CrInvalidPointer;
            break;
        }
        const unsigned copy = length < capacity ? length + 1 : capacity;
        Store(buffer, name, copy);
        result = length < capacity ? CrSuccess : CrBufferSmall;
        break;
    }
    case 0x08: // CM_Get_Depth(pulDepth, dn, flags)
    {
        unsigned index = tree.Index(arg(1)), depth = 0;
        if (index == tree.count)
        {
            result = CrInvalidDevnode;
            break;
        }
        while (index != ~0u && depth < 16)
        {
            index = tree.parent[index];
            ++depth;
        }
        result = set_ulong(arg(0), depth) ? CrSuccess : CrInvalidPointer;
        break;
    }
    case 0x0c: // CM_Query_Remove_SubTree(dnAncestor, flags)
    case 0x0d: // CM_Remove_SubTree(dnAncestor, flags)
        // Nothing holds a devnode: the class installer goes
        // on and deletes the keys, and with them the devnode is gone, the
        // tree being read from the registry. It is there again at the next
        // start (src/registry_persist.cpp).
        result = tree.Index(arg(0)) == tree.count ? CrInvalidDevnode : CrSuccess;
        break;
    case 0x16: // CM_Lock(flags): no other thread changes this tree
    case 0x17: // CM_Unlock(flags)
        result = CrSuccess;
        break;
    case 0x33: // CM_Reenumerate_DevNode(dn, flags)
    case 0x34: // CM_Setup_DevNode(dn, flags)
        result = tree.Index(arg(0)) == tree.count ? CrInvalidDevnode : CrSuccess;
        break;
    case 0x36: // CM_Get_DevNode_Status(pulStatus, pulProblem, dn, flags)
    {
        const unsigned index = tree.Index(arg(2));
        if (index == tree.count)
        {
            result = CrInvalidDevnode;
            break;
        }
        // Started and without problem, or disabled in this hardware profile
        // (CM_PROB_DISABLED): "Disable in this hardware profile" of the page
        // General marks the device, and the tree shows it crossed out. The
        // port itself goes on using its device (README.md).
        bool disabled = false;
        if (index != ~0u)
        {
            char name[200];
            ConfigDeviceId(tree, index, name);
            disabled = Registry::DevnodeDisabled(registry.store, name);
        }
        set_ulong(arg(0), Registry::DevnodeStatus(disabled, index != ~0u && tree.parent[index] == ~0u) &
                              (index == ~0u ? ~0x2000u : ~0u));
        set_ulong(arg(1), disabled ? Registry::DevnodeProblemDisabled : 0);
        result = CrSuccess;
        break;
    }
    case 0x3c: // CM_Get_DevNode_Key_Size(pulLen, dn, pszSubKey, flags)
    case 0x3d: // CM_Get_DevNode_Key(dn, pszSubKey, buffer, length, flags)
    {
        const bool size = function == 0x3c;
        const unsigned index = tree.Index(size ? arg(1) : arg(0));
        if (index == tree.count || index == ~0u)
        {
            result = CrInvalidDevnode;
            break;
        }
        const unsigned flags = size ? arg(3) : arg(4), sub = flat(size ? arg(2) : arg(1));
        // CM_REGISTRY_SOFTWARE (1): the driver key the value "Driver" names
        // under Services\Class; otherwise the hardware key under Enum.
        char path[400];
        if (flags & 1)
        {
            const Registry::Value *driver = nullptr;
            if (registry.store.Query(tree.keys[index], "Driver", driver) || !driver || !driver->size)
            {
                result = CrNoSuchValue;
                break;
            }
            char name[200] = {};
            memcpy(name, driver->data, driver->size < sizeof name - 1 ? driver->size : sizeof name - 1);
            snprintf(path, sizeof path, "System\\CurrentControlSet\\Services\\Class\\%s", name);
        }
        else
        {
            char name[200];
            ConfigDeviceId(tree, index, name);
            snprintf(path, sizeof path, "Enum\\%s", name);
        }
        const char *tail = sub ? ReadString(sub) : nullptr;
        if (tail && *tail)
        {
            const size_t used = strlen(path);
            snprintf(path + used, sizeof path - used, "\\%s", tail);
        }
        const unsigned length = unsigned(strlen(path));
        if (size)
        {
            result = set_ulong(arg(0), length + 1) ? CrSuccess : CrInvalidPointer;
            break;
        }
        const unsigned buffer = flat(arg(2)), capacity = arg(3);
        if (!buffer)
        {
            result = CrInvalidPointer;
            break;
        }
        if (length + 1 > capacity)
        {
            result = CrBufferSmall;
            break;
        }
        Store(buffer, path, length + 1);
        result = CrSuccess;
        break;
    }
    case 0x3e: // CM_Read_Registry_Value(dn, pszSubKey, pszValueName, ulExpectedType,
               // buffer, pulLength, flags)
    {
        const unsigned index = tree.Index(arg(0));
        if (index == tree.count || index == ~0u)
        {
            result = CrInvalidDevnode;
            break;
        }
        auto &store = registry.store;
        Registry::U key = tree.keys[index];
        bool created = false;
        if (arg(6) & 1)
        {
            // CM_REGISTRY_SOFTWARE: the driver key under Services\Class.
            const Registry::Value *driver = nullptr;
            char path[260] = "System\\CurrentControlSet\\Services\\Class\\";
            if (store.Query(key, "Driver", driver) || !driver || !driver->size || driver->size > 200)
            {
                result = CrRegistryError;
                break;
            }
            const size_t used = strlen(path);
            memcpy(path + used, driver->data, driver->size);
            path[used + driver->size] = 0;
            if (store.Open(store.roots[2], path, false, key, created))
            {
                result = CrRegistryError;
                break;
            }
        }
        const unsigned sub = flat(arg(1)), name = flat(arg(2));
        const char *tail = sub ? ReadString(sub) : nullptr;
        if (tail && *tail && store.Open(key, tail, false, key, created))
        {
            result = CrRegistryError;
            break;
        }
        const Registry::Value *value = nullptr;
        const char *value_name = name ? ReadString(name) : "";
        if (store.Query(key, value_name ? value_name : "", value) || !value)
        {
            result = CrNoSuchValue;
            break;
        }
        if (arg(3) && value->type != arg(3))
        {
            result = 38; // CR_WRONG_TYPE
            break;
        }
        const unsigned length = flat(arg(5)), buffer = flat(arg(4));
        if (!length)
        {
            result = CrInvalidPointer;
            break;
        }
        const unsigned capacity = Read32(length);
        Write32(length, value->size);
        if (!buffer)
        {
            result = CrSuccess;
            break;
        }
        if (value->size > capacity)
        {
            result = CrBufferSmall;
            break;
        }
        Store(buffer, value->data, value->size);
        result = CrSuccess;
        break;
    }
    case 0x40: // CM_Disable_DevNode(dn, flags)
    case 0x41: // CM_Enable_DevNode(dn, flags): "Enable Device" of the page General
    {
        // Nothing holds a devnode and none needs a restart to stop or start:
        // its state is the flag of the current hardware profile.
        const unsigned index = tree.Index(arg(0));
        if (index >= tree.count)
        {
            result = CrInvalidDevnode;
            break;
        }
        char name[200];
        ConfigDeviceId(tree, index, name);
        Registry::U value = 0;
        Registry::ProfileFlags(registry.store, 0, name, value);
        value = function == 0x40 ? value | 1 : value & ~1u;
        result = Registry::SetProfileFlags(registry.store, 0, name, value) ? CrSuccess : CrRegistryError;
        break;
    }
    case 0x4a: // CM_Get_HW_Prof_Flags(szDevNodeName, ulConfig, pulValue, flags)
    case 0x4b: // CM_Set_HW_Prof_Flags(szDevNodeName, ulConfig, ulValue, flags)
    {
        // CSConfigFlags of the device in a hardware profile, 0 the current
        // one: HKLM\Config\<profile>\Enum\<device> (Registry::ProfileFlags).
        const unsigned id = flat(arg(0));
        const char *device = id ? ReadString(id) : nullptr;
        if (!device || !*device || strlen(device) >= 200)
        {
            result = CrInvalidPointer;
            break;
        }
        char name[200];
        snprintf(name, sizeof name, "%s", device);
        if (function == 0x4a)
        {
            Registry::U value = 0;
            Registry::ProfileFlags(registry.store, arg(1), name, value);
            result = set_ulong(arg(2), value) ? CrSuccess : CrInvalidPointer;
        }
        else
            result = Registry::SetProfileFlags(registry.store, arg(1), name, arg(2)) ? CrSuccess : CrRegistryError;
        break;
    }
    case 0x52: // CM_Get_Hardware_Profile_Info(ulIndex, pHWProfileInfo, flags)
    {
        // The profiles are the FriendlyNameNNNN of IDConfigDB, as the page
        // Hardware Profiles writes them (Copy, Rename, Delete); Setup makes
        // 0001. 0FFFFFFFFh names the current one.
        unsigned index = arg(0);
        const unsigned info = flat(arg(1));
        if (!info)
        {
            result = CrInvalidPointer;
            break;
        }
        char name[80] = "Original Configuration";
        unsigned number = 0;
        Registry::U key = 0;
        bool created = false;
        if (!registry.store.Open(registry.store.roots[2], "System\\CurrentControlSet\\control\\IDConfigDB", false, key,
                                 created))
        {
            const Registry::Value *value = nullptr;
            if (index == ~0u)
            {
                if (!registry.store.Query(key, "CurrentConfig", value) && value && value->size >= 4)
                    number = unsigned(atoi((const char *)value->data));
                if (!number) number = 1;
            }
            else
                for (unsigned candidate = 1; candidate < 100 && !number; ++candidate)
                {
                    char label[32];
                    snprintf(label, sizeof label, "FriendlyName%04u", candidate);
                    if (!registry.store.Query(key, label, value) && value && !index--)
                        number = candidate;
                }
            char label[32];
            snprintf(label, sizeof label, "FriendlyName%04u", number);
            if (number && !registry.store.Query(key, label, value) && value && value->size &&
                value->size < sizeof name)
            {
                memcpy(name, value->data, value->size);
                name[value->size] = 0;
            }
        }
        else if (index == 0 || index == ~0u)
            number = 1;
        if (!number)
        {
            result = CrNoMoreHwProfiles;
            break;
        }
        unsigned char record[88] = {};
        record[0] = (unsigned char)number; // HWPI_ulHWProfile
        memcpy(record + 4, name, strlen(name));
        Store(info, record, sizeof record); // dwFlags 0: the profile is docked-state independent
        result = CrSuccess;
        break;
    }
    default:
        if (log)
        {
            char text[120];
            snprintf(text, sizeof text, "CONFIGMG PM API function %02xh is not ported yet; answering CR_FAILURE",
                     function);
            log(text);
        }
        result = CrFailure;
        break;
    }
    // NEO98_CM_TRACE=1: every call with its first six argument words.
    static const bool trace = getenv("NEO98_CM_TRACE") != nullptr;
    if (log && trace)
    {
        char text[200];
        snprintf(text, sizeof text, "CM %02x(%x,%x,%x,%x,%x,%x) = %u", function, arg(0), arg(1), arg(2), arg(3), arg(4),
                 arg(5), result);
        log(text);
    }
    return true;
}
unsigned Application::ConfigManagerControl(const unsigned *a)
{
    if(!(a[1]&0x80000000u)||a[3]<4||a[5]<4||!virtual_memory.Access(a[2],4,false)||!virtual_memory.Access(a[4],4,true))return 87;
    const unsigned arguments=Read32(a[2]);
    if(!virtual_memory.Access(arguments,24,false)||(a[6]&&!virtual_memory.Access(a[6],4,true)))return 87;
    unsigned result=19,words=0;
    Win16ConfigManagerCall(result,words,a[1]&0xffff,arguments);
    Write32(a[4],result);if(a[6])Write32(a[6],4);return 0;
}
} // namespace X86
