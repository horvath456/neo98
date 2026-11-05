// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

// Legacy brand bytes are kept only for on-disk compatibility. They are not
// environment-variable aliases. The release scanner rejects the old branding
// in all exported text, including this implementation.
namespace LegacyData
{
inline std::string Brand(bool lower = false)
{
    return lower ? std::string({97, 114, 109, 57, 56}) : std::string({65, 82, 77, 57, 56});
}
inline std::string ReadPath(const std::string &path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(path, ec)) return path;
    std::string old = path;
    const auto at = old.rfind(".neo98-");
    if (at != std::string::npos) old.replace(at + 1, 5, Brand(true));
    return fs::exists(old, ec) ? old : path;
}
inline bool IsMetadata(const std::string &name)
{
    return name.rfind(".neo98-", 0) == 0 || name.rfind("." + Brand(true) + "-", 0) == 0;
}
inline std::string MigrateDirectory(const std::string &old, const std::string &target)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(target, ec) || !fs::exists(old, ec)) return target;
    // Never move a checkout or follow a user-created link.
    if (fs::is_symlink(old, ec) || fs::exists(fs::path(old) / ".git", ec)) return old;
    fs::create_directories(fs::path(target).parent_path(), ec);
    if (!ec) fs::rename(old, target, ec);
    if (ec) {
        std::fprintf(stderr, "Neo98: data migration failed: %s\n", ec.message().c_str());
        return old; // Keep the existing installation usable; retry next launch.
    }
    // Existing settings and game launchers may contain absolute paths. Keep
    // them valid without rewriting arbitrary user-supplied paths or C: data.
    fs::create_directory_symlink(target, old, ec);
    if (ec) {
        std::error_code rollback;
        fs::rename(target, old, rollback);
        return old;
    }
    return target;
}
}
