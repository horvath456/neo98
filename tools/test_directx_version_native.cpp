// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_registry.h"
#include <cassert>
#include <fstream>
#include <iterator>

static void Seed(const char *file, const char *path)
{
    std::ifstream input(file, std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)), {});
    assert(!bytes.empty());
    assert(!Files::System().Seed(path, bytes.data(), unsigned(bytes.size())));
}

int main(int argc, char **argv)
{
    assert(argc == 6);
    auto &registry = Registry::System();
    unsigned key = 0;
    bool created = false;
    assert(!registry.Open(registry.roots[2], "Software\\Microsoft\\DirectX", true, key, created));
    static const char old[] = "4.06.03.0518", upgraded[] = "4.09.00.0904", custom[] = "4.09.99.9999";
    assert(!registry.Set(key, "Version", 1, old, sizeof old));
    Seed(argv[1], "C:\\WINDOWS\\SYSTEM\\DSOUND.DLL");
    Seed(argv[2], "C:\\WINDOWS\\SYSTEM\\DDRAW.DLL");
    assert(!HostRegistry::SyncDirectXVersion());
    Seed(argv[3], "C:\\WINDOWS\\SYSTEM\\DSOUND.DLL");
    assert(!HostRegistry::SyncDirectXVersion());
    Seed(argv[4], "C:\\WINDOWS\\SYSTEM\\DDRAW.DLL");
    assert(!HostRegistry::SyncDirectXVersion());
    Seed(argv[5], "C:\\WINDOWS\\SYSTEM\\D3D9.DLL");
    assert(HostRegistry::SyncDirectXVersion());
    const Registry::Value *value = nullptr;
    assert(!registry.Query(key, "Version", value));
    assert(value->size == sizeof upgraded && !memcmp(value->data, upgraded, sizeof upgraded));
    assert(!HostRegistry::SyncDirectXVersion());
    assert(!registry.Set(key, "Version", 1, custom, sizeof custom));
    assert(!HostRegistry::SyncDirectXVersion());
    assert(!registry.Query(key, "Version", value));
    assert(value->size == sizeof custom && !memcmp(value->data, custom, sizeof custom));
    puts("DirectX version PASS: vanilla, partial upgrade, complete 9.0c, idempotence, custom installer version");
}
