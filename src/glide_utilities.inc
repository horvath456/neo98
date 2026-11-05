// SPDX-License-Identifier: GPL-2.0-or-later
// Glide 2 texture utilities. The public 196-byte GrMipMapInfo records live
// in guest memory, like the original driver's 1024-record table.
unsigned Context::UtilityTable(Host &host)
{
    static_assert(sizeof(UtilityInfo) == 196, "GrMipMapInfo layout");
    if (!utility_table)
    {
        utility_table = host.Allocate(sizeof(UtilityInfo) * 1024);
        if (utility_table)
        {
            std::vector<unsigned char> zero(sizeof(UtilityInfo) * 1024);
            if (!host.Write(utility_table, zero.data(), (unsigned)zero.size())) return 0;
        }
    }
    return utility_table;
}

void Context::UtilityRegisters(UtilityInfo &info, bool allocation)
{
    static constexpr unsigned aspect_bits[] = {0x700000, 0x500000, 0x300000, 0, 0x200000, 0x400000, 0x600000};
    static constexpr unsigned level_bits[] = {~0u, 0x80000, 0xc0000, 0};
    info.texture_mode = 9 | info.format << 8 | (info.mag_filter == 1 ? 4 : 0) |
        (info.min_filter == 1 ? 2 : 0) | (info.s_clamp == 1 ? 0x40 : 0) |
        (info.t_clamp == 1 ? 0x80 : 0) | (info.mipmap == 2 ? 0x10 : 0) |
        (info.trilinear ? 0x40000000 : 0);
    // With mipmapping disabled both register limits name the large level.
    const unsigned small = info.mipmap ? info.small_lod : info.large_lod;
    info.t_lod = small << 8 | info.large_lod << 2 | aspect_bits[info.aspect] |
                 level_bits[info.even_odd & 3] | info.bias << 12;
    if (allocation && info.trilinear)
    {
        if (info.even_odd & 2) info.t_lod |= 0x40000;
        if (info.even_odd != 3) info.t_lod |= 0x80000;
    }
}

