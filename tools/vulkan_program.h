// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <string>
#include <vector>
namespace HostVulkan::Program
{
bool Available();
bool Translate(const unsigned *tokens, unsigned count, bool pixel, bool wbuffer, std::string &source,
               std::string &error, bool flat = false);
bool Compile(const std::string &source, bool pixel, std::vector<unsigned> &words, std::string &error);
} // namespace HostVulkan::Program
