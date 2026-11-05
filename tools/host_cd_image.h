// SPDX-License-Identifier: GPL-2.0-or-later
// A CD image as a CD-ROM drive: the host provider behind src/cd_audio.h and
// the read-only volume of its data track.
// The image is described by a CUE sheet, the usual companion of a CD image:
// FILE "GAME.ISO" BINARY
// TRACK 01 MODE1/2048
// INDEX 01 00:00:00
// FILE "MUSIC.BIN" BINARY
// TRACK 02 AUDIO
// INDEX 01 00:00:00
// A FILE may carry several tracks (one BIN for the whole disc), the data track
// may be MODE1/2048 (an ISO), MODE1/2352 or MODE2/2352 (raw sectors), and an
// audio track is raw 2352-byte sectors of 16-bit little-endian stereo at
// 44100 Hz. PREGAP and INDEX 00 are read the way a burner reads them: they lie
// between two tracks on the disc and the audio of INDEX 01 is the track. A
// plain ISO file without a CUE sheet is a disc with one data track.
// The data track is read as ISO 9660, preferring the Joliet directory tree
// (UCS-2 big endian, without the ";1" version) over the primary tree.
// Hybrid discs still use UDF 1.02's complete directory tree when supported.
// Every write answers ERROR_WRITE_PROTECT, 19.
#pragma once
#include "../src/cd_audio.h"
#include "../src/volume.h"
#include "host_udf.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <strings.h>
#include <unistd.h>
#include <vector>
namespace HostCdImage
{
using U = unsigned;
class Image final : public Volume::Backend, public CdAudio::Disc
{
  public:
    ~Image() override
    {
        for (auto &file : files)
            if (file.handle >= 0)
                close(file.handle);
    }
    // Opens a CUE sheet or an ISO file. False with a reason in `error`.
    bool Open(const char *path, std::string &error)
    {
        const std::string text = path;
        const bool cue = text.size() > 4 && !strcasecmp(text.c_str() + text.size() - 4, ".cue");
        if (!cue)
        {
            if (!AddFile(text))
                return Fail(error, "cannot open " + text);
            tracks.push_back({1, false, 0, 0, 0, 0, 2048});
        }
        else if (!ParseCue(text, error))
            return false;
        if (!Place(error))
            return false;
        if (!ReadVolume())
            return Fail(error, "the data track has no ISO 9660 volume descriptor");
        udf_active = udf.Open([this](U sector, unsigned char *out, U inside, U bytes) {
            return ReadData(sector, out, inside, bytes);
        });
        description = text + " (CD image)";
        return true;
    }

    // CdAudio::Disc
    U Tracks() const override { return U(tracks.size()); }
    bool TrackAt(U index, CdAudio::Track &out) const override
    {
        if (index >= tracks.size())
            return false;
        const auto &t = tracks[index];
        out = {t.number, t.audio, t.start, t.length};
        return true;
    }
    U LeadOut() const override { return lead_out; }
    U ReadAudio(U lba, U sectors, unsigned char *out) override
    {
        U done = 0;
        for (; done < sectors; ++done)
        {
            const TrackRecord *t = TrackOf(lba + done);
            if (!t || !t->audio || !ReadRaw(*t, lba + done - t->start, out + done * CdAudio::SectorBytes,
                                            CdAudio::SectorBytes, 0))
                break;
        }
        return done;
    }

