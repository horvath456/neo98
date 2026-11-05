// SPDX-License-Identifier: GPL-2.0-or-later
// The registry read from the hives at run time against the Python import.
// tools/test_registry_hives_native.py builds and runs this.
//
// test-registry-hives SYSTEM.DAT USER.DAT NETAMD.INF NETCLI.INF DUMP
//
// 1. A store decoded from the generated image (tools/import_registry.py, the
// header the Python script made from the same hives just before) and a
// store decoded from the hives by src/registry_hives.h hold the same keys
// and values in the same order.
// 2. DUMP gets every key and value of both whole hives as the C++ decoder
// reads them; the Python script writes the same from its own decoder and
// compares the two files.
#include "../src/registry.h"
#include "../src/registry_hives.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static std::vector<unsigned char> load(const char *path)
{
    std::vector<unsigned char> bytes;
    FILE *file = fopen(path, "rb");
    if (!file) { fprintf(stderr, "cannot read %s\n", path); exit(2); }
    unsigned char block[65536];
    size_t got;
    while ((got = fread(block, 1, sizeof block, file)) > 0) bytes.insert(bytes.end(), block, block + got);
    fclose(file);
    return bytes;
}

static unsigned keys_seen = 0, values_seen = 0, differences = 0;

// Comparing two importers alone can miss the same defect in both. These
// lookups must reach the original hive through Win98's predefined aliases.
static bool aliases(Registry::Store &store, const char *label)
{
    unsigned classes = 0, profile = 0, direct = 0, alias = 0;
    bool created = false;
    const char *clsid = "CLSID\\{E436EBB3-524F-11CE-9F53-0020AF0BA770}\\InprocServer32";
    bool okay = !store.Open(store.roots[2], "Software\\Classes", false, classes, created) &&
                classes == store.roots[0] &&
                !store.Open(store.roots[2], "Config\\0001", false, profile, created) && profile == store.roots[5] &&
                !store.Open(store.roots[0], clsid, false, alias, created) &&
                !store.Open(classes, clsid, false, direct, created) && direct == alias;
    const Registry::Value *value = nullptr;
    if (okay) okay = !store.Query(alias, "", value) && value->type == 1 && value->size &&
                     strstr(reinterpret_cast<const char *>(value->data), "QUARTZ.DLL");
    unsigned count = 0;
    if (okay) {
        const unsigned parent = store.nodes[classes].parent;
        for (unsigned node = store.nodes[parent].child; node != Registry::Missing; node = store.nodes[node].sibling)
            if (!strcmp(store.nodes[node].name, "Classes")) ++count;
        okay = count == 1;
    }
    printf("registry aliases %s: %s\n", label, okay ? "PASS" : "FAIL");
    return okay;
}

// SHDOC401!SHCreateDesktop calls CoCreateInstance for this class at
// An empty HKCR makes that fail, leaves the desktop object null,
// and SHLWAPI!ordinal_107 subsequently copies a valid string to 000002E8
// Test the installed registration without a replay journal or
// the host's BROWSEUI registration additions.
static bool explorer_class(Registry::Store &store, const char *label)
{
    const char *clsid = "CLSID\\{AF604EFE-8897-11D1-B944-00A0C90312E1}\\InProcServer32";
    unsigned classes = 0, direct = 0, alias = 0;
    bool created = false;
    bool okay = !store.Open(store.roots[2], "Software\\Classes", false, classes, created) &&
                !store.Open(classes, clsid, false, direct, created) &&
                !store.Open(store.roots[0], clsid, false, alias, created) && direct == alias;
    const Registry::Value *value = nullptr;
    const char server[] = "C:\\WINDOWS\\SYSTEM\\BROWSEUI.DLL", threading[] = "Apartment";
    if (okay) okay = !store.Query(alias, "", value) && value->type == 1 &&
                     value->size == sizeof server && !memcmp(value->data, server, sizeof server);
    if (okay) okay = !store.Query(alias, "ThreadingModel", value) && value->type == 1 &&
                     value->size == sizeof threading && !memcmp(value->data, threading, sizeof threading);
    printf("Explorer desktop class %s: %s\n", label, okay ? "PASS" : "FAIL");
    return okay;
}

static void differ(const std::string &path, const char *what)
{
    if (++differences <= 40) printf("DIFFERENT %s: %s\n", path.c_str(), what);
}

static void compare(const Registry::Store &a, unsigned ka, const Registry::Store &b, unsigned kb, const std::string &path)
{
    ++keys_seen;
    for (unsigned i = 0;; ++i) {
        const Registry::Value *va = nullptr, *vb = nullptr;
        const unsigned ea = a.EnumValue(ka, i, va), eb = b.EnumValue(kb, i, vb);
        if (ea || eb) {
            if (ea != eb) differ(path, "number of values");
            break;
        }
        ++values_seen;
        if (strcmp(va->name, vb->name)) differ(path + " value " + va->name + " / " + vb->name, "name or order");
        else if (va->type != vb->type) differ(path + " value " + va->name, "type");
        else if (va->size != vb->size || (va->size && memcmp(va->data, vb->data, va->size))) differ(path + " value " + va->name, "payload");
    }
    for (unsigned i = 0;; ++i) {
        unsigned ca = 0, cb = 0;
        const unsigned ea = a.EnumKey(ka, i, ca), eb = b.EnumKey(kb, i, cb);
        if (ea || eb) {
            if (ea != eb) differ(path, "number of keys");
            break;
        }
        const char *na = a.nodes[ca].name, *nb = b.nodes[cb].name;
        if (strcmp(na, nb)) { differ(path + "\\" + na + " / " + nb, "key name or order"); continue; }
        if (strcmp(a.nodes[ca].klass, b.nodes[cb].klass)) differ(path + "\\" + na, "class");
        compare(a, ca, b, cb, path + "\\" + na);
    }
}

