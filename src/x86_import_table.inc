// SPDX-License-Identifier: GPL-2.0-or-later
// Host import names use case-insensitive DLLs and case-sensitive exports.
// Both the session directory and a process index bound names, never holes.
static unsigned ImportNameHash(const char *dll, const char *name)
{
    unsigned hash = 2166136261u;
    for (const char *p = dll; p && *p; ++p)
        hash = (hash ^ (unsigned char)(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p)) * 16777619u;
    hash = (hash ^ '!') * 16777619u;
    for (const char *p = name; p && *p; ++p) hash = (hash ^ (unsigned char)*p) * 16777619u;
    return hash;
}
static bool ImportDllEqual(const char *a, const char *b)
{
    if (!a) a = "";
    if (!b) b = "";
    for (;; ++a, ++b) {
        const unsigned x = (unsigned char)(*a >= 'A' && *a <= 'Z' ? *a + 32 : *a);
        const unsigned y = (unsigned char)(*b >= 'A' && *b <= 'Z' ? *b + 32 : *b);
        if (x != y) return false;
        if (!x) return true;
    }
}
bool Application::ImportTable::Reserve(unsigned index)
{
    auto *&chunk = chunks[index >> ChunkBits];
    if (!chunk) chunk = static_cast<Import *>(calloc(ChunkSize, sizeof(Import)));
    return chunk != nullptr;
}
Application::ImportTable::~ImportTable()
{
    for (auto *chunk : chunks) free(chunk);
}
unsigned Application::ImportTable::Find(const char *dll, const char *name, unsigned limit) const
{
    if (!dll || !name) return 0;
    for (unsigned at = names[ImportNameHash(dll, name) & (NameBuckets - 1)]; at; at = (*this)[at - 1].name_next) {
        const auto &item = (*this)[at - 1];
        if (at <= limit && ImportDllEqual(item.dll, dll) && !strcmp(item.name, name)) return at;
    }
    return 0;
}
void Application::ImportTable::Index(unsigned slot)
{
    auto &item = (*this)[slot];
    auto &head = names[ImportNameHash(item.dll, item.name) & (NameBuckets - 1)];
    // Binding a session gate again keeps its name and its existing link.
    for (unsigned at = head; at; at = (*this)[at - 1].name_next) if (at == slot + 1) return;
    item.name_next = head;
    head = slot + 1;
}
