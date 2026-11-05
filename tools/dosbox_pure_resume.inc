// SPDX-License-Identifier: GPL-2.0-or-later
// Included by callback.cpp. The provider pauses the DOS thread at a frame
// boundary before touching this registry, as it does for the core archive.
static std::vector<Neo98ResumeFrame*> neo98_resume_frames;
extern bool PAGING_NEO98_InFault();

static bool Neo98PrivateReturn(Bit16u segment, Bit32u offset)
{
    if (segment != CB_SEG) return false;
    const Bit32u interrupts = CB_SOFFSET + CB_MAX * CB_SIZE;
    if (offset >= interrupts && offset < interrupts + 256 * 6)
        return (offset - interrupts) / 6 != 0x21; // the outer shell EXEC is safe
    return (offset >= CB_SOFFSET + call_idle * CB_SIZE && offset < CB_SOFFSET + (call_idle + 1) * CB_SIZE) ||
           (offset >= CB_SOFFSET + call_stop * CB_SIZE && offset < CB_SOFFSET + (call_stop + 1) * CB_SIZE);
}

// Old heads have no native locals. A flat fresh start does not prove that
// the saved machine was flat too: it may already be in a later CON read.
extern "C" bool DBP_Neo98ResumeLegacySafe()
{
    if (cpu.pmode) return true;
    if (Neo98PrivateReturn(SegValue(cs), reg_eip)) return false;
    if (cpu.stack.big) return true;
    const PhysPt first = PhysMake(SegValue(ss), reg_sp);
    const PhysPt second = PhysMake(SegValue(ss), Bit16u(reg_sp + 2));
    const Bitu memory = MEM_TotalPages() * 4096;
    if (first + 2 > memory || second + 2 > memory) return false;
    const Bit16u ip = phys_readb(first) | (Bit16u(phys_readb(first + 1)) << 8);
    const Bit16u segment = phys_readb(second) | (Bit16u(phys_readb(second + 1)) << 8);
    return !Neo98PrivateReturn(segment, ip);
}
Neo98ResumeFrame::Neo98ResumeFrame(unsigned tag_, std::function<void(DBPArchive&, bool)> fields_)
    : tag(tag_), fields(fields_)
{
    neo98_resume_frames.push_back(this);
}
Neo98ResumeFrame::~Neo98ResumeFrame()
{
    assert(!neo98_resume_frames.empty() && neo98_resume_frames.back() == this);
    neo98_resume_frames.pop_back();
}
bool Neo98ResumeSafe()
{
    if (neo98_resume_frames.size() > 64 || PAGING_NEO98_InFault()) return false;
    unsigned calls = 0;
    for (size_t i = 0; i < neo98_resume_frames.size(); ++i)
    {
        unsigned tag = neo98_resume_frames[i]->tag;
        if (tag >= 0x100 && tag < 0x200)
        {
            if (++calls == 1 && tag == 0x121) continue; // shell EXEC
            if (tag == 0x116 && i && neo98_resume_frames[i-1]->tag == 0x400) continue; // CON read
            return false; // unregistered native caller: retry after it returns
        }
        if (tag == 0x200 && !(i && (neo98_resume_frames[i-1]->tag == 0x700 || neo98_resume_frames[i-1]->tag == 0x701))) return false;
        if (tag == 0x300 && !(i && neo98_resume_frames[i-1]->tag == 0x800)) return false;
    }
    return true;
}

// A bounded extension beside the unchanged DOSBox core archive. The
// signatures of every frame are checked before any live local is changed.
static std::vector<Bit8u> Neo98ResumeBytes()
{
    std::vector<Bit8u> bytes;
    Bit32u count = (Bit32u)neo98_resume_frames.size();
    bytes.resize(8);
    Bit32u version = 1;
    memcpy(bytes.data(), &version, 4);
    memcpy(bytes.data()+4, &count, 4);
    for (const auto *frame : neo98_resume_frames)
    {
        DBPArchiveCounter size;
        frame->fields(size, false);
        if (size.count > 65536 || bytes.size() + size.count + 8 > 1048576) return {};
        Bit32u tag = frame->tag, length = (Bit32u)size.count;
        size_t at = bytes.size();
        bytes.resize(at + 8 + length);
        memcpy(bytes.data()+at, &tag, 4);
        memcpy(bytes.data()+at+4, &length, 4);
        DBPArchiveWriter write(bytes.data()+at+8, length);
        frame->fields(write, false);
        if (write.had_error) return {};
    }
    return bytes;
}
extern "C" size_t DBP_Neo98ResumeSize()
{
    if (!Neo98ResumeSafe()) return 0;
    return Neo98ResumeBytes().size();
}
extern "C" bool DBP_Neo98ResumeSave(void *out, size_t size)
{
    const auto bytes = Neo98ResumeBytes();
    if (!Neo98ResumeSafe() || bytes.empty() || size != bytes.size()) return false;
    memcpy(out, bytes.data(), size);
    return true;
}
extern "C" int DBP_Neo98ResumeLoad(const void *data, size_t size, bool apply)
{
    if (size < 8 || size > 1048576) return -1;
    const Bit8u *at = (const Bit8u*)data, *end = at + size;
    Bit32u version, count;
    memcpy(&version, at, 4); memcpy(&count, at+4, 4); at += 8;
    if (version != 1 || count > 64) return -1;
    bool match = count == neo98_resume_frames.size();
    std::vector<std::pair<const Bit8u*, Bit32u>> payloads;
    for (Bit32u i = 0; i < count; ++i)
    {
        if (size_t(end-at) < 8) return -1;
        Bit32u tag, length;
        memcpy(&tag, at, 4); memcpy(&length, at+4, 4); at += 8;
        if (length > 65536 || length > size_t(end-at)) return -1;
        if (i >= neo98_resume_frames.size() || tag != neo98_resume_frames[i]->tag) match = false;
        payloads.push_back({at, length});
        at += length;
    }
    if (at != end) return -1;
    if (!match || !Neo98ResumeSafe()) return 0;
    // Deserialize every frame into temporary values first. A CON payload
    // has a variable prefix length, so comparing only the live Counter
    // length would reject a legitimately restored partial line.
    for (size_t i = 0; i < payloads.size(); ++i)
    {
        DBPArchiveReader read(payloads[i].first, payloads[i].second);
        neo98_resume_frames[i]->fields(read, false);
        if (read.had_error == DBPArchive::ERR_GAMENOTRUNNING) return 0;
        if (read.had_error || read.GetOffset() != payloads[i].second) return -1;
    }
    if (apply)
        for (size_t i = 0; i < payloads.size(); ++i)
        {
            DBPArchiveReader read(payloads[i].first, payloads[i].second);
            neo98_resume_frames[i]->fields(read, true);
            if (read.had_error || read.GetOffset() != payloads[i].second) return -1;
        }
    return 1;
}
