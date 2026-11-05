/* SPDX-License-Identifier: GPL-2.0-or-later
 * Own x86 executable exercises native COMCTL32 with reentrant x86 callbacks. */
typedef unsigned U;
#define API __declspec(dllimport) __stdcall
API void ExitProcess(U);
API void Sleep(U);
API U LocalAlloc(U, U);
API U LocalFree(U);
API U GetLastError(void);
API U GetModuleHandleA(const char *);
API U LoadImageA(U, U, U, U, U, U);
API U DestroyIcon(U);
API U ImageList_Create(U, U, U, U, U);
API U ImageList_Destroy(U);
API U ImageList_ReplaceIcon(U, int, U);
API U ImageList_GetImageCount(U);
API U ImageList_SetBkColor(U, U);
API U FormatMessageA(U, const char *, U, U, char *, U, U *);
API U DSA_Create(U, U);
API U DSA_Destroy(U);
API U DSA_InsertItem(U, int, void *);
API U DSA_GetItem(U, int, void *);
API U DSA_DeleteItem(U, int);
API U DPA_Create(U);
API U DPA_CreateEx(U, U);
API U DPA_Destroy(U);
API U DPA_InsertPtr(U, int, U);
API U DPA_GetPtr(U, int);
API U DPA_Sort(U, int(__stdcall *)(U, U, U), U);
API U DPA_Search(U, U, int, int(__stdcall *)(U, U, U), U, U);
API U DPA_Clone(U, U);
API U Str_GetPtrA(const char *, char *, int);
API U Str_SetPtrA(char **, const char *);
API U Str_GetPtrW(const unsigned short *, unsigned short *, int);
API U Str_SetPtrW(unsigned short **, const unsigned short *);
__declspec(dllimport) int __cdecl wsprintfA(char *, const char *, ...);
API int wvsprintfA(char *, const char *, U *);
static U comparisons;
void run_thread_crt_test(void);
void run_thread_crt_family_test(void);
void run_crt_data_test(void);
void run_kernel_path_family_test(void);
void run_dc_map_test(void);
void run_time_family_test(void);
void run_thread_control_test(void);
int run_environment_family_test(void);
void run_shell_icons_test(void);
int run_image_resource_test(void);
void run_dib_api_test(void);
void run_dib_rle_test(void);
int run_shell_registry_test(void);
int run_shell_registration_test(void);
void run_dib_stretch_test(void);
void run_pen_line_test(void);
int run_menu_family_test(void);
int run_class_family_test(void);
int run_common_init_test(void);
int run_toolbar_family_test(void);
void run_cbt_hooks_test(void);
void run_scroll_family_test(void);
void run_dc_information_test(void);
void run_keyboard_layout_test(void);
void run_window_parent_test(void);
void run_user32_imports_test(void);
int run_toolbar_layout_test(void);
void run_font_query_test(void);
void run_mouse_capture_test(void);
void run_font_width_test(void);
void run_message_metadata_test(void);
void run_font_charset_test(void);
void run_crt_multibyte_test(void);
void run_caret_test(void);
void run_window_hit_test(void);
void run_character_type_test(void);
void run_user_characters_test(void);
void run_font_create_test(void);
void run_gdi_pixel_test(void);
void run_device_color_test(void);
void run_toolbar_paint_test(void);
void run_toolbar_bitmap_test(void);
void run_combobox_test(void);
void run_gdi_shapes_test(void);
void run_gdi_arcs_test(void);
void run_edit_notify_test(void);
void run_pointer_probe_test(void);
void run_region_family_test(void);
int run_clipboard_test(void);
void run_tree_test(U instance);
void run_list_test(U instance);
void run_registry_test(void);
void run_file_test(void);
void run_heap_test(void);
void run_wstrings_test(void);
void run_gdi_test(void);
void run_parameters_test(void);
void run_paint_test(U instance);
void run_timer_test(void);
void run_find_test(U instance);
void run_nls_test(void);
void run_environment_test(void);
void run_string_helpers_test(void);
void run_listbox_test(U);
void run_shell_test(U);
void run_windows_test(U);
void run_atoms_test(void);
void run_properties_test(U);
void run_keys_test(U);
void run_activation_test(U);
void run_focus_test(U);
void run_input_focus_test(U);
void run_crt_control_test(void);
void run_critical_test(void);
void run_mutex_test(void);
void run_virtual_test(void);
void run_user_hooks_test(U);
void run_crt_locale_test(void);
void run_crt_args_test(void);
void run_nonclient_test(U);
void run_mapping_test(void);
void run_semaphore_test(void);
void run_default_locale_test(void);
void run_system_locale_test(void);
void run_bitmap_test(void);
void run_crt_locale_heap_test(void);
void run_interlocked_test(void);
void run_version_test(void);
void run_crt_exit_test(void);
void finish_crt_exit_test(void);
void run_crt_memory_test(void);
static void check(int ok, U code) {
  if (!ok)
    ExitProcess(code);
}
static int equal(const char *a, const char *b) {
  while (*a && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}
static int __stdcall compare(U a, U b, U direction) {
  ++comparisons;
  U memory = LocalAlloc(64, 64);
  check(memory != 0, 50);
  for (U i = 0; i < 16; ++i)
    check(!((U *)memory)[i], 51);
  Sleep(0);
  check(!LocalFree(memory), 52);
  return ((int)a - (int)b) * (int)direction;
}
void mainCRTStartup(void) {
  run_crt_locale_heap_test();
  run_crt_data_test();
  run_keys_test(GetModuleHandleA(0));
  run_atoms_test();
  run_properties_test(GetModuleHandleA(0));
  run_shell_test(GetModuleHandleA(0));
  run_windows_test(GetModuleHandleA(0));
  run_activation_test(GetModuleHandleA(0));
  run_focus_test(GetModuleHandleA(0));
  run_input_focus_test(GetModuleHandleA(0));
  run_crt_control_test();
  run_critical_test();
  run_mutex_test();
  run_thread_crt_test();
  run_thread_crt_family_test();
  run_thread_control_test();
  { int result = run_environment_family_test(); if (result) ExitProcess(result); }
  { int result = run_shell_registry_test(); if (result) ExitProcess(result); }
  run_dib_api_test();
  run_dib_rle_test();
  run_dib_stretch_test();
  run_pen_line_test();
  { int result = run_menu_family_test(); if (result) ExitProcess(result); }
  { int result = run_class_family_test(); if (result) ExitProcess(result); }
  { int result = run_common_init_test(); if (result) ExitProcess(result); }
  { int result = run_toolbar_family_test(); if (result) ExitProcess(result); }
  run_cbt_hooks_test();
  run_scroll_family_test();
  run_dc_information_test();
  run_keyboard_layout_test();
  run_window_parent_test();
  run_user32_imports_test();
  { int result = run_toolbar_layout_test(); if (result) ExitProcess(result); }
  run_font_query_test();
  run_mouse_capture_test();
  run_font_width_test();
  run_message_metadata_test();
  run_font_charset_test();
  run_crt_multibyte_test();
  run_caret_test();
  run_window_hit_test();
  run_character_type_test();
  run_user_characters_test();
  run_font_create_test();
  run_gdi_pixel_test();
  run_device_color_test();
  run_toolbar_paint_test();
  run_toolbar_bitmap_test();
  run_combobox_test();
  run_gdi_shapes_test();
  run_gdi_arcs_test();
  run_edit_notify_test();
  { int result = run_image_resource_test(); if (result) ExitProcess(result); }
  run_shell_icons_test();
  { int result = run_shell_registration_test(); if (result) ExitProcess(result); }
  run_semaphore_test();
  run_mapping_test();
  run_default_locale_test();
  run_system_locale_test();
  run_virtual_test();
  run_pointer_probe_test();
  run_user_hooks_test(GetModuleHandleA(0));
  run_crt_locale_test();
  run_crt_args_test();
  run_nonclient_test(GetModuleHandleA(0));
  run_interlocked_test();
  run_version_test();
  run_crt_memory_test();
  run_crt_exit_test();
  run_paint_test(GetModuleHandleA(0));
  run_parameters_test();
  run_string_helpers_test();
  run_gdi_test();
  run_dc_map_test();
  run_bitmap_test();
  run_heap_test();
  run_wstrings_test();
  U array = DSA_Create(12, 3), item[3] = {98, 11, 2026}, copy[3] = {};
  check(array, 1);
  check(DSA_InsertItem(array, 0, item) == 0, 2);
  item[0] = 9;
  check(DSA_InsertItem(array, 0, item) == 0, 3);
  check(DSA_GetItem(array, 1, copy) && copy[0] == 98 && copy[2] == 2026, 4);
  check(DSA_DeleteItem(array, 0) && DSA_GetItem(array, 0, copy) &&
            copy[0] == 98,
        5);
  check(DSA_Destroy(array), 6);
  U dpa = DPA_CreateEx(1, 0);
  check(dpa, 7);
  for (U i = 0; i < 40; ++i)
    check(DPA_InsertPtr(dpa, 0x7fffffff, (39 - i) % 11) == i, 8);
  check(DPA_Sort(dpa, compare, 1), 9);
  for (U i = 1; i < 40; ++i)
    check(DPA_GetPtr(dpa, i - 1) <= DPA_GetPtr(dpa, i), 10);
  check(DPA_Search(dpa, 0, 0, compare, 1, 1) == 0, 11);
  check(DPA_Search(dpa, 20, 0, compare, 1, 3) == 40, 12);
  U clone = DPA_Clone(dpa, 0);
  check(clone, 13);
  check(DPA_Destroy(dpa), 14);
  check(DPA_GetPtr(clone, 39) == 10, 15);
  check(DPA_Destroy(clone), 16);
  check(comparisons > 100, 17);
  char output[128], *owned = 0;
  check(Str_SetPtrA(&owned, "Original strings") && owned, 70);
  check(Str_GetPtrA(owned, output, 9) == 9 && equal(output, "Original"), 71);
  check(Str_SetPtrA(&owned, owned + 9) && equal(owned, "strings"), 72);
  check(Str_SetPtrA(&owned, 0) && !owned, 73);
  unsigned short wide[] = {0x3a9, 0x41, 0}, wide_output[4], *wide_owned = 0;
  check(Str_SetPtrW(&wide_owned, wide) && Str_GetPtrW(wide_owned, wide_output, 2) == 2 &&
            wide_output[0] == 0x3a9 && !wide_output[1],
        74);
  check(Str_SetPtrW(&wide_owned, 0) && !wide_owned, 75);
  check(wsprintfA(output, "%s=%#08x/%d", "Win98", 98, -12) == 20 && equal(output, "Win98=0x00000062/-12"),
        76);
  U printf_args[] = {7};
  check(wvsprintfA(output, "%04d", printf_args) == 4 && equal(output, "0007"), 77);
  U args[] = {(U) "Windows", 98, 0xabc};
  check(FormatMessageA(0x2400, "%1 %2!u!: %3!04X!%n%%", 0, 0, output, 128,
                       args) == 19,
        20);
  check(equal(output, "Windows 98: 0ABC\r\n%"), 21);
  char *allocated = 0;
  check(FormatMessageA(0x500, "regedit.hlp", 0, 0, (char *)&allocated, 64, 0) ==
            11,
        22);
  check(equal(allocated, "regedit.hlp") && !LocalFree((U)allocated), 23);
  check(!FormatMessageA(0x400, "1234", 0, 0, output, 4, 0) &&
            GetLastError() == 122,
        24);
  check(FormatMessageA(0x600, "%1!d! literal%0ignored", 0, 0, output, 128, 0) ==
            13,
        25);
  check(equal(output, "%1!d! literal"), 26);
  /* ROUTE.EXE's rows: a width on a string insert pads on the left and never
     cuts. */
  U string_args[] = {(U) "abc", (U) "wide"};
  check(FormatMessageA(0x2400, "[%1!6s!|%2!2s!]", 0, 0, output, 128, string_args) == 13 &&
            equal(output, "[   abc|wide]"),
        27);
  U number_args[] = {42, 7};
  check(FormatMessageA(0x2400, "[%1!-5u!|%2!3u!]", 0, 0, output, 128, number_args) == 11 &&
            equal(output, "[42   |  7]"),
        28);
  U instance = GetModuleHandleA(0),
    icon = LoadImageA(instance, 23, 1, 16, 16, 0);
  check(icon, 30);
  U shared = LoadImageA(instance, 23, 1, 16, 16, 0x8000);
  check(shared && shared != icon, 31);
  check(LoadImageA(instance, 23, 1, 32, 32, 0x8000) == shared, 32);
  U list = ImageList_Create(16, 16, 0x21, 4, 1);
  check(list && ImageList_GetImageCount(list) == 0, 33);
  check(ImageList_ReplaceIcon(list, -1, icon) == 0 &&
            ImageList_GetImageCount(list) == 1,
        34);
  check(DestroyIcon(icon) && !DestroyIcon(icon), 35);
  check(ImageList_ReplaceIcon(list, 0, shared) == 0 &&
            ImageList_GetImageCount(list) == 1,
        36);
  check(ImageList_SetBkColor(list, 0xffffff) == 0xffffffff &&
            ImageList_SetBkColor(list, 0) == 0xffffff,
        37);
  check(ImageList_Destroy(list) && !ImageList_Destroy(list), 38);
  check(DestroyIcon(shared) &&
            LoadImageA(instance, 23, 1, 16, 16, 0x8000) == shared,
        39);
  run_tree_test(instance);
  run_list_test(instance);
  run_listbox_test(instance);
  run_registry_test();
  run_file_test();
  run_kernel_path_family_test();
  run_time_family_test();
  run_region_family_test();
  { int result = run_clipboard_test(); if (result) ExitProcess(result); }
  run_timer_test();
  run_find_test(instance);
  run_nls_test();
  run_environment_test();
  finish_crt_exit_test();
}
