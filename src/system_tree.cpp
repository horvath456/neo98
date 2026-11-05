// SPDX-License-Identifier: GPL-2.0-or-later
#include "system_tree.h"
namespace SystemTree
{
namespace
{
Backend *mounted = nullptr;
}
void Mount(Backend *backend) { mounted = backend; }
Backend *Mounted() { return mounted; }
} // namespace SystemTree
