// SPDX-License-Identifier: GPL-2.0-or-later
#include "host_settings.h"
#include <cassert>
#include <fstream>
#include <filesystem>

int main()
{
    namespace fs = std::filesystem;
    std::string scratch = fs::temp_directory_path().string() + "/neo98-migration-XXXXXX";
    assert(mkdtemp(scratch.data()));
    setenv("HOME", scratch.c_str(), 1);
    unsetenv("NEO98_SETTINGS_DIR"); unsetenv("NEO98_LOG_DIR");
    unsetenv("XDG_CONFIG_HOME"); unsetenv("XDG_STATE_HOME");
    const std::string home = scratch, brand = LegacyData::Brand();
#ifdef __APPLE__
    const std::string old = home + "/Library/Application Support/" + brand;
    const std::string logs = home + "/Library/Logs/" + brand;
#else
    const std::string old = home + "/.config/" + LegacyData::Brand(true);
    const std::string logs = home + "/.local/state/" + LegacyData::Brand(true);
#endif
    const std::string machine = home + "/" + brand;
    fs::create_directories(machine + "/C"); fs::create_directories(old + "/games/example");
    fs::create_directories(logs + "/reports");
    std::ofstream(machine + "/C/." + LegacyData::Brand(true) + "-setup.json") << "{}";
    std::ofstream(machine + "/C.registry") << "private journal";
    std::ofstream(old + "/settings.ini") << "machine = " << machine << "\nscale = 2\nonline = 0\n";
    std::ofstream(old + "/games/example/settings.ini") << "game.name = Test\n";
    std::ofstream(logs + "/reports/example.txt") << "private report";
    assert(HostSettings::DefaultMachine() == home + "/Neo98");
    const auto dir = HostSettings::Directory(), log = HostSettings::LogDirectory();
    HostSettings::Settings settings;
    assert(HostSettings::Load(settings) && settings.scale == 2 && !settings.online);
    assert(HostSettings::Ready(settings.machine));
    assert(fs::exists(settings.machine + "/C.registry"));
    assert(fs::exists(dir + "/games/example/settings.ini"));
    assert(fs::exists(log + "/reports/example.txt"));
    assert(fs::is_symlink(old) && fs::is_symlink(logs));
    assert(HostSettings::Directory() == dir && HostSettings::LogDirectory() == log);
    // Existing new settings win, even when a separate legacy directory remains.
    fs::remove(old); fs::create_directories(old);
    std::ofstream(old + "/settings.ini") << "scale = 4\n";
    assert(HostSettings::Load(settings) && settings.scale == 2);
    // An override never migrates default user paths.
    setenv("NEO98_SETTINGS_DIR", (home + "/override").c_str(), 1);
    assert(HostSettings::Directory() == home + "/override");
    assert(fs::exists(old + "/settings.ini"));
    // New machine folders use English; existing print folders retain their jobs.
    assert(HostSettings::PrintDirectory(machine) == machine + "/Print Output");
    fs::create_directories(machine + "/Druckausgabe");
    std::ofstream(machine + "/Druckausgabe/job.ps") << "existing job";
    assert(HostSettings::PrintDirectory(machine) == machine + "/Druckausgabe");
    assert(fs::exists(HostSettings::PrintDirectory(machine) + "/job.ps"));
    fs::create_directories(machine + "/Print Output");
    assert(HostSettings::MachinePrintDirectory(machine) == machine + "/Druckausgabe");
    const auto direct = home + "/direct";
    fs::create_directories(direct);
    std::ofstream(direct + "/.neo98-setup.json") << "{}";
    assert(HostSettings::PrintDirectory(direct) == direct + ".print-output");
    fs::remove_all(home);
    puts("PASS migration, settings, machine, journal, game machines, reports, repeat and overrides");
}
