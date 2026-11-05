// SPDX-License-Identifier: GPL-2.0-or-later
// SHELL32 ordinals 86, 87 and 88 connected to win98_shell_dragdrop.h.
// The thirteen OLE32 entries the original resolves are not native: NEO98
// resolves and calls them in whatever module the loader supplies for
// OLE32.DLL, which for the shell runs is the original file as guest code.
//
struct Application::DragDropEnvironment
{
    using U = unsigned;
    Application &app;
    // The original's LoadLibraryA, GetProcAddress and FreeLibrary calls run
    // through the same loader the guest's own calls use, including the
    // reference count and DllMain.
    U LoadLibrary(const char *name)
    {
        if (!app.LockLoader())
            return 0;
        U handle = 0;
        const unsigned index = app.LoadModule(name);
        if (index < Application::ModuleCapacity)
        {
            ++app.modules[index].references;
            if (app.InitializeModule(index))
                handle = app.modules[index].handle;
            else
                --app.modules[index].references;
        }
        app.UnlockLoader();
        return handle;
    }
    U Procedure(U module, const char *name)
    {
        if (!app.LockLoader())
            return 0;
        const unsigned index = app.ModuleIndex(module);
        const U address = index < Application::ModuleCapacity ? app.ModuleProcedure(index, name, 0) : 0;
        app.UnlockLoader();
        return address;
    }
    void FreeLibrary(U module)
    {
        //  passes the module cell unchecked, which is zero when the
        // count was unbalanced. The loader rejects that handle; so does this.
        if (!module || !app.LockLoader())
            return;
        const unsigned index = app.ModuleIndex(module);
        if (index && index < Application::ModuleCapacity)
        {
            if (app.modules[index].references)
                --app.modules[index].references;
            app.CollectModules();
        }
        app.UnlockLoader();
    }
    U Invoke(U procedure, const U *arguments, U count)
    {
        if (!procedure)
        {
            app.Fail("SHELL32 drag and drop needs an OLE32 entry this build does not provide");
            return Win98ShellDragDrop::Unexpected;
        }
        return app.Call(procedure, arguments, count);
    }
    //  builds SHELL32's own default IDropSource. That object is not
    // implemented, so a caller that relies on it stops the run instead of
    // receiving an invented interface pointer.
    U DefaultSource(U)
    {
        app.Fail("SHELL32 SHDoDragDrop without a drop source is not ported");
        return 0;
    }
    void ReleaseSource(U source)
    {
    // IUnknown::Release through the object's own vtable.
        const U release = app.Read32(app.Read32(source) + 8), arguments[] = {source};
        if (release)
            app.Call(release, arguments, 1);
    }
};
unsigned Application::ShellDragDropApi(unsigned ordinal, const unsigned *a)
{
    DragDropEnvironment e{*this};
    if (ordinal == 86)
        return Win98ShellDragDrop::Register(e, drag_drop, a[0], a[1]);
    if (ordinal == 87)
        return Win98ShellDragDrop::Revoke(e, drag_drop, a[0]);
    return Win98ShellDragDrop::Drag(e, drag_drop, a[0], a[1], a[2], a[3], a[4]);
}