unsigned Context::UtilityCall(unsigned ordinal, const unsigned *a, Host &host)
{
    using namespace Win98Glide;
    if (ordinal == guTexMemReset)
    {
        utility_count = 0;
        for (unsigned t = 0; t < 2; ++t)
        {
            utility_used[t] = 0;
            utility_current[t] = ~0u;
            utility_ncc_next[t] = 0;
            utility_ncc[t][0] = utility_ncc[t][1] = ~0u;
        }
        if (utility_table)
        {
            std::vector<unsigned char> zero(sizeof(UtilityInfo) * 1024);
            host.Write(utility_table, zero.data(), (unsigned)zero.size());
        }
        return 0;
    }
    if (ordinal == guTexMemQueryAvail || ordinal == guTexGetCurrentMipMap)
    {
        if (a[0] >= Card::Tmus) return Unsupported("guTex query of an invalid TMU", host);
        return ordinal == guTexMemQueryAvail ? Card::TmuBytes - utility_used[a[0]] : utility_current[a[0]];
    }
    if (ordinal == guTexAllocateMemory)
    {
        if (a[0] >= Card::Tmus || a[6] > 8 || a[7] > a[6] || a[8] > 6 || !(a[1] & 3) || a[4] > 15)
            return Unsupported("guTexAllocateMemory with invalid texture parameters", host);
        const unsigned tmu = a[0], even_odd = a[1] & 3;
        unsigned bytes = 0;
        for (unsigned lod = a[7]; lod <= a[6]; ++lod)
            if (LevelPresent(lod, a[7], even_odd)) bytes += LevelBytes(lod, a[8], a[4]);
        bytes = (bytes + 7) & ~7u;
        // An allocation must not cross a 2 MiB bank, even when it fails.
        if (utility_used[tmu] < Card::TmuBytes && bytes > Card::TmuBytes - utility_used[tmu])
            utility_used[tmu] = Card::TmuBytes;
        if (bytes > Card::TmuBytes - utility_used[tmu] || utility_count == 1024) return ~0u;
        const unsigned table = UtilityTable(host);
        if (!table) return ~0u;
        UtilityInfo info{};
        info.valid = 1;
        info.width = a[2]; info.height = a[3]; info.format = a[4]; info.mipmap = a[5];
        info.small_lod = a[6]; info.large_lod = a[7]; info.aspect = a[8];
        info.s_clamp = a[9]; info.t_clamp = a[10]; info.min_filter = a[11]; info.mag_filter = a[12];
        const float bias = Float(a[13]);
        const double encoded_bias = (double(bias) + 0.125) * 4;
        if (!std::isfinite(encoded_bias) || encoded_bias < -2147483648.0 || encoded_bias >= 2147483648.0)
            return Unsupported("guTexAllocateMemory with invalid LOD bias", host);
        info.bias = unsigned(int(encoded_bias)) & 63;
        info.trilinear = a[14]; info.tmu = tmu; info.even_odd = even_odd;
        info.base = utility_used[tmu] - LevelOffset(info.large_lod, 0, info.aspect, info.format, even_odd);
        UtilityRegisters(info, true);
        if (!host.Write(table + utility_count * sizeof info, &info, sizeof info)) return ~0u;
        utility_used[tmu] += bytes;
        return utility_count++;
    }
    if (a[0] == ~0u && (ordinal == guTexSource || ordinal == guTexChangeAttributes)) return 0;
    if (a[0] >= 1024) return Unsupported("guTex utility with an out-of-range identifier", host);
    const unsigned table = UtilityTable(host);
    if (!table) return 0;
    const unsigned address = table + a[0] * sizeof(UtilityInfo);
    if (ordinal == guTexGetMipMapInfo) return address;
    UtilityInfo info;
    if (!host.Read(address, &info, sizeof info)) return 0;
    if (ordinal == guTexChangeAttributes)
    {
        unsigned *attributes[] = {&info.width, &info.height, &info.format, &info.mipmap,
            &info.small_lod, &info.large_lod, &info.aspect, &info.s_clamp, &info.t_clamp,
            &info.min_filter, &info.mag_filter};
        for (unsigned i = 0; i < 11; ++i) if (a[i + 1] != ~0u) *attributes[i] = a[i + 1];
        if (info.aspect > 6) return Unsupported("guTexChangeAttributes with an invalid aspect", host);
        UtilityRegisters(info, false);
        return host.Write(address, &info, sizeof info) ? 1 : 0;
    }
    if (info.tmu >= Card::Tmus || info.large_lod > 8 || info.small_lod > 8 || info.aspect > 6 || info.format > 15)
        return Unsupported("guTex utility with invalid public texture metadata", host);
    const unsigned start = info.base + LevelOffset(info.large_lod, 0, info.aspect, info.format, info.even_odd);
    auto download = [&](unsigned lod, unsigned data) {
        if (LevelPresent(lod, info.large_lod, info.even_odd))
        {
            const unsigned args[] = {info.tmu, start, lod, info.large_lod, info.aspect, info.format, info.even_odd, data};
            Call(grTexDownloadMipMapLevel, args, host);
        }
        int w, h;
        LevelSize(lod, info.aspect, w, h);
        return data + unsigned(w * h) * TexelBytes(info.format);
    };
    if (ordinal == guTexDownloadMipMap)
    {
        info.data = a[1];
        if (info.format == 1 && !host.Read(a[2], info.ncc, sizeof info.ncc)) return 0;
        if (!host.Write(address, &info, sizeof info)) return 0;
        unsigned data = a[1];
        for (unsigned lod = info.large_lod; lod <= info.small_lod; ++lod) data = download(lod, data);
        return 0;
    }
    if (ordinal == guTexDownloadMipMapLevel)
    {
        if (a[1] > 8) return Unsupported("guTexDownloadMipMapLevel with an invalid level", host);
        unsigned data = 0;
        if (!host.Read(a[2], &data, sizeof data)) return 0;
        data = download(a[1], data);
        host.Write(a[2], &data, sizeof data);
        return 0;
    }
    // guTexSource leaves the combine function and unrelated render state alone.
    Flush();
    dirty = true;
    const unsigned tmu = info.tmu;
    utility_current[tmu] = a[0];
    source[tmu] = {true, start, info.even_odd, info.small_lod, info.large_lod, info.aspect, info.format};
    state.tmu[tmu].mipmap = info.mipmap;
    state.tmu[tmu].min_bilinear = (info.texture_mode & 2) != 0;
    state.tmu[tmu].mag_bilinear = (info.texture_mode & 4) != 0;
    state.tmu[tmu].clamp_s = (info.texture_mode & 0x40) != 0;
    state.tmu[tmu].clamp_t = (info.texture_mode & 0x80) != 0;
    const unsigned bias = (info.t_lod >> 12) & 63;
    state.tmu[tmu].lod_bias = float(int(bias) - (bias & 32 ? 64 : 0)) * 0.25f;
    if (info.format == 1 || info.format == 9)
    {
        unsigned slot = 0;
        if (utility_ncc[tmu][0] == a[0]) slot = 0;
        else if (utility_ncc[tmu][1] == a[0]) slot = 1;
        else
        {
            slot = utility_ncc_next[tmu];
            utility_ncc_next[tmu] ^= 1;
            utility_ncc[tmu][slot] = a[0];
            const unsigned args[] = {tmu, slot, address + 84};
            Call(grTexDownloadTable, args, host);
        }
        ncc_select[tmu] = slot;
    }
    return 0;
}
