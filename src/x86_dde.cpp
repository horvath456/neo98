// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_dde.h"
#include "user.h"
#include "x86_app.h"
#include <stdio.h>
#include <string.h>
namespace X86
{
using namespace Win98Dde;
namespace
{
// Native handle spaces. The original hands out DDEML's own near pointers and
// 16-bit window handles; NEO98 never exposes a host address, so each family
// keeps its own tagged range, as the monitor and GDI ports do.
constexpr U kConvBase = 0x4dc00000, kDataBase = 0x4dd40000, kListBase = 0x4dd80000;
constexpr U kInstances = 16, kConversations = 32, kDataHandles = 64, kServices = 32;
constexpr U kConvLists = 8;
// CONVINFO of the 32-bit DDEML: 60h bytes, the last two dwords the two
// windows, CONVCONTEXT (24h bytes with its SECURITY_QUALITY_OF_SERVICE) at
// 34h. The caller names how much of it it wants in cb.
constexpr U kConvInfoSize = 0x60, kConvContextSize = 0x24;
// wStatus bits and wConvst values the original writes.
constexpr U kStatusConnected = 0x0001, kStatusClient = 0x0010;
constexpr U kStateConnected = 2;
// QID_SYNC (-1), the only transaction id a synchronous port can answer for.
constexpr U kQueueSync = 0xffffffffu;
char lower(char c) { return c >= 'A' && c <= 'Z' ? char(c - 'A' + 'a') : c; }
} // namespace
struct Application::DdeState final
{
    struct Instance
    {
        bool used = false;
        U cookie = 0, callback = 0, flags = 0, error = 0;
        U instance_window = 0, server_window = 0;
        // DdeEnableCallback(EC_DISABLE) for the whole instance.
        bool callbacks_disabled = false;
        U services[kServices] = {};
        U service_count = 0;
    } instances[kInstances];
    struct Conversation
    {
        bool used = false;
        U client = 0, server = 0;   // instance indices, 0xffffffff for an unknown peer
        U service = 0, topic = 0;   // atoms
        U client_window = 0, server_window = 0;
        bool client_side = true;
        U user_handle = 0;          // DdeSetUserHandle, CONVINFO +4
        U list = 0;                 // the DdeConnectList this one belongs to
    } conversations[kConversations];
    struct ConvList
    {
        bool used = false;
        U owner = 0;                // instance index of the client
        U members[kConversations] = {};
        U count = 0;
    } lists[kConvLists];
    struct Data
    {
        bool used = false;
        U owner = 0, address = 0, size = 0, format = 0, item = 0, flags = 0, access = 0;
    } data[kDataHandles];
    U cookie_counter = 0;
    bool classes = false;
};
Application::DdeState &Application::Dde()
{
    if (!dde)
    {
        dde = new DdeState;
        // The original registers its window classes from DDEML's own DllMain.
        // Both carry the class name of the original, so the window tree shows
        // the same two entries; the procedure stays native.
        User::RegisterClass(kInstanceClass, 0, 0, 0);
        // DMGFrame answers the raw DDE protocol of a client that is no DDEML
        // instance of this process, the way DDEML's frame procedure does
        // (SHELL32's ShellExecute with ddeexec).
        User::RegisterClass(kServerClass, DdeFrameProcedure, 0, 0);
        dde->classes = true;
    }
    return *dde;
}
void Application::FreeDde()
{
    if (!dde)
        return;
    for (auto &d : dde->data)
        if (d.used && d.address)
            Free(d.address);
    delete dde;
    dde = nullptr;
}
// Validate the instance pair: the
// low word selects the record, the high word must equal the record's +30h.
void *Application::DdeInstance(unsigned id)
{
    auto &state = Dde();
    const U index = (id & 0xffff) - 1, cookie = id >> 16;
    if (!(id & 0xffff) || index >= kInstances)
        return nullptr;
    auto &record = state.instances[index];
    if (!record.used || record.cookie != cookie)
        return nullptr;
    return &record;
}
unsigned Application::DdeCallback(unsigned instance_index, unsigned type, unsigned format, unsigned conv,
                                  unsigned hsz1, unsigned hsz2, unsigned data, unsigned d1, unsigned d2)
{
    auto &record = Dde().instances[instance_index];
    if (!record.callback)
        return 0;
    const unsigned arguments[8] = {type, format, conv, hsz1, hsz2, data, d1, d2};
    return Call(record.callback, arguments, 8);
}
// The original transport: DdeConnect posts WM_DDE_INITIATE to every top-level
// window and a server answers with WM_DDE_ACK. NEO98 posts the same two
// messages through the native queues so the exchange is visible on our side,
// and resolves the server from the ported name-service list. A guest window
// that is not a DDEML server never answers, which is the original's outcome
// for a window without a DDE server procedure.
unsigned Application::DdeBroadcastInitiate(unsigned client_window, unsigned service, unsigned topic)
{
    unsigned windows[64] = {}, count = 0;
    if (User::WindowSnapshot(User::GetDesktopWindow(), windows, 64, count))
        for (U i = 0; i < count; ++i)
            if (windows[i] != client_window)
                User::PostMessage(windows[i], kWmDdeInitiate, client_window, service | (topic << 16));
    return count;
}
// The server search DdeConnect makes : walk the instances, skip the
// ones that are no server or refuse connections, and take the first whose name
// service carries the requested service atom. A null atom matches every
// server. DdeConnectList makes the same walk, only it does not stop.
unsigned Application::DdeMatchServer(unsigned client_index, unsigned service, unsigned from)
{
    auto &state = Dde();
    for (U i = from; i < kInstances; ++i)
    {
        auto &peer = state.instances[i];
        if (!peer.used || !peer.server_window)
            continue;
        if (i == client_index && (peer.flags & kCbfFailSelfconnections))
            continue;
        if (peer.flags & kCbfFailConnections)
            continue;
        bool named = !service;
        for (U s = 0; s < peer.service_count && !named; ++s)
            named = peer.services[s] == service;
        if (named)
            return i;
    }
    return kInstances;
}

// The second half of DdeConnect: ask the server, take a conversation slot,
// post the acknowledgement and confirm. DdeConnectList repeats it per server.
unsigned Application::DdeOpen(unsigned client_index, unsigned server_index, unsigned service,
                              unsigned topic)
{
    auto &state = Dde();
    auto &client = state.instances[client_index];
    auto &server = state.instances[server_index];
    if (!DdeCallback(server_index, kXtypConnect, 0, 0, topic, service, 0, 0, 0))
    {
        client.error = kNoConvEstablished;
        return 0;
    }
    U slot = kConversations;
    for (U i = 0; i < kConversations; ++i)
        if (!state.conversations[i].used)
        {
            slot = i;
            break;
        }
    if (slot == kConversations)
    {
        client.error = kMemoryError;
        return 0;
    }
    auto &conversation = state.conversations[slot];
    conversation = DdeState::Conversation();
    conversation.used = true;
    conversation.client = client_index;
    conversation.server = server_index;
    conversation.service = service;
    conversation.topic = topic;
    conversation.client_window = client.instance_window;
    conversation.server_window = server.server_window;
    // The server's acceptance reaches the client as WM_DDE_ACK with the
    // server window in wParam, exactly as the original posts it.
    User::PostMessage(conversation.client_window, kWmDdeAck, conversation.server_window,
                      service | (topic << 16));
    if (!(server.flags & kCbfSkipConnectConfirms))
        DdeCallback(server_index, kXtypConnectConfirm, 0, kConvBase + slot + 1, topic, service, 0, 0, 0);
    return kConvBase + slot + 1;
}

// One conversation out of a handle, with the record checks every entry makes.
// The type stays private to this file, so the pointer travels untyped, exactly
// as DdeInstance does.
void *Application::DdeConversation(unsigned handle)
{
    auto &state = Dde();
    const U slot = handle - kConvBase - 1;
    if (handle < kConvBase + 1 || slot >= kConversations || !state.conversations[slot].used)
        return nullptr;
    return &state.conversations[slot];
}

unsigned Application::DdeApi(ApiName name, const unsigned *a)
{
    auto &state = Dde();
    // Windows 98 wide DDE entries return zero and error 120 without
    // initializing DDEML. For DdeInitializeW, zero numerically equals
    // DMLERR_NO_ERROR even though no instance was created.
    const unsigned long length = strlen(name);
    if (name[length - 1] == 'W')
    {
        last_error = 120;
        return 0;
    }
    switch (name.id)
    {
    API_CASE("DdeInitializeA")
    {
        if (!Reserved(a[3]))
            return kInvalidParameter;
        U old = 0;
        if (!a[0] || !Read(a[0], &old, 4))
            return kInvalidParameter;
        if (old)
        {
            auto *record = (DdeState::Instance *)DdeInstance(old);
            if (!record)
                return kInvalidParameter;
            record->flags = Merge(record->flags, a[2]);
            record->callback = a[1];
            return kNoError;
        }
        U index = kInstances;
        for (U i = 0; i < kInstances; ++i)
            if (!state.instances[i].used)
            {
                index = i;
                break;
            }
        if (index == kInstances)
            return kMemoryError;
        auto &record = state.instances[index];
        record = DdeState::Instance();
        record.used = true;
        record.callback = a[1];
        // the stored flags always carry APPCMD_FILTERINITS, and an
        // APPCLASS_MONITOR instance additionally gets kMonitorFlags.
        record.flags = a[2] | kAppcmdFilterinits;
        if (a[2] & kAppclassMonitor)
            record.flags |= kMonitorFlags;
        record.cookie = ++state.cookie_counter;
        // Both windows belong to the thread that initializes, as in DDEML,
        // where the calling task creates them.
        record.instance_window =
            User::CreateWindow(kInstanceClass, "", 0, 0, 0, 0, 0, 0, 0, 0, ThreadId());
        if (!(a[2] & kAppcmdClientonly))
            record.server_window =
                User::CreateWindow(kServerClass, "", 0x80000000, 0, 0, 0, 0, 0, 0, 0, ThreadId());
        const U id = (record.cookie << 16) | (index + 1);
        if (log)
        {
            char text[120];
            snprintf(text, sizeof text, "DDEML instance %08x window=%08x frame=%08x thread=%u me=%u pid=%u", id, record.instance_window, record.server_window, User::WindowThread(record.server_window), ThreadId(), User::WindowProcess(record.server_window));
            log(text);
        }
        if (!Store(a[0], &id, 4))
            return kInvalidParameter;
        return kNoError;
    }
    API_CASE("DdeUninitialize")
    {
        auto *record = (DdeState::Instance *)DdeInstance(a[0]);
        if (!record)
            return 0;
        for (auto &c : state.conversations)
            if (c.used && (&state.instances[c.client] == record || &state.instances[c.server] == record))
                c.used = false;
        if (record->instance_window)
            User::DestroyWindow(record->instance_window);
        if (record->server_window)
            User::DestroyWindow(record->server_window);
        *record = DdeState::Instance();
        return 1;
    }
    API_CASE("DdeGetLastError")
    {
        auto *record = (DdeState::Instance *)DdeInstance(a[0]);
        if (!record)
            return kDllNotInitialized;
        const U error = record->error;
        record->error = kNoError;
        return error;
    }
    API_CASE("DdeCreateStringHandleA")
    {
        auto *record = (DdeState::Instance *)DdeInstance(a[0]);
        if (!record)
            return 0;
        record->error = kNoError;
        if (!CodePage(a[2]))
        {
            record->error = kInvalidParameter;
            return 0;
        }
        if (!a[1])
            return 0;
        const char *text = ReadString(a[1]);
        if (!text || !*text)
            return 0;
        return GlobalAtomSearch(a[1], true);
    }
    API_CASE("DdeFreeStringHandle") API_CASE("DdeKeepStringHandle")
    {
        auto *record = (DdeState::Instance *)DdeInstance(a[0]);
        if (!record)
            return 0;
        record->error = kNoError;
        if (!a[1])
            return 0;
        if (name[3] == 'F')
            GlobalAtomDelete(a[1]);
        else
        {
            char text[256] = {};
            if (GlobalAtomName(a[1], text, sizeof text))
                GlobalAtomText(text);
        }
        return 1;
    }
    API_CASE("DdeCmpStringHandles")
    {
        if (a[0] == a[1])
            return 0;
        char first[256] = {}, second[256] = {};
        GlobalAtomName(a[0], first, sizeof first);
        GlobalAtomName(a[1], second, sizeof second);
        for (U i = 0;; ++i)
        {
            const char x = lower(first[i]), y = lower(second[i]);
            if (x != y)
                return x < y ? 0xffffffff : 1;
            if (!x)
                return 0;
        }
    }
    API_CASE("DdeQueryStringA")
    {
        auto *record = (DdeState::Instance *)DdeInstance(a[0]);
        if (!record)
            return 0;
        record->error = kNoError;
        if (!CodePage(a[4]))
        {
            record->error = kInvalidParameter;
            return 0;
        }
        char text[256] = {};
        const U length = GlobalAtomName(a[1], text, sizeof text);
        // a buffer with a zero size only receives the
        // terminator, and a null buffer with a size answers the length.
        if (!a[2])
            return a[3] ? length : 0;
        if (!a[3])
        {
            const char zero = 0;
            Store(a[2], &zero, 1);
            return 0;
        }
        U copied = length;
        if (copied > a[3] - 1)
            copied = a[3] - 1;
        Store(a[2], text, copied + 1);
        return copied;
    }
    API_CASE("DdeNameService")
    {
        auto *record = (DdeState::Instance *)DdeInstance(a[0]);
        if (!record)
            return 0;
        record->error = kNoError;
        // DNS_FILTERON sets and DNS_FILTEROFF clears
        // APPCMD_FILTERINITS before the register bits are looked at.
        if (a[3] & kDnsFilteron)
            record->flags |= kAppcmdFilterinits;
        if (a[3] & kDnsFilteroff)
            record->flags &= ~U(kAppcmdFilterinits);
        if (!(a[3] & (kDnsRegister | kDnsUnregister)))
            return 0;
        if (record->flags & kAppcmdClientonly)
        {
            record->error = kDllUsage;
            return 0;
        }
        if (a[3] & kDnsRegister)
        {
            if (!a[1])
            {
                record->error = kInvalidParameter;
                return 0;
            }
            if (record->service_count == kServices)
            {
                record->error = kMemoryError;
                return 0;
            }
            for (U i = 0; i < record->service_count; ++i)
                if (record->services[i] == a[1])
                    return 1;
            record->services[record->service_count++] = a[1];
        }
        else
        {
            for (U i = 0; i < record->service_count;)
                if (!a[1] || record->services[i] == a[1])
                    record->services[i] = record->services[--record->service_count];
                else
                    ++i;
        }
        // and tell every other instance about the change. Our
        // instances see it through the same callback filter bits.
        for (U i = 0; i < kInstances; ++i)
        {
            auto &peer = state.instances[i];
            if (!peer.used || &peer == record)
                continue;
            const U skip = (a[3] & kDnsRegister) ? kCbfSkipRegistrations : kCbfSkipUnregistrations;
            if (peer.flags & skip)
                continue;
            DdeCallback(i, (a[3] & kDnsRegister) ? kXtypRegister : kXtypUnregister, 0, 0, a[1], a[1], 0, 0, 0);
        }
        return 1;
    }
    API_CASE("DdeConnect")
    {
        auto *record = (DdeState::Instance *)DdeInstance(a[0]);
        if (!record)
            return 0;
        record->error = kNoError;
        U client_index = kInstances;
        for (U i = 0; i < kInstances; ++i)
            if (&state.instances[i] == record)
                client_index = i;
        const U server_index = DdeMatchServer(client_index, a[1], 0);
        DdeBroadcastInitiate(record->instance_window, a[1], a[2]);
        if (server_index >= kInstances)
        {
            record->error = kNoConvEstablished;
            return 0;
        }
        return DdeOpen(client_index, server_index, a[1], a[2]);
    }
    API_CASE("DdeDisconnect")
    {
        const U slot = a[0] - kConvBase - 1;
        if (a[0] < kConvBase + 1 || slot >= kConversations || !state.conversations[slot].used)
            return 0;
        auto &conversation = state.conversations[slot];
        User::PostMessage(conversation.server_window, kWmDdeTerminate, conversation.client_window, 0);
        if (!(state.instances[conversation.server].flags & kCbfSkipDisconnects))
            DdeCallback(conversation.server, kXtypDisconnect, 0, a[0], 0, 0, 0, 0, 0);
        conversation.used = false;
        return 1;
    }
    // DDEML.4 over the third thunk table. It is DdeConnect over
    // every server that answers the name instead of the first one, and it
    // hands back a list handle that DdeQueryNextServer walks.
    API_CASE("DdeConnectList")
    {
        auto *record = (DdeState::Instance *)DdeInstance(a[0]);
        if (!record)
            return 0;
        record->error = kNoError;
        if (!Reserved(a[4]) && a[4])
        {
            // The context is read only for its size field; a CONVCONTEXT of a
            // foreign size is DMLERR_INVALID_PARAMETER.
            U size = 0;
            if (!Read(a[4], &size, 4) || size > 20)
            {
                record->error = kInvalidParameter;
                return 0;
            }
        }
        U client_index = kInstances;
        for (U i = 0; i < kInstances; ++i)
            if (&state.instances[i] == record)
                client_index = i;
        // A list handed in is extended, not replaced; Windows 98 keeps the old
        // members and adds the servers that are not in it yet.
        U list = kConvLists;
        if (a[3])
        {
            const U slot = a[3] - kListBase - 1;
            if (a[3] < kListBase + 1 || slot >= kConvLists || !state.lists[slot].used)
            {
                record->error = kInvalidParameter;
                return 0;
            }
            list = slot;
        }
        else
            for (U i = 0; i < kConvLists; ++i)
                if (!state.lists[i].used)
                {
                    list = i;
                    state.lists[i] = DdeState::ConvList();
                    state.lists[i].used = true;
                    state.lists[i].owner = client_index;
                    break;
                }
        if (list >= kConvLists)
        {
            record->error = kMemoryError;
            return 0;
        }
        DdeBroadcastInitiate(record->instance_window, a[1], a[2]);
        auto &members = state.lists[list];
        for (U server = DdeMatchServer(client_index, a[1], 0); server < kInstances;
             server = DdeMatchServer(client_index, a[1], server + 1))
        {
            bool already = false;
            for (U i = 0; i < members.count && !already; ++i)
            {
                auto *existing = (DdeState::Conversation *)DdeConversation(members.members[i]);
                already = existing && existing->server == server;
            }
            if (already || members.count >= kConversations)
                continue;
            const U conversation = DdeOpen(client_index, server, a[1], a[2]);
            if (!conversation)
                continue;
            ((DdeState::Conversation *)DdeConversation(conversation))->list = kListBase + list + 1;
            members.members[members.count++] = conversation;
        }
        if (!members.count)
        {
            if (!a[3])
                members = DdeState::ConvList();
            record->error = kNoConvEstablished;
            return 0;
        }
        record->error = kNoError;
        return kListBase + list + 1;
    }
    // DDEML.5 over the FT table, entry 2Bh. A null previous handle
    // answers the first member, the last member answers zero.
    API_CASE("DdeQueryNextServer")
    {
        const U slot = a[0] - kListBase - 1;
        if (a[0] < kListBase + 1 || slot >= kConvLists || !state.lists[slot].used)
            return 0;
        auto &members = state.lists[slot];
        U index = 0;
        if (a[1])
        {
            while (index < members.count && members.members[index] != a[1])
                ++index;
            if (index >= members.count)
                return 0;
            ++index;
        }
        while (index < members.count && !DdeConversation(members.members[index]))
            ++index;
        return index < members.count ? members.members[index] : 0;
    }
    // DDEML.6, entry 2Ah. Every member is disconnected and the list is gone.
    API_CASE("DdeDisconnectList")
    {
        const U slot = a[0] - kListBase - 1;
        if (a[0] < kListBase + 1 || slot >= kConvLists || !state.lists[slot].used)
            return 0;
        auto &members = state.lists[slot];
        for (U i = 0; i < members.count; ++i)
            if (DdeConversation(members.members[i]))
            {
                const unsigned arguments[] = {members.members[i]};
                DdeApi("DdeDisconnect", arguments);
            }
        members = DdeState::ConvList();
        return 1;
    }
    // DDEML.9 over. The conversation record of the original carries
    // more than NEO98 keeps, so every field it has is filled and the rest is
    // left at zero; the limits are named in
    // DDEML.9 through the USER32 thunk (entry 14h). The 32-bit CONVINFO is
    // 60h bytes: thirteen dwords, the 36-byte CONVCONTEXT at 34h, hwnd at
    // 58h and hwndPartner at 5Ch; SHDOC401 asks for exactly that size and
    // reads hwndPartner. DDEML.9 clamps a larger or zero cb to its
    // full size. The transaction id -1 is QID_SYNC.
    API_CASE("DdeQueryConvInfo")
    {
        auto *conversation = (DdeState::Conversation *)DdeConversation(a[0]);
        if (!conversation || !a[2])
            return 0;
        // A conversation the frame window of a server holds for a client
        // without DDEML has only the server half; it answers as the server.
        const bool server_half = conversation->client == 0xffffffffu;
        auto &owner = state.instances[server_half ? conversation->server : conversation->client];
        owner.error = kNoError;
        // Only the synchronous transaction exists natively; every other id is
        // DMLERR_UNFOUND_QUEUE_ID, which is what the original answers for an
        // id its queue does not know.
        if (a[1] != kQueueSync)
        {
            owner.error = kUnfoundQueueId;
            return 0;
        }
        U size = 0;
        if (!Read(a[2], &size, 4) || size < 8)
        {
            owner.error = kInvalidParameter;
            return 0;
        }
        if (size > kConvInfoSize) size = kConvInfoSize;
        U info[kConvInfoSize / 4] = {};
        info[0] = size;
        info[1] = conversation->user_handle;
        // Both halves of a DDEML conversation live in one native record, so
        // the partner handle is the handle itself. A partner without DDEML
        // has no conversation handle.
        info[2] = server_half ? 0 : a[0];
        info[3] = conversation->service;
        info[4] = conversation->service;
        info[5] = conversation->topic;
        info[9] = server_half ? kStatusConnected : kStatusConnected | kStatusClient;
        info[10] = kStateConnected;
        info[11] = owner.error;
        info[12] = conversation->list;
        info[13] = kConvContextSize;         // ConvCtxt.cb
        info[22] = server_half ? conversation->server_window : conversation->client_window;
        info[23] = server_half ? conversation->client_window : conversation->server_window;
        return Store(a[2], info, size) ? size : 0;
    }
    // DDEML.10 through USER32 entry 27h: (hConv, id, hUser), three dwords as
    // the thunk passes them. The value is kept per
    // conversation and read back by DdeQueryConvInfo; the transaction id must
    // be QID_SYNC, a conversation that is gone is
    // DMLERR_INVALIDPARAMETER.
    API_CASE("DdeSetUserHandle")
    {
        auto *conversation = (DdeState::Conversation *)DdeConversation(a[0]);
        if (!conversation)
            return 0;
        const bool server_half = conversation->client == 0xffffffffu;
        auto &owner = state.instances[server_half ? conversation->server : conversation->client];
        owner.error = kNoError;
        if (a[1] != kQueueSync)
        {
            owner.error = kUnfoundQueueId;
            return 0;
        }
        conversation->user_handle = a[2];
        return 1;
    }
    // DDEML.12, entry 26h: (idInst, hConv, idTransaction). Every native
    // transaction runs inside DdeClientTransaction and is over when that call
    // returns, so no queue id is ever outstanding. Id 0 abandons all of them,
    // which here is nothing, and succeeds; QID_SYNC cannot be
    // abandoned and is DMLERR_INVALIDPARAMETER; any other id
    // is the DMLERR_UNFOUND_QUEUE_ID of a queue that does not know it.
    API_CASE("DdeAbandonTransaction")
    {
        auto *record = (DdeState::Instance *)DdeInstance(a[0]);
        if (!record)
            return 0;
        record->error = kNoError;
        if (a[1] && !DdeConversation(a[1]))
        {
            record->error = kInvalidParameter;
            return 0;
        }
        if (a[2] == kQueueSync)
        {
            record->error = kInvalidParameter;
            return 0;
        }
        if (!a[2])
            return 1;
        record->error = kUnfoundQueueId;
        return 0;
    }
    // DDEML.13, entry 25h. The original walks the advise links of the topic
    // and sends XTYP_ADVREQ for each. NEO98 has no advise links at all:
    // DdeClientTransaction refuses XTYP_ADVSTART, so none can ever be made.
    // The call therefore reports DMLERR_NOTPROCESSED instead of claiming to
    // have notified somebody.
    API_CASE("DdePostAdvise")
    {
        auto *record = (DdeState::Instance *)DdeInstance(a[0]);
        if (!record)
            return 0;
        record->error = kNoError;
        if (record->flags & kAppcmdClientonly)
        {
            record->error = kDllUsage;
            return 0;
        }
        record->error = kNotProcessed;
        return 0;
    }
    API_CASE("DdeClientTransaction")
    {
        const U slot = a[2] - kConvBase - 1;
        if (a[2] < kConvBase + 1 || slot >= kConversations || !state.conversations[slot].used)
            return 0;
        auto &conversation = state.conversations[slot];
        auto &client = state.instances[conversation.client];
        auto &server = state.instances[conversation.server];
        client.error = kNoError;
        if (a[5] != kXtypExecute)
        {
            client.error = kNotProcessed;
            return 0;
        }
        if (server.flags & kCbfFailExecutes)
        {
            client.error = kNotProcessed;
            return 0;
        }
        const U handle = DdeMakeData(conversation.client, a[0], a[1], 0, a[3], a[4], 0);
        if (!handle)
        {
            client.error = kMemoryError;
            return 0;
        }
        User::PostMessage(conversation.server_window, kWmDdeExecute, conversation.client_window,
                          state.data[handle - kDataBase - 1].address);
        const U answer =
            DdeCallback(conversation.server, kXtypExecute, a[4], a[2], conversation.topic, a[3], handle, 0, 0);
        // DDE_FACK in the high word of the posted WM_DDE_ACK lParam, which is
        // what DdeClientTransaction reports through pdwResult.
        // The documented server answer for XTYP_EXECUTE is DDE_FACK in bit 15;
        // DDE_FNOTPROCESSED is zero. Anything without that bit counts as not
        // acknowledged, which is the transaction's failure path.
        const U acknowledged = (answer & 0x8000) != 0;
        User::PostMessage(conversation.client_window, kWmDdeAck, conversation.server_window,
                          acknowledged ? 0x8000 : 0);
        if (a[7])
        {
            const U result = acknowledged ? 0x8000u : 0u;
            Store(a[7], &result, 4);
        }
        if (!acknowledged)
        {
            client.error = kNotProcessed;
            return 0;
        }
        return handle;
    }
    API_CASE("DdeCreateDataHandle")
    {
        auto *record = (DdeState::Instance *)DdeInstance(a[0]);
        if (!record)
            return 0;
        record->error = kNoError;
        U client_index = 0;
        for (U i = 0; i < kInstances; ++i)
            if (&state.instances[i] == record)
                client_index = i;
        const U handle = DdeMakeData(client_index, a[1], a[2], a[3], a[4], a[5], a[6]);
        if (!handle)
            record->error = kMemoryError;
        return handle;
    }
    // USER, then DDEML.15. USER hands a CF_METAFILEPICT,
    // CF_DSPMETAFILEPICT or CF_DIB handle over only as four bytes at offset
    // zero. DDEML refuses a handle it does not know with
    // DMLERR_INVALIDPARAMETER on the owner, clears the error otherwise, grows
    // the block when the data goes past its end, copies and answers the same
    // handle. The port keeps no DDE header before the data, so the offset is
    // not moved by four.
    API_CASE("DdeAddData")
    {
        const U slot = a[0] - kDataBase - 1;
        if (a[0] < kDataBase + 1 || slot >= kDataHandles || !state.data[slot].used)
            return 0;
        auto &data = state.data[slot];
        auto &owner = state.instances[data.owner];
        if ((data.format == 3 || data.format == 0x83 || data.format == 8) && (a[2] != 4 || a[3] != 0))
            return 0;
        owner.error = kNoError;
        const U end = a[3] + a[2];
        if (end < a[3])
        {
            owner.error = kInvalidParameter;
            return 0;
        }
        if (end > data.size)
        {
            const U grown = Allocate(end ? end : 1);
            if (!grown)
            {
                owner.error = kMemoryError;
                return 0;
            }
            unsigned char buffer[1024];
            for (U done = 0; done < data.size;)
            {
                const U step = data.size - done < sizeof buffer ? data.size - done : U(sizeof buffer);
                if (!Read(data.address + done, buffer, step) || !Store(grown + done, buffer, step))
                    break;
                done += step;
            }
            Free(data.address);
            data.address = grown;
            data.size = end;
        }
        if (a[1] && a[2])
        {
            unsigned char buffer[1024];
            for (U done = 0; done < a[2];)
            {
                const U step = a[2] - done < sizeof buffer ? a[2] - done : U(sizeof buffer);
                if (!Read(a[1] + done, buffer, step) || !Store(data.address + a[3] + done, buffer, step))
                {
                    owner.error = kInvalidParameter;
                    return 0;
                }
                done += step;
            }
        }
        return a[0];
    }
    // DDEML.26. This port delivers every callback at once and never
    // queues one, so EC_QUERYWAITING finds nothing; EC_DISABLE and the two
    // enabling forms are kept as the instance's flag.
    API_CASE("DdeEnableCallback")
    {
        auto *record = (DdeState::Instance *)DdeInstance(a[0]);
        if (!record)
            return 0;
        record->error = kNoError;
        if ((a[1] && !DdeConversation(a[1])) || (a[2] & 0xff75))
        {
            record->error = kInvalidParameter;
            return 0;
        }
        if (a[2] == 2)
            return 0;
        if (a[2] == 8)
        {
            if (!a[1]) record->callbacks_disabled = true;
            return 1;
        }
        if (!a[1]) record->callbacks_disabled = false;
        return 1;
    }
    // DDEML.37 : only a client conversation can reconnect; it is
    // connected again to its service and topic, keeps its list, and the new
    // handle comes back. No advise link exists in this port to restart.
    API_CASE("DdeReconnect")
    {
        auto *conversation = (DdeState::Conversation *)DdeConversation(a[0]);
        if (!conversation)
        {
            for (auto &instance : state.instances)
                if (instance.used)
                    instance.error = kNoConvEstablished;
            return 0;
        }
        auto &client = state.instances[conversation->client];
        client.error = kNoError;
        if (!conversation->client_side)
        {
            client.error = kInvalidParameter;
            return 0;
        }
        const U client_index = conversation->client, list = conversation->list;
        const U server_index = DdeMatchServer(client_index, conversation->service, 0);
        if (server_index >= kInstances)
        {
            client.error = kNoConvEstablished;
            return 0;
        }
        const U fresh = DdeOpen(client_index, server_index, conversation->service, conversation->topic);
        if (!fresh)
            return 0;
        if (auto *renewed = (DdeState::Conversation *)DdeConversation(fresh))
            renewed->list = list;
        return fresh;
    }
    API_CASE("DdeGetData") API_CASE("DdeAccessData") API_CASE("DdeUnaccessData") API_CASE("DdeFreeDataHandle")
    {
        const U slot = a[0] - kDataBase - 1;
        if (a[0] < kDataBase + 1 || slot >= kDataHandles || !state.data[slot].used)
            return 0;
        auto &data = state.data[slot];
        if (API_IS(name,"DdeAccessData"))
        {
            if (a[1])
                Store(a[1], &data.size, 4);
            ++data.access;
            return data.address;
        }
        if (API_IS(name,"DdeUnaccessData"))
        {
            if (data.access)
                --data.access;
            return 1;
        }
        if (API_IS(name,"DdeFreeDataHandle"))
        {
            if (data.address)
                Free(data.address);
            data = DdeState::Data();
            return 1;
        }
        if (!a[1])
            return data.size;
        const U offset = a[3] >= data.size ? data.size : a[3];
        U bytes = data.size - offset;
        if (bytes > a[2])
            bytes = a[2];
        unsigned char buffer[1024];
        U done = 0;
        while (done < bytes)
        {
            U step = bytes - done;
            if (step > sizeof buffer)
                step = sizeof buffer;
            if (!Read(data.address + offset + done, buffer, step) || !Store(a[1] + done, buffer, step))
                return 0;
            done += step;
        }
        return bytes;
    }
    default:
        break;
    }
    return 0;
}
unsigned Application::DdeMakeData(unsigned owner, unsigned source, unsigned size, unsigned offset,
                                  unsigned item, unsigned format, unsigned flags)
{
    auto &state = Dde();
    U slot = kDataHandles;
    for (U i = 0; i < kDataHandles; ++i)
        if (!state.data[i].used)
        {
            slot = i;
            break;
        }
    if (slot == kDataHandles)
        return 0;
    auto &data = state.data[slot];
    data = DdeState::Data();
    const U bytes = size ? size : 1;
    data.address = Allocate(bytes);
    if (!data.address)
        return 0;
    data.used = true;
    data.owner = owner;
    data.size = size;
    data.item = item;
    data.format = format;
    data.flags = flags;
    if (source && size)
    {
        unsigned char buffer[1024];
        U done = 0;
        while (done < size)
        {
            U step = size - done;
            if (step > sizeof buffer)
                step = sizeof buffer;
            if (!Read(source + offset + done, buffer, step) || !Store(data.address + done, buffer, step))
            {
                Free(data.address);
                data = DdeState::Data();
                return 0;
            }
            done += step;
        }
    }
    return kDataBase + slot + 1;
}
// The frame procedure of a server instance for a client in another process
// or without DDEML: SHELL32 broadcasts WM_DDE_INITIATE with SendMessageTimeout
//  and waits for WM_DDE_ACK sent back to its window, then posts
// WM_DDE_EXECUTE with a GMEM_DDESHARE block and expects WM_DDE_ACK with the
// packed pair (status, hCommands), and finally WM_DDE_TERMINATE. The command
// block lies in the client's memory; USER hands DDE memory between processes,
// so the server gets its own copy.
unsigned Application::DdeFrameMessage(unsigned hwnd, unsigned message, unsigned wp, unsigned lp)
{
    auto &state = Dde();
    U index = kInstances;
    for (U i = 0; i < kInstances; ++i)
        if (state.instances[i].used && state.instances[i].server_window == hwnd) index = i;
    if (index == kInstances) return DefaultWindowMessage(hwnd, message, wp, lp);
    auto &server = state.instances[index];
    auto find = [&](U client) -> U {
        for (U i = 0; i < kConversations; ++i)
        {
            auto &c = state.conversations[i];
            if (c.used && c.server == index && c.client == 0xffffffffu && c.client_window == client) return i;
        }
        return kConversations;
    };
    if (log && message >= kWmDdeInitiate && message <= kWmDdeExecute)
    {
        char text[120];
        snprintf(text, sizeof text, "DDEML frame %08x message=%x wp=%08x lp=%08x", hwnd, message, wp, lp);
        log(text);
    }
    if (message == kWmDdeInitiate)
    {
        const U service = lp & 0xffff, topic = lp >> 16;
        if (wp == hwnd || (server.flags & kCbfFailConnections)) return 0;
        // A DDEML instance of this process connects through DdeConnect.
        for (const auto &peer : state.instances)
            if (peer.used && peer.instance_window == wp) return 0;
        bool named = !service;
        for (U s = 0; s < server.service_count && !named; ++s) named = server.services[s] == service;
        if (!named && (server.flags & kAppcmdFilterinits)) return 0;
        const U accepted = DdeCallback(index, kXtypConnect, 0, 0, topic, service, 0, 0, 0);
        if (log)
        {
            char text[120];
            snprintf(text, sizeof text, "DDEML frame connect service=%x topic=%x named=%d answer=%x", service, topic, named, accepted);
            log(text);
        }
        if (!accepted) return 0;
        U slot = kConversations;
        for (U i = 0; i < kConversations && slot == kConversations; ++i)
            if (!state.conversations[i].used) slot = i;
        if (slot == kConversations) return 0;
        auto &c = state.conversations[slot];
        c = DdeState::Conversation();
        c.used = true;
        c.client = 0xffffffffu;
        c.server = index;
        c.service = service;
        c.topic = topic;
        c.client_window = wp;
        c.server_window = hwnd;
        c.client_side = false;
        // The answer is sent, with new references to both atoms, as the
        // server procedure of DDEML sends it during the broadcast.
        char text[256];
        if (service && GlobalAtomName(service, text, sizeof text)) GlobalAtomText(text);
        if (topic && GlobalAtomName(topic, text, sizeof text)) GlobalAtomText(text);
        const unsigned ack[] = {wp, kWmDdeAck, hwnd, service | (topic << 16)};
        Api("SendMessageA", ack);
        if (!(server.flags & kCbfSkipConnectConfirms))
            DdeCallback(index, kXtypConnectConfirm, 0, kConvBase + slot + 1, topic, service, 0, 0, 0);
        return 0;
    }
    if (message == kWmDdeExecute)
    {
        const U slot = find(wp);
        if (slot == kConversations)
        {
            if (log) log("DDEML frame execute without conversation");
            return 0;   // the native DdeClientTransaction path
        }
        auto *client = static_cast<Application *>(User::ProcessContextOf(User::WindowProcess(wp)));
        U status = 0;
        if (slot != kConversations && client && !(server.flags & kCbfFailExecutes))
        {
            // The command text out of the client's block.
            char command[4096];
            U length = 0;
            const unsigned lock[] = {lp};
            const U at = client->Api("GlobalLock", lock);
            if (at)
            {
                while (length + 1 < sizeof command && client->Read(at + length, command + length, 1) && command[length])
                    ++length;
                client->Api("GlobalUnlock", lock);
            }
            command[length] = 0;
            const U text = Allocate(length + 1);
            if (text && Store(text, command, length + 1))
            {
                auto &c = state.conversations[slot];
                const U data = DdeMakeData(index, text, length + 1, 0, 0, 0, 0);
                const U answer = data ? DdeCallback(index, kXtypExecute, 0, kConvBase + slot + 1, c.topic, 0, data, 0, 0) : 0;
                status = answer & 0xc0ff;   // DDE_FACK, DDE_FBUSY and the application bits
                if (log)
                {
                    char line[320];
                    snprintf(line, sizeof line, "DDEML frame execute command=%.200s answer=%x", command, answer);
                    log(line);
                }
                if (data)
                {
                    const unsigned release[] = {data};
                    DdeApi(ApiName("DdeFreeDataHandle"), release);
                }
            }
            if (text) Free(text);
        }
        const unsigned pack[] = {kWmDdeAck, status, lp};
        const U packed = Api("PackDDElParam", pack);
        User::PostMessage(wp, kWmDdeAck, hwnd, packed);
        return 0;
    }
    if (message == kWmDdeTerminate)
    {
        const U slot = find(wp);
        if (slot != kConversations)
        {
            if (!(server.flags & kCbfSkipDisconnects))
                DdeCallback(index, kXtypDisconnect, 0, kConvBase + slot + 1, 0, 0, 0, 0, 0);
            state.conversations[slot].used = false;
            User::PostMessage(wp, kWmDdeTerminate, hwnd, 0);
        }
        return 0;
    }
    return DefaultWindowMessage(hwnd, message, wp, lp);
}
} // namespace X86