    // Volume::Backend
    bool Find(const char *absolute, Volume::Entry &out) override
    {
        if (udf_active) {
            HostUdf::Node node;
            if (!udf.Walk(absolute, node) || node.name.empty()) return false;
            DescribeUdf(node, out); return true;
        }
        Node node;
        if (!Walk(absolute, node) || node.root)
            return false;
        Describe(node, out);
        return true;
    }
    bool Child(const char *directory, U index, Volume::Entry &out) override
    {
        if (udf_active) {
            HostUdf::Node node;
            std::vector<HostUdf::Node> children;
            if (!udf.Walk(directory, node) || !udf.List(node, children) || index >= children.size()) return false;
            DescribeUdf(children[index], out); return true;
        }
        Node node;
        if (!Walk(directory, node) || !node.directory)
            return false;
        std::vector<Node> children;
        List(node, children);
        if (index >= children.size())
            return false;
        Describe(children[index], out);
        return true;
    }
    U Read(const char *absolute, U offset, void *buffer, U size, U &done) override
    {
        done = 0;
        if (udf_active) {
            HostUdf::Node node;
            if (!udf.Walk(absolute, node) || node.directory) return 2;
            if (offset >= node.size) return 0;
            size = std::min(size, node.size - offset);
            if (!udf.Read(node, offset, static_cast<unsigned char *>(buffer), size)) return 30;
            done = size; return 0;
        }
        Node node;
        if (!Walk(absolute, node) || node.directory)
            return 2;
        if (offset >= node.size)
            return 0;
        if (size > node.size - offset)
            size = node.size - offset;
        auto *out = static_cast<unsigned char *>(buffer);
        while (done < size)
        {
            const U at = offset + done, sector = node.extent + at / 2048, inside = at % 2048;
            const U part = 2048 - inside < size - done ? 2048 - inside : size - done;
            if (!ReadData(sector, out + done, inside, part))
                return 30;
            done += part;
        }
        return 0;
    }
    U Write(const char *, U, const void *, U, U &done) override { done = 0; return 19; }
    U Resize(const char *, U) override { return 19; }
    U Create(const char *, U, bool, Volume::Entry &) override { return 19; }
    U Remove(const char *, bool, bool) override { return 19; }
    U Rename(const char *, const char *) override { return 19; }
    U SetAttributes(const char *, U) override { return 19; }
    U SetTimes(const char *, const Files::TimeStamp *) override { return 19; }
    void Space(unsigned long long &total, unsigned long long &available) override
    {
        total = (unsigned long long)volume_blocks * 2048;
        available = 0;
    }
    const char *Label() override { return label.c_str(); }
    bool SetLabel(const char *) override { return false; }
    U Serial() override { return serial; }
    void Flush() override {}
    const char *Description() const override { return description.c_str(); }

  private:
    struct File
    {
        std::string path;
        int handle = -1;
        unsigned long long bytes = 0;
    };
    struct TrackRecord
    {
        U number = 0;
        bool audio = false;
        U start = 0, length = 0; // on the disc
        U file = 0;
        unsigned long long offset = 0; // byte of INDEX 01 in the file
        U sector_bytes = 2352;
        U pregap = 0;                  // PREGAP sectors, not in the file
        U index0 = ~0u, index1 = 0;    // frames from the start of the FILE
    };
    struct Node
    {
        bool root = false, directory = false;
        U extent = 0, size = 0;
        std::string name;
        unsigned char date[7] = {};
    };
    std::vector<File> files;
    std::vector<TrackRecord> tracks;
    U lead_out = 0, volume_blocks = 0, serial = 0;
    Node root_node;
    HostUdf::Volume udf;
    bool udf_active = false, joliet = false;
    std::string label, description;

