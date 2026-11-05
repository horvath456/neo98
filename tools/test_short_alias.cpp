// SPDX-License-Identifier: GPL-2.0-or-later
// The 8.3 alias rule of src/win98_short_alias.h against the aliases the Win98
// installation actually carries in its FAT directories.
// It walks the Win98 folder of setup (build/win98-c), whose sidecars.neo98-index hold the aliases tools/win98_folder.py took over from FAT, and
// asks the rule for the alias of every long name. The comparison uses the
// stored aliases as the set of taken names, so the answer does not depend on
// the order this program walks a directory in.
// Entries whose stored alias has a numeric tail other than ~1 are counted
// separately: their tail says in which order that directory was once filled,
// which a finished installation cannot tell us.
#include "../src/win98_short_alias.h"
#include "legacy_data.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
std::string Upper(std::string text)
{
    for (auto &c : text)
        c = char(Win98ShortAlias::Fold((unsigned char)c));
    return text;
}
// tools/host_volume.h ToHost: a byte above 127 is the UTF-8 form of the code
// point with the same number.
std::string ToHost(const std::string &guest)
{
    std::string out;
    for (unsigned char c : guest)
        if (c < 0x80)
            out += char(c);
        else
        {
            out += char(0xc0 | (c >> 6));
            out += char(0x80 | (c & 0x3f));
        }
    return out;
}
struct Folder
{
    std::vector<std::string> taken;
};
std::unordered_map<std::string, Folder> folders;
std::vector<std::pair<std::string, std::string>> entries;   // guest path, stored alias

bool Walk(const std::string &host, const std::string &guest)
{
    FILE *file = fopen(LegacyData::ReadPath(host + "/.neo98-index").c_str(), "r");
    if (!file)
        return false;
    std::vector<std::string> directories;
    char line[4096];
    while (fgets(line, sizeof line, file))
    {
        if (line[0] == '#')
            continue;
        char *fields[5] = {};
        char *cursor = line;
        unsigned count = 0;
        for (; count < 5; ++count)
        {
            fields[count] = cursor;
            char *tab = strchr(cursor, '\t');
            if (!tab)
                break;
            *tab = 0;
            cursor = tab + 1;
        }
        if (count != 4)
            continue;
        char *end = fields[4] + strlen(fields[4]);
        while (end > fields[4] && (end[-1] == '\n' || end[-1] == '\r'))
            *--end = 0;
        const std::string name = fields[4];
        const std::string path = guest.empty() ? name : guest + "\\" + name;
        // The sidecar leaves the alias field empty when the long name already
        // is the short name; then the short name is the upper-case long name.
        const std::string alias = *fields[3] ? fields[3] : Upper(name);
        folders[Upper(guest)].taken.push_back(Upper(alias));
        entries.push_back({path, alias});
        if (strtoul(fields[1], nullptr, 10) & 16)
            directories.push_back(name);
    }
    fclose(file);
    for (const auto &name : directories)
        Walk(host + "/" + ToHost(name), guest.empty() ? name : guest + "\\" + name);
    return true;
}
} // namespace

int main(int argc, char **argv)
{
    const char *root = argc > 1 ? argv[1] : "build/win98-c";
    if (!Walk(root, ""))
    {
        fprintf(stderr, "no Win98 folder at %s; run python3 neo98.py setup --iso ...\n", root);
        return 2;
    }
    unsigned same = 0, differs = 0, ordered = 0;
    std::vector<std::string> examples;
    for (const auto &entry : entries)
    {
        const std::string &path = entry.first;
        const std::string parent = path.rfind('\\') == std::string::npos
                                       ? std::string()
                                       : path.substr(0, path.rfind('\\'));
        const std::string name = path.rfind('\\') == std::string::npos
                                     ? path
                                     : path.substr(path.rfind('\\') + 1);
        const std::string &stored = entry.second;
        const char *tilde = strchr(stored.c_str(), '~');
        if (tilde && !(tilde[1] == '1' && (!tilde[2] || tilde[2] == '.')))
        {
            ++ordered;
            continue;
        }
        struct Environment
        {
            const Folder &folder;
            const std::string &mine;
            bool Taken(const char *candidate) const
            {
                const std::string text = Upper(candidate);
                for (const auto &other : folder.taken)
                    if (other == text && other != mine)
                        return true;
                return false;
            }
        } environment{folders[Upper(parent)], stored};
        char alias[13] = {};
        Win98ShortAlias::Generate(environment, name.c_str(), alias);
        if (Upper(alias) == Upper(stored))
            ++same;
        else
        {
            ++differs;
            if (examples.size() < 10)
                examples.push_back(name + " -> " + alias + " instead of " + stored);
        }
    }
    for (const auto &text : examples)
        printf("SHORT ALIAS DIFFERS %s\n", text.c_str());
    printf("SHORT ALIAS %u compared, %u equal, %u different, %u with a creation-order tail\n",
           same + differs, same, differs, ordered);
    // At most one name may carry a character the OEM code page maps and this
    // port does not (the reference system installation had `3½ Floppy (A).lnk`);
    // everything else has to match.
    if (differs > 1 || !same)
    {
        fprintf(stderr, "the alias rule does not reproduce the imported directories\n");
        return 1;
    }
    printf("SHORT ALIAS PASS\n");
    return 0;
}
