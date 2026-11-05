// SPDX-License-Identifier: GPL-2.0-or-later
// installed resources, cached PE, absent/malformed files, raster fonts.
#define private public
#include "../src/x86_app.h"
#undef private
#include "../src/gdi_font_catalog.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
static std::vector<unsigned char> Load(const char *name)
{
    FILE *file = fopen(name, "rb"); assert(file);
    fseek(file, 0, SEEK_END); const auto size = ftell(file); rewind(file);
    std::vector<unsigned char> data(size);
    assert(fread(data.data(), 1, size, file) == size); fclose(file);
    return data;
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    const auto dll = Load(argv[1]), font = Load(argv[2]);
    const char *path = "C:\\WINDOWS\\SYSTEM\\COMDLG32.DLL";
    Files::System().MakeDirectory("C:\\WINDOWS");
    Files::System().MakeDirectory("C:\\WINDOWS\\SYSTEM");
    {
        auto *app = new X86::Application(nullptr);
        unsigned size = 123;
        assert(!app->CommonResource(5, 1540, size) && size == 0);
        assert(!Files::System().Seed(path, dll.data(), dll.size()));
        // A failed load is remembered too; a new process gets a fresh attempt.
        assert(!app->CommonResource(5, 1540, size));
        delete app;
    }
    {
        auto *app = new X86::Application(nullptr);
        Pe32::Image original; assert(original.Parse(dll.data(), dll.size()));
        const unsigned resources[][2] = {{5,1540},{5,1541},{5,1547},{5,400},{6,65}};
        for (const auto &key : resources)
        {
            unsigned expected = 0, size = 0;
            const auto *a = original.Resource(key[0], key[1], &expected);
            const auto *b = app->CommonResource(key[0], key[1], size);
            assert(a && b && size == expected && !memcmp(a, b, size));
        }
        unsigned size = 0;
        const auto *first = app->CommonResource(5, 1540, size);
        auto *parsed = app->common_dialog_image;
        assert(!Files::System().Remove(path, false));
        for (unsigned i = 0; i < 20; ++i)
            assert(app->CommonResource(5, 1540, size) == first && app->common_dialog_image == parsed);
        delete app;
    }
    for (unsigned size : {1u, 64u, 512u})
    {
        std::vector<unsigned char> bad(size);
        assert(!Files::System().Seed(path, bad.data(), bad.size()));
        auto *app = new X86::Application(nullptr);
        unsigned length = 123;
        assert(!app->CommonResource(5, 1540, length) && length == 0);
        delete app;
        assert(!Files::System().Remove(path, false));
    }
    assert(DibText::FontCount() == 0 && !DibText::OriginalFont(0).data);
    assert(GdiFonts::InstallFontFile("SSERIFE.FON", font.data(), font.size()) == 6);
    assert(DibText::FontCount() == 6 && !DibText::OriginalFont(6).data);
    assert(DibText::OriginalSelfTest() == 0);
    GdiFonts::Reset();
    assert(!DibText::FontCount());
    puts("RUNTIME CONTENT PASS: resources, cache, absent/malformed files, six FNTs and original text vectors");
}
