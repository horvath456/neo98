// SPDX-License-Identifier: GPL-2.0-or-later
#include "../src/registry.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using Registry::U;
int main()
{
    auto *store = new Registry::Store;
    {
        U enumeration=0, key=0, keys[Registry::DevnodeCapacity], handles[Registry::DevnodeCapacity];
        bool made=false;
        assert(!store->Open(store->roots[2],"Enum",true,enumeration,made));
        const U count=Registry::Devnodes(*store,enumeration,keys,Registry::DevnodeCapacity);
        for(U i=0;i<count;++i)handles[i]=Registry::DevnodeHandle(*store,keys[i]);
        const char *path="Root\\NEO98HandleProbe\\0000";
        assert(!store->Open(enumeration,path,true,key,made) && made);
        Registry::PublishDevnodes(*store);
        const U added=Registry::DevnodeHandle(*store,key);
        for(U i=0;i<count;++i)assert(handles[i]==Registry::DevnodeHandle(*store,keys[i]) && handles[i]!=added);
        assert(!store->DeleteKey(enumeration,path));
        Registry::PublishDevnodes(*store);
        char published[80];
        snprintf(published,sizeof published,"Config Manager\\Enum\\%08X",added);
        U missing=0;
        assert(store->Open(store->roots[6],published,false,missing,made)==2);
        assert(!store->Open(enumeration,path,true,key,made) && made);
        assert(Registry::DevnodeHandle(*store,key)!=added);
        assert(!store->DeleteKey(enumeration,path));
        Registry::PublishDevnodes(*store);
    }
    {
        Registry::Session a(*store), b(*store);
        U parent = 0, child = 0, other = 0, node = 0;
        bool made = false;
        assert(!a.Open(0x80000001, "Software\\NEO98Contract\\Nested", true, child, made) && made);
        assert(!a.Open(0x80000001, "software\\neo98contract", false, parent, made) && !made);
        assert(!a.Key(child, node));
        const unsigned char payload[] = {0, 255, 1, 0, 128};
        assert(!store->Set(node, "Binary", 3, payload, sizeof payload));
        assert(!b.Open(0x80000003, ".Default\\Software\\NEO98Contract\\nested", false, other, made));
        U same = 0;
        assert(!b.Key(other, same) && same == node);
        const Registry::Value *value = nullptr;
        assert(!store->Query(same, "bINARY", value) && value->type == 3 && value->size == sizeof payload &&
               !memcmp(value->data, payload, sizeof payload));
        U p = 0;
        assert(!a.Key(parent, p));
        U enumerated = 0;
        assert(!store->EnumKey(p, 0, enumerated) && enumerated == node &&
               store->EnumKey(p, 1, enumerated) == 259);
        assert(!store->DeleteKey(p, "Nested"));
        assert(a.Key(child, node) == 1018 && b.Key(other, node) == 1018);
        U fresh = 0;
        assert(!a.Open(parent, "Nested", true, fresh, made) && made);
        U fresh_node = 0;
        assert(!a.Key(fresh, fresh_node) && fresh_node != same);
        assert(!a.Close(child) && !b.Close(other));
        assert(a.Close(child) == 6);
        assert(!a.Open(parent, "Deep\\A\\B\\C", true, child, made));
        assert(!store->DeleteKey(p, "Deep") && a.Key(child, node) == 1018);
        assert(!a.Close(child));
        for (U i = 0; i < 300; ++i)
        {
            assert(!a.Open(parent, "Transient", true, child, made) && made);
            assert(!a.Key(child, node));
            assert(!store->Set(node, "", 1, "ok", 3));
            assert(!store->DeleteKey(node, ""));
            assert(a.Key(child, node) == 1018);
            U missing = 0;
            assert(a.Open(parent, "Transient", false, missing, made) == 2);
            assert(!a.Close(child) && a.Key(child, node) == 6);
        }
        assert(!a.Close(fresh) && !a.Close(parent));
        U root = 0;
        assert(!a.Open(0x80000002, "", false, root, made) && root == 0x80000002 && a.Close(root) == 0);
        assert(!a.Key(root, node) && store->DeleteKey(node, "Software") == 5);
        assert(store->DeleteKey(node, "") == 5);
    }
    {
        Registry::Session later(*store);
        U h = 0, n = 0;
        bool made = false;
        assert(!later.Open(0x80000001, "Software\\NEO98Contract\\Nested", false, h, made) && !made);
        assert(!later.Key(h, n));
        const Registry::Value *value = nullptr;
        assert(store->Query(n, "Binary", value) == 2);
    }
    U held_node = 0;
    {
        Registry::Session many(*store);
        U opened[600] = {};
        bool made = false;
        for (U &handle : opened)
            assert(!many.Open(0x80000002, "Software\\NEO98ManyHandles", true, handle, made));
        assert(!many.Key(opened[599], held_node));
        assert(store->nodes[held_node].references == 600);
        const U closed = opened[10];
        assert(!many.Close(closed));
        assert(!many.Open(0x80000002, "Software\\NEO98ManyHandles", false, opened[10], made));
        assert(opened[10] != closed && many.Key(closed, held_node) == 6);
        assert(!many.Key(opened[10], held_node));
        for (U handle : opened) assert(!many.Close(handle));
        assert(store->nodes[held_node].references == 0);
        assert(!many.Open(0x80000002, "Software\\NEO98ManyHandles", false, opened[0], made));
    }
    assert(store->nodes[held_node].references == 0);
    delete store;
    puts("REGISTRY PASS shared sessions, aliases, nested keys, binary values, recursive deletion, stale "
         "handles, reuse, growing handle storage and destructor cleanup");
}