static void hex(FILE *out, const void *data, unsigned size)
{
    const auto *bytes = static_cast<const unsigned char *>(data);
    for (unsigned i = 0; i < size; ++i) fprintf(out, "%02x", bytes[i]);
}

static bool dump(FILE *out, const char *label, const std::vector<unsigned char> &hive)
{
    using namespace Registry::HiveImport;
    Arena arena;
    Builder builder(arena);
    Tree tree;
    TreeSink sink(tree);
    unsigned sink_error = 0;
    const unsigned error = Win98Creg::Read(hive.data(), (unsigned)hive.size(), sink, sink_error);
    if (error) { printf("FAIL %s does not decode: %u\n", label, error); return false; }
    fprintf(out, "H %s %u %u\n", label, tree.keys.count, tree.values.count);
    for (unsigned i = 0; i < tree.keys.count; ++i) {
        const Str path = tree.Path(i, builder);
        fputs("K ", out); hex(out, path.p, path.n); fputc('\n', out);
        for (unsigned k = 0; k < tree.keys[i].values; ++k) {
            const auto &value = tree.values[tree.keys[i].first_value + k];
            fputs("V ", out); hex(out, value.name.p, value.name.n);
            fprintf(out, " %u ", value.type); hex(out, value.data, value.size); fputc('\n', out);
        }
    }
    printf("decoded %s: %u keys, %u values\n", label, tree.keys.count, tree.values.count);
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 6) { fprintf(stderr, "usage: test-registry-hives SYSTEM.DAT USER.DAT NETAMD.INF NETCLI.INF DUMP\n"); return 2; }
    const auto system = load(argv[1]), user = load(argv[2]), netamd = load(argv[3]), netcli = load(argv[4]);
    bool okay = true;
    {
        Registry::Store image;
        Registry::Hives hives;
        hives.system = system.data(); hives.system_size = (unsigned)system.size();
        hives.user = user.data(); hives.user_size = (unsigned)user.size();
        hives.netamd = netamd.data(); hives.netamd_size = (unsigned)netamd.size();
        hives.netcli = netcli.data(); hives.netcli_size = (unsigned)netcli.size();
        Registry::StartFrom(&hives);
        Registry::Store decoded;
        Registry::StartFrom(nullptr);
        printf("image: source=%u error=%u keys=%u values=%u\n", image.start.source, image.import_error, image.start.keys, image.start.values);
        printf("hives: source=%u error=%u keys=%u values=%u network=%u\n", decoded.start.source, decoded.import_error,
               decoded.start.keys, decoded.start.values, decoded.start.network ? 1u : 0u);
        okay = image.start.source == 1 && decoded.start.source == 2 && !image.import_error && !decoded.import_error &&
               decoded.start.network && image.start.keys == decoded.start.keys && image.start.values == decoded.start.values;
        okay = aliases(image, "image") && okay;
        okay = aliases(decoded, "hives") && okay;
        okay = explorer_class(image, "image") && okay;
        okay = explorer_class(decoded, "hives") && okay;
        static const char *const names[] = {"HKEY_LOCAL_MACHINE", "HKEY_USERS", "HKEY_PERFORMANCE_DATA", "HKEY_DYN_DATA"};
        static const unsigned roots[] = {2, 3, 4, 6};
        for (unsigned i = 0; i < 4; ++i) compare(image, image.roots[roots[i]], decoded, decoded.roots[roots[i]], names[i]);
        printf("compared %u keys, %u values, %u differences\n", keys_seen, values_seen, differences);
        okay = okay && !differences;
        // A file that is no hive is refused the way VMM refuses it.
        std::vector<unsigned char> broken(system.begin(), system.begin() + 4096);
        broken[0] = 'X';
        Registry::Hives bad = hives;
        bad.system = broken.data(); bad.system_size = (unsigned)broken.size();
        Registry::StartFrom(&bad);
        Registry::Store refused;
        Registry::StartFrom(nullptr);
        printf("broken hive: error=%u\n", refused.import_error);
        okay = okay && refused.import_error == 1009;
        // Without the INF files the hives load, without the LAN card.
        Registry::Hives bare = hives;
        bare.netamd = bare.netcli = nullptr;
        Registry::StartFrom(&bare);
        Registry::Store plain;
        Registry::StartFrom(nullptr);
        printf("without INF: error=%u network=%u keys=%u\n", plain.import_error, plain.start.network ? 1u : 0u, plain.start.keys);
        okay = okay && !plain.import_error && !plain.start.network && plain.start.keys && plain.start.keys < decoded.start.keys;
    }
    FILE *out = fopen(argv[5], "wb");
    if (!out) { fprintf(stderr, "cannot write %s\n", argv[5]); return 2; }
    okay = dump(out, "SYSTEM.DAT", system) && okay;
    okay = dump(out, "USER.DAT", user) && okay;
    fclose(out);
    printf("%s\n", okay ? "Registry hives PASS" : "Registry hives FAIL");
    return okay ? 0 : 1;
}
