// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_registry.h"
#include <cassert>
#include <cstdlib>
#include <unistd.h>

int main()
{
    char folder[] = "/tmp/neo98-imported-setup-XXXXXX";
    assert(mkdtemp(folder));
    auto *store = new Registry::Store;
    unsigned setup = 0;
    bool created = false;
    assert(!store->Open(store->roots[2], "Software\\Microsoft\\Windows\\CurrentVersion\\Setup",
                        true, setup, created));
    assert(!store->Set(setup, "HWSetup", 1, "1", 1));
    assert(!HostRegistry::FinishImportedSetup(*store, folder));
    const Registry::Value *value = nullptr;
    assert(!store->Query(setup, "HWSetup", value));
    const std::string marker = std::string(folder) + "/.neo98-setup.json";
    FILE *file = fopen(marker.c_str(), "wb");
    assert(file);
    fputs("{}\n", file);
    fclose(file);
    const unsigned other = 1;
    assert(!store->Set(setup, "HWSetup", 4, &other, sizeof other));
    assert(!HostRegistry::FinishImportedSetup(*store, folder));
    assert(!store->Query(setup, "HWSetup", value) && value->type == 4);
    assert(!store->Set(setup, "HWSetup", 1, "2", 2));
    assert(!HostRegistry::FinishImportedSetup(*store, folder));
    assert(!store->Query(setup, "HWSetup", value) && value->data[0] == '2');
    assert(!store->Set(setup, "HWSetup", 1, "1", 1));
    assert(HostRegistry::FinishImportedSetup(*store, folder));
    assert(store->Query(setup, "HWSetup", value) == 2);
    assert(!HostRegistry::FinishImportedSetup(*store, folder));
    // A subsequent guest installer owns its new flag, even across calls.
    assert(!store->Set(setup, "HWSetup", 1, "1", 2));
    assert(!HostRegistry::FinishImportedSetup(*store, folder));
    assert(!store->Query(setup, "HWSetup", value) && value->size == 2);
    delete store;
    unlink(marker.c_str());
    rmdir(folder);
    puts("IMPORTED SETUP PASS: unmarked drive and other flags untouched, stale flag cleared once, later guest setup preserved");
}
