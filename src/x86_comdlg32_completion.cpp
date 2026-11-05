// SPDX-License-Identifier: GPL-2.0-or-later
// The COMDLG32 exports that were still missing from the export contract
// on 23 September 2026. COMDLG32 runs as the
// original by the DLL rule (src/dll_routes.h); this native form only answers
// when a test moves the DLL back with --native-module.
// GetFileTitleW is the Unicode error gate of Windows 98:
// SetLastError(ERROR_CALL_NOT_IMPLEMENTED) and a zero word. The other eight
// missing exports are parts of the original's dialogs (the procedures and
// subclasses of ChooseColor, ChooseFont and the file dialog, LoadAlterBitmap,
// PageSetupDlgA); they stay missing in the native form, see
//
#include "x86_app.h"
namespace X86
{
unsigned Application::Comdlg32CompletionApi(ApiName name, const unsigned *)
{
    switch (name.id)
    {
    API_CASE("GetFileTitleW")
        last_error = 120;
        return 0;
    default:
        break;
    }
    Fail(name);
    return 0;
}
} // namespace X86
