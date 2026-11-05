// SPDX-License-Identifier: GPL-2.0-or-later
#include <filesystem>
#include <fstream>
#include <string>
#include <set>
#include <system_error>
#include <cstdio>
#include <windows.h>
namespace fs=std::filesystem;
static unsigned checks,failures;
static void Check(bool ok,const char *name){++checks;if(!ok){++failures;std::printf("FAIL %s\n",name);}}
int main()
{
    try {
        const fs::path base="C:\\FSFIX3", old=fs::current_path();std::error_code ec;
        fs::remove_all(base,ec);ec.clear();
        Check(fs::create_directories(base/"one"/"two"),"nested create");
        Check(!fs::create_directories(base/"one"),"existing create");
        Check(fs::is_directory(base/"one"/"two"),"directory status");
        Check(!fs::exists(base/"missing",ec)&&!ec,"missing exists clears error");
        const fs::path file=base/"one"/L"gr\u00fc\u00dfe.txt";
        {std::ofstream out(file,std::ios::binary);out<<"abc\n123";Check(out.good(),"ofstream unicode ACP path");}
        Check(fs::file_size(file)==7,"file size");
        Check(fs::is_regular_file(file),"regular status");
        Check(fs::equivalent(file,file.parent_path()/"."/file.filename()),"equivalent file aliases");
        Check(fs::equivalent(base/"one",base/"one"/"."),"equivalent directories");
        Check(!fs::equivalent(base/"one",base/"one"/"two"),"different directories");
        Check(fs::hard_link_count(file)==1,"link count");
        auto write=fs::last_write_time(file);fs::last_write_time(file,write);
        Check(fs::last_write_time(file)==write,"file time roundtrip");
        Check(fs::copy_file(file,base/"copy"),"copy create");
        Check(fs::file_size(base/"copy")==7,"copy size");
        Check(!fs::copy_file(file,base/"copy",fs::copy_options::skip_existing),"copy skip");
        Check(!fs::copy_file(file,base/"copy",ec)&&ec==std::errc::file_exists,"copy existing error");ec.clear();
        Check(fs::copy_file(file,base/"copy",fs::copy_options::overwrite_existing),"copy overwrite");
        {std::ifstream in(base/"copy",std::ios::binary);std::string content((std::istreambuf_iterator<char>(in)),{});Check(content=="abc\n123","copy bytes");}
        fs::resize_file(base/"copy",3);Check(fs::file_size(base/"copy")==3,"truncate");
        fs::rename(base/"copy",base/"renamed");Check(!fs::exists(base/"copy")&&fs::exists(base/"renamed"),"rename");
        fs::rename(file,base/"renamed",ec);
        Check(bool(ec)&&fs::file_size(file)==7&&fs::file_size(base/"renamed")==3,"Win98 rename preserves existing target");ec.clear();
        for(unsigned n=0;n<128;++n){fs::copy_file(file,base/"temporary");fs::remove(base/"temporary");}
        Check(true,"copy closes adopted descriptors");
        (void)fs::status(base/L"\u4e00",ec);Check(bool(ec),"unrepresentable ACP path rejected");ec.clear();
        fs::permissions(base/"renamed",fs::perms::owner_read,fs::perm_options::replace);
        Check((fs::status(base/"renamed").permissions()&fs::perms::owner_write)==fs::perms::none,"readonly permissions");
        fs::permissions(base/"renamed",fs::perms::owner_write,fs::perm_options::add);
        Check((fs::status(base/"renamed").permissions()&fs::perms::owner_write)!=fs::perms::none,"writable permissions");
        std::set<std::string> names;
        for(auto &e:fs::directory_iterator(base)){names.insert(e.path().filename().string());Check(e.exists(),"entry exists");}
        Check(names==std::set<std::string>{"one","renamed"},"directory contents");
        unsigned all=0;for(auto &e:fs::recursive_directory_iterator(base)){Check(e.exists(),"recursive entry");++all;}
        Check(all==4,"recursive count");
        fs::copy(base/"one",base/"tree",fs::copy_options::recursive);
        Check(fs::exists(base/"tree"/"two")&&fs::file_size(base/"tree"/file.filename())==7,"recursive copy");
        fs::current_path(base);Check(fs::equivalent(fs::current_path(),base),"current path");
        Check(fs::equivalent(fs::canonical("one/../one"),base/"one"),"canonical");
        Check(fs::weakly_canonical("one/../absent")==base/"absent","weak canonical");
        fs::current_path(old);
        auto space=fs::space(file);Check(space.capacity>0&&space.available<=space.capacity,"space from file");
        Check(fs::is_directory(fs::temp_directory_path()),"temp directory");
        fs::create_symlink(file,base/"link",ec);Check(bool(ec)&&!fs::exists(base/"link"),"Win98 symlink unsupported");ec.clear();
        fs::create_hard_link(file,base/"link",ec);Check(bool(ec)&&!fs::exists(base/"link"),"Win98 hardlink unsupported");ec.clear();
        bool threw=false;try{(void)fs::file_size(base/"absent");}catch(const fs::filesystem_error &e){threw=e.code()==std::errc::no_such_file_or_directory;}
        Check(threw,"filesystem exception");
        Check(fs::remove_all(base)==8,"remove all count");
        Check(!fs::exists(base),"removed tree");
    } catch(const std::exception &e){++failures;std::printf("EXCEPTION %s\n",e.what());}
    std::printf("FILESYSTEM checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
