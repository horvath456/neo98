// SPDX-License-Identifier: GPL-2.0-or-later
// The hive import must reproduce the timezone catalog the earlier, independent
// local hive extractor, byte for byte.
#include "../src/registry.h"
#include "../build/timedate-registry-data/win98_timezone_registry_data.h"
#include <cassert>
#include <cstdio>
#include <cstring>
int main()
{
    using Registry::U;
    using namespace Win98TimezoneRegistry;
    assert(!Registry::SystemInitializationError());
    auto &s = Registry::System();
    U used_nodes = 0, used_values = 0;
    for (U i = 0; i < s.node_capacity; ++i)
        used_nodes += s.nodes[i].used;
    for (U i = 0; i < s.value_capacity; ++i)
        used_values += s.values[i].used;
    U root = 0;
    bool made = false;
    assert(!s.Open(s.roots[2], Root, false, root, made));
    U children = 0, node = 0;
    while (!s.EnumKey(root, children, node))
        ++children;
    assert(children == 63);
    for (auto &seed : Values)
    {
        U key = 0;
        assert(!s.Open(root, Keys[seed.key], false, key, made));
        const Registry::Value *value = nullptr;
        assert(!s.Query(key, seed.name, value));
        assert(value->type == seed.type && value->size == seed.size &&
               !memcmp(value->data, Bytes + seed.offset, seed.size));
    }
    // The spare slots above the imported image are finite and report exhaustion.
    auto *full = new Registry::Store;
    assert(!full->import_error);
    U spare = 0, key = 0, error = 0;
    for (char name[32]; !error; ++spare)
    {
        snprintf(name, sizeof name, "NEO98Spare%u", spare);
        error = full->Open(full->roots[1], name, true, key, made);
    }
    assert(error == 8 && spare > 1 && spare <= 1025);
    U values = 0;
    assert(!full->Open(full->roots[1], "NEO98Spare0", false, key, made));
    for (char name[32]; !error || error == 8;)
    {
        snprintf(name, sizeof name, "v%u", values);
        error = full->Set(key, name, 1, "x", 2);
        if (error)
            break;
        ++values;
    }
    assert(error == 8 && values > 1 && values <= 1024);
    delete full;
    printf("TIMEZONE REGISTRY PASS zones=%u values=%zu used_nodes=%u/%u used_values=%u/%u "
           "spare_keys=%u spare_values=%u\n",
           children, sizeof Values / sizeof *Values, used_nodes, s.node_capacity, used_values,
           s.value_capacity, spare, values);
}
