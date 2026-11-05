// SPDX-License-Identifier: GPL-2.0-or-later
// The PCM mixer. See src/sound.h for the mixer interface.
// Three things happen here and nothing else happens here:
// the pool Every submitted block is copied once, because the mixer reads
// it later on the host audio thread and guest memory may not be
// touched from there. The pool is one static array with a free
// list; a block that does not fit is refused, and the caller
// answers MMSYSERR_NOMEM.
// the render One pass over the open sources per output frame. Each source
// carries its own position as a frame index and a fraction of
// OutputRate, so the rate conversion is a running sum and needs
// no division per sample. Eight bit is unsigned with 128 in the
// middle, sixteen bit is signed little endian, mono goes to both
// channels, and the two values between which the fraction lies
// are interpolated.
// the queue Blocks finish in the order they were submitted. A block that
// carries WHDR_BEGINLOOP opens a loop that ends at the block
// with WHDR_ENDLOOP; the blocks in between do not finish until
// the last pass is over, which is what Windows promises.
#include "sound.h"
#include <new>
namespace Sound
{
namespace
{
// The byte pool and its free list. A chunk is either used or free; free
// neighbours are joined when a chunk is released, so a program that plays the
// same two buffers over and over keeps the list short.
struct Chunk
{
    unsigned start = 0, bytes = 0;
    bool used = false;
};
constexpr unsigned MaximumChunks = MaximumSources * InitialBlocks + 2;
unsigned char pool[PoolBytes];
Chunk chunks[MaximumChunks];
unsigned chunk_count = 0;

void PoolStart()
{
    chunk_count = 1;
    chunks[0] = Chunk{0, PoolBytes, false};
}
// Answers the offset of `bytes` in the pool, or PoolBytes when there is no
// room. Zero is a valid offset, so the failure is the size of the pool.
unsigned PoolTake(unsigned bytes)
{
    if (!chunk_count) PoolStart();
    if (!bytes || bytes > PoolBytes) return PoolBytes;
    bytes = (bytes + 3) & ~3u;
    for (unsigned i = 0; i < chunk_count; ++i)
    {
        if (chunks[i].used || chunks[i].bytes < bytes) continue;
        const unsigned start = chunks[i].start, whole = chunks[i].bytes;
        if (whole == bytes) { chunks[i].used = true; return start; }
        if (chunk_count == MaximumChunks) continue;
        // Make room for the remainder behind the chunk that is being taken.
        for (unsigned j = chunk_count; j > i + 1; --j) chunks[j] = chunks[j - 1];
        ++chunk_count;
        chunks[i] = Chunk{start, bytes, true};
        chunks[i + 1] = Chunk{start + bytes, whole - bytes, false};
        return start;
    }
    return PoolBytes;
}
void PoolGive(unsigned start)
{
    for (unsigned i = 0; i < chunk_count; ++i)
    {
        if (chunks[i].start != start || !chunks[i].used) continue;
        chunks[i].used = false;
        if (i + 1 < chunk_count && !chunks[i + 1].used)
        {
            chunks[i].bytes += chunks[i + 1].bytes;
            for (unsigned j = i + 1; j + 1 < chunk_count; ++j) chunks[j] = chunks[j + 1];
            --chunk_count;
        }
        if (i && !chunks[i - 1].used)
        {
            chunks[i - 1].bytes += chunks[i].bytes;
            for (unsigned j = i; j + 1 < chunk_count; ++j) chunks[j] = chunks[j + 1];
            --chunk_count;
        }
        return;
    }
}

Mixer mixer;
Output output;
short *capture = nullptr;
unsigned capture_frames = 0, captured = 0;
// The clock of the silent provider: when it started and how many frames it has
// rendered since. Keeping both drift free is simpler than a remainder.
unsigned long long silent_start = 0, silent_frames = 0;
bool silent_started = false;

inline int Clamp(int value)
{
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return value;
}
} // namespace

void SetOutput(const Output &value)
{
    if (output.Stop && output.owner != value.owner) output.Stop(output.owner);
    output = value;
    silent_start = silent_frames = 0;
    silent_started = false;
}
void SetCapture(short *buffer, unsigned frames)
{
    capture = buffer;
    capture_frames = frames;
    captured = 0;
}
unsigned Captured() { return captured; }
Mixer &Shared() { return mixer; }
void ResetAll()
{
    Speaker::Reset();   // Reset the speaker generator too.
    Microphone::ResetAll();   // Reset capture as well.
    for (unsigned source = 1; source <= MaximumSources; ++source)
        if (mixer.Valid(source)) mixer.Close(source);
    PoolStart();
    capture = nullptr;
    capture_frames = captured = 0;
    silent_start = silent_frames = 0;
    silent_started = false;
}

void Mixer::Lock() const { if (output.Lock) output.Lock(output.owner); }
void Mixer::Unlock() const { if (output.Unlock) output.Unlock(output.owner); }
void Lock() { if (output.Lock) output.Lock(output.owner); }
void Unlock() { if (output.Unlock) output.Unlock(output.owner); }
bool Mixer::Live() const { return output.Start != nullptr; }
bool Mixer::StartOutput() { return !Live() || output.Start(output.owner); }
// A mixer out of a saved state (src/x86_state_machine.cpp): its sources came
// with the state, the device of the host did not. A source that is open has
// started it once and never will again.
void Mixer::StateBind()
{
    silent_started = false;
    for (const auto &source : sources)
        if (source.open)
        {
            StartOutput();
            return;
        }
}

// What the wave driver of a Windows 98 sound card accepts, and what this mixer
// converts: PCM, one or two channels, eight or sixteen bits, and a rate the
// running sum can follow without overflowing.
bool Mixer::Supports(const Format &format)
{
    if (format.channels != 1 && format.channels != 2) return false;
    if (format.bits != 8 && format.bits != 16) return false;
    return format.rate >= 100 && format.rate <= 100000;
}

unsigned Mixer::Open(const Format &format)
{
    if (!Supports(format)) return 0;
    Lock();
    unsigned found = 0;
    for (unsigned i = 0; i < MaximumSources; ++i)
    {
        if (sources[i].open) continue;
        sources[i] = Source{};
        sources[i].open = true;
        sources[i].format = format;
        found = i + 1;
        break;
    }
    Unlock();
    if (found && Live() && !output.Start(output.owner))
    {
        Lock();
        sources[found - 1] = Source{};
        Unlock();
        return 0;
    }
    return found;
}

// A generator source has no format and no queue; it answers with frames in
// the output format whenever the mixer renders.
unsigned Mixer::OpenGenerator(Generator generator, void *owner)
{
    if (!generator) return 0;
    Lock();
    unsigned found = 0;
    for (unsigned i = 0; i < MaximumSources; ++i)
    {
        if (sources[i].open) continue;
        sources[i] = Source{};
        sources[i].open = true;
        sources[i].generator = generator;
        sources[i].generator_owner = owner;
        found = i + 1;
        break;
    }
    Unlock();
    if (found && Live() && !output.Start(output.owner))
    {
        Lock();
        sources[found - 1] = Source{};
        Unlock();
        return 0;
    }
    return found;
}

// A ring source: the memory of the owner, played where it lies.
unsigned Mixer::OpenRing(const unsigned char *memory, unsigned bytes, const Format &format)
{
    if (!memory || !bytes || !Supports(format)) return 0;
    Lock();
    unsigned found = 0;
    for (unsigned i = 0; i < MaximumSources; ++i)
    {
        if (sources[i].open) continue;
        sources[i] = Source{};
        sources[i].open = true;
        sources[i].format = format;
        sources[i].ring = memory;
        sources[i].ring_bytes = bytes;
        found = i + 1;
        break;
    }
    Unlock();
    if (found && Live() && !output.Start(output.owner))
    {
        Lock();
        sources[found - 1] = Source{};
        Unlock();
        return 0;
    }
    return found;
}
void Mixer::PlayRing(unsigned source, bool looping)
{
    if (!Valid(source) || !sources[source - 1].ring) return;
    Lock();
    sources[source - 1].ring_playing = true;
    sources[source - 1].ring_looping = looping;
    Unlock();
}
void Mixer::StopRing(unsigned source)
{
    if (!Valid(source) || !sources[source - 1].ring) return;
    Lock();
    sources[source - 1].ring_playing = false;
    Unlock();
}
bool Mixer::RingPlaying(unsigned source) const
{
    return Valid(source) && sources[source - 1].ring && sources[source - 1].ring_playing;
}
unsigned Mixer::RingCursor(unsigned source) const
{
    if (!Valid(source) || !sources[source - 1].ring) return 0;
    Lock();
    const Source &s = sources[source - 1];
    const unsigned offset = s.frame * s.format.channels * (s.format.bits / 8);
    Unlock();
    return offset < s.ring_bytes ? offset : 0;
}
void Mixer::SetRingCursor(unsigned source, unsigned offset)
{
    if (!Valid(source) || !sources[source - 1].ring) return;
    Lock();
    Source &s = sources[source - 1];
    const unsigned frame_bytes = s.format.channels * (s.format.bits / 8);
    s.frame = offset < s.ring_bytes ? offset / frame_bytes : 0;
    s.fraction = 0;
    Unlock();
}
void Mixer::SetRingRate(unsigned source, unsigned rate)
{
    if (!Valid(source) || !sources[source - 1].ring || rate < 100 || rate > 100000) return;
    Lock();
    sources[source - 1].format.rate = rate;
    Unlock();
}
void Mixer::SetRingFormat(unsigned source, const Format &format)
{
    if (!Valid(source) || !sources[source - 1].ring || !Supports(format)) return;
    Lock();
    sources[source - 1].format = format;
    sources[source - 1].frame = sources[source - 1].fraction = 0;
    Unlock();
}
unsigned OutputBlock()
{
    // Without a host the mixer renders what has come due whenever its clock
    // is advanced, a few milliseconds apart; ten milliseconds are the stretch
    // such a step takes when it comes late.
    return output.Start ? (output.block ? output.block : 1024) : OutputRate / 100;
}
unsigned Mixer::RingLead(unsigned source) const
{
    if (!Valid(source) || !sources[source - 1].ring) return 0;
    const Source &s = sources[source - 1];
    const unsigned frame_bytes = s.format.channels * (s.format.bits / 8);
    const unsigned long long frames = ((unsigned long long)OutputBlock() * s.format.rate + OutputRate - 1) / OutputRate;
    return (unsigned)frames * frame_bytes;
}
unsigned Mixer::RingsPlaying() const
{
    unsigned count = 0;
    for (const auto &source : sources)
        if (source.open && source.ring && source.ring_playing) ++count;
    return count;
}

bool Mixer::Valid(unsigned source) const
{
    return source && source <= MaximumSources && sources[source - 1].open;
}

void Mixer::Close(unsigned source)
{
    if (!Valid(source)) return;
    Lock();
    Source &s = sources[source - 1];
    for (unsigned i = s.first; i != s.tail; ++i) PoolGive(s.queue[i % s.queue.size()].offset);
    s = Source{};
    Unlock();
}

namespace
{
struct Copy
{
    const unsigned char *from;
};
bool CopyFill(void *owner, unsigned char *into, unsigned bytes)
{
    const unsigned char *from = ((Copy *)owner)->from;
    for (unsigned i = 0; i < bytes; ++i) into[i] = from[i];
    return true;
}
} // namespace

unsigned Mixer::Submit(unsigned source, const unsigned char *data, unsigned bytes,
                       unsigned tag, unsigned flags, unsigned loops)
{
    Copy copy{data};
    return Submit(source, bytes, tag, flags, loops, CopyFill, &copy);
}

unsigned Mixer::Submit(unsigned source, unsigned bytes, unsigned tag, unsigned flags,
                       unsigned loops, Fill fill, void *owner)
{
    if (!Valid(source) || !tag) return 1;
    Lock();
    Source &s = sources[source - 1];
    unsigned result = 0;
    // Keep enough completion slots for every outstanding header, including
    // tags the application has not collected yet. Render and Reset can then
    // return all headers without allocating on the audio thread.
    const unsigned capacity = (unsigned)s.queue.size();
    if ((unsigned long long)(s.tail - s.first) + s.done_count >= capacity)
    {
        if (capacity >= 0x80000000u) { Unlock(); return 2; }
        const unsigned next = capacity ? capacity * 2 : InitialBlocks;
        try
        {
            std::vector<Block> queue(next);
            std::vector<unsigned> done(next);
            for (unsigned i = s.first; i != s.tail; ++i)
                queue[i % next] = s.queue[i % capacity];
            for (unsigned i = 0; i < s.done_count; ++i)
                done[(s.done_head + i) % next] = s.done[(s.done_head + i) % capacity];
            s.queue.swap(queue);
            s.done.swap(done);
        }
        catch (const std::bad_alloc &) { Unlock(); return 2; }
    }
    {
        const unsigned offset = bytes ? PoolTake(bytes) : 0;
        if (bytes && offset == PoolBytes) result = 2;
        else if (bytes && !fill(owner, pool + offset, bytes))
        {
            PoolGive(offset);
            result = 2;
        }
        else
        {
            Block &block = s.queue[s.tail % s.queue.size()];
            block = Block{offset, bytes, tag, flags, loops};
            ++s.tail;
        }
    }
    Unlock();
    return result;
}

unsigned Mixer::Complete(unsigned source)
{
    if (!Valid(source)) return 0;
    Lock();
    Source &s = sources[source - 1];
    unsigned tag = 0;
    if (s.done_count)
    {
        tag = s.done[s.done_head % s.done.size()];
        ++s.done_head;
        --s.done_count;
    }
    Unlock();
    return tag;
}

void Mixer::Pause(unsigned source)
{
    if (!Valid(source)) return;
    Lock();
    sources[source - 1].paused = true;
    Unlock();
}
void Mixer::Restart(unsigned source)
{
    if (!Valid(source)) return;
    Lock();
    sources[source - 1].paused = false;
    Unlock();
}
void Mixer::BreakLoop(unsigned source)
{
    if (!Valid(source)) return;
    Lock();
    sources[source - 1].break_loop = true;
    Unlock();
}
void Mixer::Reset(unsigned source)
{
    if (!Valid(source)) return;
    Lock();
    Source &s = sources[source - 1];
    for (unsigned i = s.first; i != s.tail; ++i)
    {
        Block &block = s.queue[i % s.queue.size()];
        PoolGive(block.offset);
        if (s.done_count < s.done.size())
            s.done[(s.done_head + s.done_count++) % s.done.size()] = block.tag;
    }
    s.first = s.cursor = s.tail;
    s.frame = s.fraction = 0;
    s.looping = s.break_loop = false;
    s.loop_start = s.loop_left = 0;
    s.played = 0;
    s.paused = false;
    Unlock();
}

void Mixer::SetVolume(unsigned source, unsigned volume)
{
    if (!Valid(source)) return;
    Lock();
    sources[source - 1].volume = volume;
    Unlock();
}
unsigned Mixer::Volume(unsigned source) const
{
    return Valid(source) ? sources[source - 1].volume : 0xffffffff;
}
void Mixer::SetSourceGroup(unsigned source, unsigned group)
{
    if (!Valid(source) || group >= Groups) return;
    Lock();
    sources[source - 1].group = group;
    Unlock();
}
void Mixer::SetGroupVolume(unsigned group, unsigned volume)
{
    if (group >= Groups) return;
    Lock();
    group_volume[group] = volume;
    Unlock();
}
unsigned Mixer::GroupVolume(unsigned group) const { return group < Groups ? group_volume[group] : 0xffffffff; }
void Mixer::SetGroupMute(unsigned group, bool mute)
{
    if (group >= Groups) return;
    Lock();
    group_mute[group] = mute;
    Unlock();
}
bool Mixer::GroupMute(unsigned group) const { return group < Groups && group_mute[group]; }

void Mixer::SetMasterVolume(unsigned volume)
{
    Lock();
    master_volume = volume;
    Unlock();
}
unsigned Mixer::MasterVolume() const { return master_volume; }
void Mixer::SetMasterMute(bool mute)
{
    Lock();
    master_mute = mute;
    Unlock();
}
bool Mixer::MasterMute() const { return master_mute; }
unsigned long long Mixer::Played(unsigned source) const
{
    return Valid(source) ? sources[source - 1].played : 0;
}
Format Mixer::SourceFormat(unsigned source) const
{
    return Valid(source) ? sources[source - 1].format : Format{};
}
bool Mixer::Paused(unsigned source) const
{
    return Valid(source) && sources[source - 1].paused;
}
bool Mixer::Busy(unsigned source) const
{
    return Valid(source) && sources[source - 1].first != sources[source - 1].tail;
}

// Hands every block from `first` through `index` back to the owner and frees
// its pool bytes. Called when a block, or the last pass of a loop, is over.
void Mixer::Retire(Source &source, unsigned index)
{
    while ((int)(index - source.first) >= 0)
    {
        Block &block = source.queue[source.first % source.queue.size()];
        PoolGive(block.offset);
        if (source.done_count < source.done.size())
            source.done[(source.done_head + source.done_count++) % source.done.size()] = block.tag;
        ++source.first;
    }
}

// One ring over `frames` output frames. The bytes are read where the owner
// keeps them, at the cursor, at this moment; the two values the fraction lies
// between are interpolated as for a queued block, and the frame behind the
// last one of a looping ring is its first.
void Mixer::RenderRing(Source &source, short *out, unsigned frames, unsigned left_volume, unsigned right_volume)
{
    const unsigned channels = source.format.channels, bits = source.format.bits;
    const unsigned frame_bytes = channels * (bits / 8);
    const unsigned ring_frames = source.ring_bytes / frame_bytes;
    if (!ring_frames) { source.ring_playing = false; return; }
    if (source.frame >= ring_frames) source.frame = 0;
    for (unsigned f = 0; f < frames && source.ring_playing; ++f)
    {
        const unsigned char *bytes = source.ring + source.frame * frame_bytes;
        const bool last = source.frame + 1 >= ring_frames;
        const unsigned char *next = !last ? bytes + frame_bytes : source.ring_looping ? source.ring : bytes;
        int value[2] = {0, 0};
        for (unsigned c = 0; c < 2; ++c)
        {
            const unsigned lane = channels == 1 ? 0 : c;
            int a, b;
            if (bits == 8)
            {
                a = ((int)bytes[lane] - 128) * 256;
                b = ((int)next[lane] - 128) * 256;
            }
            else
            {
                a = (short)(bytes[lane * 2] | (bytes[lane * 2 + 1] << 8));
                b = (short)(next[lane * 2] | (next[lane * 2 + 1] << 8));
            }
            value[c] = a + (int)((long long)(b - a) * source.fraction / OutputRate);
        }
        const int left = out[f * 2] + (int)((long long)value[0] * left_volume / 0xffff);
        const int right = out[f * 2 + 1] + (int)((long long)value[1] * right_volume / 0xffff);
        out[f * 2] = (short)Clamp(left);
        out[f * 2 + 1] = (short)Clamp(right);
        source.fraction += source.format.rate;
        while (source.fraction >= OutputRate)
        {
            source.fraction -= OutputRate;
            source.played += frame_bytes;
            if (++source.frame < ring_frames) continue;
            source.frame = 0;
            if (!source.ring_looping) { source.ring_playing = false; source.fraction = 0; break; }
        }
    }
}

void Mixer::Hold(bool hold)
{
    Lock();
    held = hold;
    // Without a host device nothing renders during the pause: no block is
    // left to fade, and the first one afterwards starts at full volume.
    if (!Live()) faded = false;
    Unlock();
}

void Mixer::Render(short *out, unsigned frames)
{
    for (unsigned i = 0; i < frames * OutputChannels; ++i) out[i] = 0;
    if (held && faded) return;
    for (auto &source : sources)
    {
        if (!source.open || source.paused) continue;
        // The group of the source carries the device volume of its line; a
        // muted group is silent without losing the value.
        const unsigned group = source.group < Groups ? source.group : 0;
        // A generator runs even on a muted line: the note queue of MMSOUND
        // (src/pc_speaker.cpp) keeps its time in it, and a mute must not stop
        // that clock. Its frames are added at volume zero then.
        const bool muted = group_mute[group] != 0;
        if (muted && !source.generator) continue;
        const unsigned group_left = muted ? 0 : group_volume[group] & 0xffff;
        const unsigned group_right = muted ? 0 : (group_volume[group] >> 16) & 0xffff;
        if (source.generator)
        {
            // A generator writes its own frames; the mixer adds them with the
            // volume of the source, exactly as it adds a queued block.
            const unsigned left_volume = (source.volume & 0xffff) * group_left / 0xffff;
            const unsigned right_volume = ((source.volume >> 16) & 0xffff) * group_right / 0xffff;
            short scratch[128 * OutputChannels];
            unsigned done = 0;
            while (done < frames)
            {
                unsigned step = frames - done;
                if (step > 128) step = 128;
                source.generator(source.generator_owner, scratch, step);
                for (unsigned f = 0; f < step; ++f)
                {
                    const int left = out[(done + f) * 2] +
                                     (int)((long long)scratch[f * 2] * left_volume / 0xffff);
                    const int right = out[(done + f) * 2 + 1] +
                                      (int)((long long)scratch[f * 2 + 1] * right_volume / 0xffff);
                    out[(done + f) * 2] = (short)Clamp(left);
                    out[(done + f) * 2 + 1] = (short)Clamp(right);
                }
                source.played += step * OutputChannels * 2;
                done += step;
            }
            continue;
        }
        const unsigned channels = source.format.channels, bits = source.format.bits;
        const unsigned frame_bytes = channels * (bits / 8);
        const unsigned left_volume = (source.volume & 0xffff) * group_left / 0xffff;
        const unsigned right_volume = ((source.volume >> 16) & 0xffff) * group_right / 0xffff;
        if (source.ring)
        {
            if (source.ring_playing) RenderRing(source, out, frames, left_volume, right_volume);
            continue;
        }
        for (unsigned f = 0; f < frames; ++f)
        {
            if (source.cursor == source.tail) break;
            Block *block = &source.queue[source.cursor % source.queue.size()];
            // A block that starts a loop opens it the moment it is entered.
            if (!source.looping && !source.frame && !source.fraction && (block->flags & BeginLoop))
            {
                source.looping = true;
                source.loop_start = source.cursor;
                source.loop_left = block->loops ? block->loops : 1;
            }
            const unsigned block_frames = frame_bytes ? block->bytes / frame_bytes : 0;
            if (source.frame >= block_frames)
            {
                // The end of a block. Either the loop takes the cursor back or
                // the block, and everything the loop held, is over.
                if (source.looping && (block->flags & EndLoop) && !source.break_loop &&
                    source.loop_left > 1)
                {
                    --source.loop_left;
                    source.cursor = source.loop_start;
                    source.frame = 0;
                    continue;
                }
                if (source.looping && ((block->flags & EndLoop) || source.break_loop))
                {
                    source.looping = false;
                    source.break_loop = false;
                    source.loop_left = 0;
                }
                ++source.cursor;
                source.frame = 0;
                source.fraction = 0;
                if (!source.looping) Retire(source, source.cursor - 1);
                --f;
                continue;
            }
            const unsigned char *bytes = pool + block->offset + source.frame * frame_bytes;
            const bool last = source.frame + 1 >= block_frames;
            const unsigned char *next = last ? bytes : bytes + frame_bytes;
            int value[2] = {0, 0};
            for (unsigned c = 0; c < 2; ++c)
            {
                const unsigned lane = channels == 1 ? 0 : c;
                int a, b;
                if (bits == 8)
                {
                    a = ((int)bytes[lane] - 128) * 256;
                    b = ((int)next[lane] - 128) * 256;
                }
                else
                {
                    a = (short)(bytes[lane * 2] | (bytes[lane * 2 + 1] << 8));
                    b = (short)(next[lane * 2] | (next[lane * 2 + 1] << 8));
                }
                value[c] = a + (int)((long long)(b - a) * source.fraction / OutputRate);
            }
            const int accumulated_left = out[f * 2] + (int)((long long)value[0] * left_volume / 0xffff);
            const int accumulated_right =
                out[f * 2 + 1] + (int)((long long)value[1] * right_volume / 0xffff);
            out[f * 2] = (short)Clamp(accumulated_left);
            out[f * 2 + 1] = (short)Clamp(accumulated_right);
            source.fraction += source.format.rate;
            while (source.fraction >= OutputRate)
            {
                source.fraction -= OutputRate;
                ++source.frame;
                source.played += frame_bytes;
            }
        }
    }
    // Counted before the volume of the output: what the programs played.
    rendered_frames += frames;
    for (unsigned f = 0; f < frames; ++f)
    {
        bool audible = false;
        for (unsigned c = 0; c < OutputChannels; ++c) audible |= out[f * OutputChannels + c] != 0;
        audible_frames += audible;
    }
    // The volume of the output. A card scales the mixed sum here, after every
    // source has had its own volume, and the mute switch keeps the value.
    if (master_mute)
    {
        for (unsigned i = 0; i < frames * OutputChannels; ++i) out[i] = 0;
    }
    else if (master_volume != 0xffffffffu)
    {
        const unsigned left = master_volume & 0xffff, right = (master_volume >> 16) & 0xffff;
        for (unsigned f = 0; f < frames; ++f)
        {
            out[f * OutputChannels] = (short)((long long)out[f * OutputChannels] * left / 0xffff);
            if (OutputChannels > 1)
                out[f * OutputChannels + 1] = (short)((long long)out[f * OutputChannels + 1] * right / 0xffff);
        }
    }
    // The block that ends at a pause falls to silence, the first one after
    // it rises from silence.
    if (held != faded && frames)
    {
        for (unsigned f = 0; f < frames; ++f)
        {
            const unsigned gain = held ? frames - 1 - f : f + 1;
            for (unsigned c = 0; c < OutputChannels; ++c)
                out[f * OutputChannels + c] = (short)((int)out[f * OutputChannels + c] * (int)gain / (int)frames);
        }
        faded = held;
    }
}

void Mixer::Advance(unsigned long long now)
{
    if (Live()) return;
    if (!silent_started) { silent_started = true; silent_start = now; return; }
    if (now <= silent_start) return;
    const unsigned long long target = (now - silent_start) * OutputRate / 1000000;
    if (target <= silent_frames) return;
    unsigned long long due = target - silent_frames;
    if (due > OutputRate) due = OutputRate;
    silent_frames += due;
    short scratch[512];
    while (due)
    {
        unsigned step = due > 256 ? 256 : (unsigned)due;
        if (capture && captured < capture_frames)
        {
            if (step > capture_frames - captured) step = capture_frames - captured;
            Render(capture + captured * OutputChannels, step);
            captured += step;
        }
        else Render(scratch, step);
        due -= step;
    }
}
} // namespace Sound