    static bool Fail(std::string &error, const std::string &text)
    {
        error = text;
        return false;
    }
    bool AddFile(const std::string &path)
    {
        File file;
        file.path = path;
        file.handle = open(path.c_str(), O_RDONLY);
        if (file.handle < 0)
            return false;
        const off_t end = lseek(file.handle, 0, SEEK_END);
        file.bytes = end < 0 ? 0 : (unsigned long long)end;
        files.push_back(file);
        return true;
    }
    static U Msf(const char *text)
    {
        unsigned m = 0, s = 0, f = 0;
        sscanf(text, "%u:%u:%u", &m, &s, &f);
        return (m * 60 + s) * 75 + f;
    }
    bool ParseCue(const std::string &path, std::string &error)
    {
        FILE *cue = fopen(path.c_str(), "r");
        if (!cue)
            return Fail(error, "cannot open " + path);
        std::string folder = path;
        const auto slash = folder.rfind('/');
        folder = slash == std::string::npos ? std::string() : folder.substr(0, slash + 1);
        char line[512];
        bool ok = true;
        while (ok && fgets(line, sizeof line, cue))
        {
            char word[32] = {};
            if (sscanf(line, " %31s", word) != 1)
                continue;
            if (!strcasecmp(word, "FILE"))
            {
                const char *open = strchr(line, '"'), *close = open ? strchr(open + 1, '"') : nullptr;
                std::string name;
                if (open && close)
                    name.assign(open + 1, close);
                else
                {
                    char bare[400] = {};
                    sscanf(line, " %*s %399s", bare);
                    name = bare;
                }
                if (!AddFile(name[0] == '/' ? name : folder + name))
                    ok = Fail(error, "cannot open " + name);
            }
            else if (!strcasecmp(word, "TRACK"))
            {
                unsigned number = 0;
                char mode[32] = {};
                sscanf(line, " %*s %u %31s", &number, mode);
                if (files.empty())
                    ok = Fail(error, "TRACK before FILE");
                TrackRecord t;
                t.number = number;
                t.file = U(files.size() - 1);
                t.audio = !strcasecmp(mode, "AUDIO");
                t.sector_bytes = !strcasecmp(mode, "MODE1/2048") ? 2048 : 2352;
                if (!t.audio && strncasecmp(mode, "MODE1/", 6) && strncasecmp(mode, "MODE2/", 6))
                    ok = Fail(error, std::string("track mode ") + mode + " is not read");
                tracks.push_back(t);
            }
            else if (!strcasecmp(word, "INDEX") && !tracks.empty())
            {
                unsigned index = 0;
                char at[32] = {};
                sscanf(line, " %*s %u %31s", &index, at);
                if (index == 0)
                    tracks.back().index0 = Msf(at);
                else if (index == 1)
                    tracks.back().index1 = Msf(at);
            }
            else if (!strcasecmp(word, "PREGAP") && !tracks.empty())
            {
                char at[32] = {};
                sscanf(line, " %*s %31s", at);
                tracks.back().pregap = Msf(at);
            }
        }
        fclose(cue);
        if (ok && tracks.empty())
            ok = Fail(error, "the CUE sheet has no track");
        return ok;
    }
    // Lays the tracks out on the disc: each track starts where the previous
    // one ended plus its pregap, and its length runs to the next track of the
    // same file or to the end of the file.
    bool Place(std::string &error)
    {
        U at = 0;
        for (size_t i = 0; i < tracks.size(); ++i)
        {
            auto &t = tracks[i];
            const auto &file = files[t.file];
            t.offset = (unsigned long long)t.index1 * t.sector_bytes;
            unsigned long long end = file.bytes;
            if (i + 1 < tracks.size() && tracks[i + 1].file == t.file)
            {
                const auto &next = tracks[i + 1];
                end = (unsigned long long)(next.index0 != ~0u ? next.index0 : next.index1) * t.sector_bytes;
            }
            if (end < t.offset)
                return Fail(error, "track " + std::to_string(t.number) + " ends before it starts");
            const U gap = t.pregap + (t.index0 != ~0u && t.index1 > t.index0 ? t.index1 - t.index0 : 0);
            at += i ? gap : 0;
            t.start = at;
            t.length = U((end - t.offset) / t.sector_bytes);
            at += t.length;
        }
        lead_out = at;
        return true;
    }
    const TrackRecord *TrackOf(U lba) const
    {
        for (const auto &t : tracks)
            if (lba >= t.start && lba < t.start + t.length)
                return &t;
        return nullptr;
    }
    bool ReadRaw(const TrackRecord &t, U sector, unsigned char *out, U bytes, U inside) const
    {
        const auto &file = files[t.file];
        const unsigned long long at = t.offset + (unsigned long long)sector * t.sector_bytes + inside;
        return pread(file.handle, out, bytes, off_t(at)) == ssize_t(bytes);
    }
    // 2048 user bytes of a data sector: behind the 16-byte header of a MODE1
    // raw sector, the 24-byte header of a MODE2 form 1 sector.
    bool ReadData(U sector, unsigned char *out, U inside, U bytes) const
    {
        const TrackRecord *t = nullptr;
        for (const auto &track : tracks)
            if (!track.audio)
            {
                t = &track;
                break;
            }
        if (!t || sector >= t->length)
            return false;
        U header = 0;
        if (t->sector_bytes == 2352)
        {
            unsigned char mode = 1;
            if (!ReadRaw(*t, sector, &mode, 1, 15))
                return false;
            header = mode == 2 ? 24 : 16;
        }
        return ReadRaw(*t, sector, out, bytes, header + inside);
    }
    static U Le32(const unsigned char *p) { return p[0] | p[1] << 8 | p[2] << 16 | U(p[3]) << 24; }
    bool ReadVolume()
    {
        unsigned char descriptor[2048], selected[2048] = {};
        bool primary = false;
        U level = 0;
        // A boot descriptor or a primary descriptor can precede the SVD.
        // The terminator ends the ISO set, before any UDF recognition data.
        for (U sector = 16; sector < 256; ++sector)
        {
            if (!ReadData(sector, descriptor, 0, 2048) || memcmp(descriptor + 1, "CD001", 5) ||
                descriptor[6] != 1 || descriptor[0] == 255)
                break;
            const bool is_primary = descriptor[0] == 1;
            U candidate = 0;
            if (descriptor[0] == 2 && !(descriptor[7] & 1) &&
                descriptor[88] == '%' && descriptor[89] == '/')
                candidate = descriptor[90] == '@' ? 1 : descriptor[90] == 'C' ? 2 :
                            descriptor[90] == 'E' ? 3 : 0;
            Node root;
            if ((!is_primary && !candidate) || descriptor[128] != 0 || descriptor[129] != 8 ||
                !Record(descriptor + 156, root, false) || !root.directory)
                continue;
            if (is_primary && !primary)
            {
                primary = true;
                for (U i = 0; i < 2048; ++i)
                    serial = serial * 31 + descriptor[i];
                if (!level)
                    memcpy(selected, descriptor, sizeof selected);
            }
            if (candidate > level)
            {
                level = candidate;
                memcpy(selected, descriptor, sizeof selected);
            }
        }
        if (!primary)
            return false;
        joliet = level != 0;
        volume_blocks = Le32(selected + 80);
        std::string text = joliet ? Unicode(selected + 40, 32) :
                           std::string(reinterpret_cast<const char *>(selected + 40), 32);
        while (!text.empty() && (text.back() == ' ' || text.back() == 0))
            text.pop_back();
        label = text.substr(0, 11);
        if (!Record(selected + 156, root_node, false))
            return false;
        root_node.root = true;
        return true;
    }
    static std::string Unicode(const unsigned char *text, U bytes)
    {
        std::string out;
        for (U i = 0; i + 1 < bytes; i += 2)
        {
            const U c = U(text[i]) << 8 | text[i + 1];
            // Volume::Entry has the same single-byte guest spelling as
            // HostVolume and HostUdf, not a UTF-8 host path.
            out += c <= 255 ? char(c) : '_';
        }
        return out;
    }
    static bool Record(const unsigned char *r, Node &out, bool unicode)
    {
        if (r[0] < 34 || !r[32] || r[32] > r[0] - 33)
            return false;
        const bool special = r[32] == 1 && r[33] <= 1;
        if (unicode && !special && (r[32] & 1))
            return false;
        out = Node{};
        out.extent = Le32(r + 2);
        out.size = Le32(r + 10);
        memcpy(out.date, r + 18, 7);
        out.directory = (r[25] & 2) != 0;
        out.name = unicode && !special ? Unicode(r + 33, r[32]) :
                   std::string(reinterpret_cast<const char *>(r + 33), r[32]);
        if (!special && (out.name.find('\0') != std::string::npos ||
                         out.name.find_first_of("/\\") != std::string::npos))
            return false;
        const auto version = out.name.find(';');
        if (version != std::string::npos)
            out.name.resize(version);
        if (!out.name.empty() && out.name.back() == '.')
            out.name.pop_back();
        return true;
    }
    void List(const Node &directory, std::vector<Node> &out) const
    {
        std::vector<unsigned char> bytes(directory.size);
        for (U done = 0; done < directory.size; done += 2048)
            if (!ReadData(directory.extent + done / 2048, bytes.data() + done, 0,
                          directory.size - done < 2048 ? directory.size - done : 2048))
                return;
        for (U at = 0; at < directory.size;)
        {
            const U length = bytes[at];
            if (!length)
            {
                at = (at / 2048 + 1) * 2048; // records do not cross a sector
                continue;
            }
            Node node;
            if (length <= 2048 - at % 2048 && length <= directory.size - at &&
                Record(&bytes[at], node, joliet))
            {
                if (bytes[at + 32] != 1 || bytes[at + 33] > 1)
                    out.push_back(node); // skip "." and ".."
            }
            at += length;
        }
    }
    bool Walk(const char *absolute, Node &out) const
    {
        if (!absolute || !absolute[0] || absolute[1] != ':')
            return false;
        out = root_node;
        const char *p = absolute + 2;
        while (*p)
        {
            while (*p == '\\' || *p == '/')
                ++p;
            if (!*p)
                break;
            std::string part;
            while (*p && *p != '\\' && *p != '/')
                part += *p++;
            if (!out.directory)
                return false;
            std::vector<Node> children;
            List(out, children);
            bool matched = false;
            for (const auto &child : children)
                if (!strcasecmp(child.name.c_str(), part.c_str()))
                {
                    out = child;
                    matched = true;
                    break;
                }
            if (!matched)
                return false;
        }
        return true;
    }
    static void DescribeUdf(const HostUdf::Node &source, Volume::Entry &out)
    {
        Node node;
        node.name = source.name; node.size = source.size; node.directory = source.directory;
        memcpy(node.date, source.date, sizeof node.date);
        Describe(node, out);
    }
    static void Describe(const Node &node, Volume::Entry &out)
    {
        out = Volume::Entry{};
        snprintf(out.name, sizeof out.name, "%s", node.name.c_str());
        out.attributes = node.directory ? 0x11u : 0x01u; // read-only, directory
        out.size = node.directory ? 0 : node.size;
        // ISO 9660 dates: years since 1900, month, day, hour, minute, second.
        const U year = node.date[0] >= 80 ? node.date[0] - 80u : 0u;
        const U date = year << 9 | U(node.date[1]) << 5 | node.date[2];
        const U time = U(node.date[3]) << 11 | U(node.date[4]) << 5 | node.date[5] / 2u;
        for (auto &stamp : out.times)
            stamp = Files::TimeStamp{date, time, 0};
    }
};
} // namespace HostCdImage
