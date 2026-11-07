// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "host_savestate.h"
#include "host_state_window_linux.h"
#include "host_machine_state.h"
#include "linux_session.h"
#include <filesystem>
namespace LinuxHost
{
class States
{
    std::string folder, restart_id, then_load, kind, name;
    std::vector<unsigned char> pending;
    HostSaveState::Entry loaded;
    std::vector<std::pair<std::string, std::string>> machine;
    bool saving = false, loading = false;
    std::chrono::steady_clock::time_point since, started = std::chrono::steady_clock::now();
    std::function<void(bool, const char *)> pause;
    std::function<void(const std::string &)> restart;
    X86::Application *app = nullptr;
    const HostMachineState::Supplier *job_supplier = nullptr, *load_supplier = nullptr;
    bool rebuilt = false, refused = false, settling = false;
    std::chrono::steady_clock::time_point settle_until;
    void Refuse(const std::string &error)
    {
        refused = true;
        printf("NEO98 HOST state load id=%s failed: %s\n", loaded.id.c_str(), error.c_str()); fflush(stdout);
        setenv("NEO98_LINUX_STATE_ERROR", (error + " The game restarts using the state's files. Its previous state is under Before loading.").c_str(), 1);
    }
    const HostMachineState::Supplier *Supplier() const
    {
        return app && app->DosVmProcess() ? (X86::Application::DosSettingsStateOpen() ? HostMachineState::DosUi() : HostMachineState::Dos())
                                          : HostMachineState::Windows();
    }
    static std::string Revision() { const char *v = getenv("NEO98_REVISION"); return v && *v ? v : "linux-x64"; }
    HostSaveState::Context Context() const
    {
        HostSaveState::Context context;
        HostSaveState::ContextOf(folder, Revision(), context);
        context.engines = HostMachineState::Engines();
        if (app) { context.game = Supplier()->kind; std::string reason; Supplier()->Ready(reason, &context.machine); }
        return context;
    }
    // `title`: what did not happen ("Not saved."), as the macOS host says
    // it; its line is the one the tests read.
    void Notice(const std::string &title, const std::string &message, bool failed = false)
    {
        printf("NEO98 HOST state %s: %s%s%s\n", failed ? "refused" : "notice", title.c_str(), message.empty() ? "" : " ", message.c_str());
        fflush(stdout);
        if (failed) HostMenu::Notice(title.c_str(), message.c_str());
    }
    bool Find(const std::string &id, HostSaveState::Entry &wanted, std::string &error, bool verify = false) const
    {
        for (const auto &state : HostSaveState::List(Context())) if (state.id == id) wanted = state;
        if (wanted.id.empty()) error = "This state is missing.";
        else if (!wanted.usable) error = wanted.reason;
        else if (verify && wanted.game != "off")
        {
            unsigned long long size = 0;
            if (HostSaveState::FileSum(HostSaveState::MachineFile(wanted.folder), size) != wanted.machine_sum || size != wanted.machine_size)
                error = "The state is damaged: its machine data (Maschine.bin) has changed since it was saved.";
        }
        return error.empty();
    }
    bool Write(const std::string &type, const std::string &title, const std::vector<unsigned char> *bytes,
               std::string &id, std::string &error)
    {
        if (app) { Files::System().Flush(); Registry::Flush(); }
        HostSaveState::Request request;
        request.kind = type; request.name = title; request.revision = Revision();
        const auto *supplier = job_supplier ? job_supplier : Supplier();
        request.game = bytes ? supplier->kind : "off"; request.engine = bytes ? supplier->Engine() : "";
        request.machine = bytes; request.machine_info = machine;
        if (bytes && app && app->DosVmProcess()) request.view.emplace_back("dos-vollbild", app->DosVmFullscreenNow() ? "1" : "0");
        for (char letter = 'C'; letter <= 'Z'; ++letter)
            if (auto *layer = HostVolume::Layer(letter))
                for (const auto &file : layer->BelowFiles())
                    if (bytes && (supplier == HostMachineState::Windows() || file.foreign)) request.files.push_back({letter, file.path, file.size, file.seconds, file.nanoseconds});
        std::vector<unsigned> picture; unsigned width = 0, height = 0;
        if (bytes)
        {
            if (!supplier->Picture || !supplier->Picture(picture,width,height))
            {
                int w = 0, h = 0; User::PixelsSize(w,h);
                if (w > 0 && h > 0 && User::Pixels()) { width = w; height = h;
                    const auto *pixels = reinterpret_cast<const unsigned *>(User::Pixels()); picture.assign(pixels,pixels+size_t(w)*h); }
            }
            if (!picture.empty()) { request.picture = picture.data(); request.picture_width = width; request.picture_height = height; }
        }
        const auto before = std::chrono::steady_clock::now();
        const bool okay = HostSaveState::Save(folder, request, id, error);
        // The line of the macOS host (tools/neo98_host.cpp), which the tests read.
        printf("NEO98 HOST state %s id=%s kind=%s game=%s machine=%zu bytes files=%zu picture=%ux%u in %lld ms%s%s\n",
               okay ? "saved" : "save failed", id.empty() ? "-" : id.c_str(), type.c_str(), request.game.c_str(), bytes ? bytes->size() : size_t(0),
               request.files.size(), width, height,
               (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count(),
               okay ? "" : ": ", okay ? "" : error.c_str());
        fflush(stdout);
        return okay;
    }
    HostStateWindow::Model Model() const
    {
        HostStateWindow::Model model;
        model.game = std::filesystem::path(folder).filename().string(); model.save_reason = Blocked(); model.can_save = model.save_reason.empty();
        for (const auto &state : HostSaveState::List(Context()))
        {
            HostStateWindow::Row row;
            row.id = state.id; row.name = state.name; row.parent = state.parent; row.kind = state.kind; row.game = state.game;
            row.time = state.time; row.usable = state.usable; row.damaged = state.damaged; row.current = state.current;
            row.reason = state.reason; row.revision = state.revision;
            if (state.has_picture) row.picture = HostSaveState::PictureFile(state.folder);
            model.rows.push_back(std::move(row));
        }
        return model;
    }
  public:
    explicit States(std::string game) : folder(std::move(game)) {}
    ~States() { HostStateWindow::Close(); }
    bool Prepare(const std::string &id)
    {
        if (folder.empty())
        {
            if (!id.empty()) fprintf(stderr, "States are available only in a game machine.\n");
            return id.empty();
        }
        HostSaveState::Recover(folder);
        if (id.empty()) return true;
        std::string error;
        const auto reject = [&] {
            pending.clear();
            printf("NEO98 HOST state load id=%s refused: %s\n", id.c_str(), error.c_str());
            fflush(stdout);
            Notice("Could not load the state", error, true);
            return true;
        };
        if (!Find(id, loaded, error, true)) return reject();
        if (loaded.game != "off")
        {
            std::string bytes;
            if (!HostSaveState::ReadFile(HostSaveState::MachineFile(loaded.folder), bytes)) return false;
            pending.assign(bytes.begin(), bytes.end());
        }
        const char *given = getenv("NEO98_STATE_KEPT");
        const std::string kept = given ? given : "";
        const bool already_kept = !kept.empty() && HostSaveState::IsDirectory(HostSaveState::StateFolder(folder, kept));
        unsetenv("NEO98_STATE_KEPT");
        std::string made = kept;
        if ((!already_kept && !Write("auto", "", nullptr, made, error)) || !HostSaveState::Restore(folder, id, error))
        { fprintf(stderr, "State not loaded: %s\n", error.c_str()); return false; }
        printf("NEO98 HOST state load id=%s game=%s files restored layers=%s machine=%zu bytes before=%s\n",
               id.c_str(), loaded.game.c_str(), loaded.layers.empty() ? "-" : loaded.layers.c_str(), pending.size(), kept.empty() ? "-" : kept.c_str());
        fflush(stdout);
        HostSaveState::Prune(folder, id); return true;
    }
    int BeforeStart(X86::Application &process, const unsigned short *clock)
    {
        app = &process; HostWindowsState::first = &process;
        if (pending.empty()) return 0;
        for (char letter = 'C'; letter <= 'Z'; ++letter)
            if (auto *layer = HostVolume::Layer(letter))
            {
                std::vector<HostVolume::Layered::BelowFile> known;
                for (const auto &file : loaded.files) if (file.drive == letter)
                    known.push_back({file.path, file.size, file.seconds, file.nanoseconds, loaded.game == "windows", loaded.game != "windows"});
                layer->NoticeBelowFiles(known);
            }
        load_supplier = HostMachineState::ByKind(loaded.game);
        if (!load_supplier) { Refuse("This Neo98 build does not recognize the state's program type (" + loaded.game + ")."); return -1; }
        load_supplier->Load(std::move(pending)); loading = true; since = std::chrono::steady_clock::now();
        bool failed = false; std::string error;
        if (load_supplier != HostMachineState::Dos())
        {
            const bool made = load_supplier == HostMachineState::DosUi()
                ? HostDosUiState::LoadNow(process,clock,failed,error) : HostWindowsState::LoadNow(process,clock,failed,error);
            if (failed || !made) { Refuse(error); return -1; }
            rebuilt = true; return 1;
        }
        return 0;
    }
    bool Rebuilt() const { return rebuilt; }
    // The state was put in place and its machine could not be built: the
    // files are those of the state, the machine is half one and half the
    // other. The host starts over on the files, as on macOS.
    bool LoadRefused() const { return refused; }
    void Attach(X86::Application &process, std::function<void(bool, const char *)> stop, std::function<void(const std::string &)> again)
    {
        app = &process; pause = std::move(stop); restart = std::move(again);
        HostWindowsState::first = app;
        HostWindowsState::pause = [this](bool value, const char *why) { pause(value, why); };
        HostWindowsState::paused = []() { return X86::Application::MachinePaused(); };
        if (const char *error = getenv("NEO98_LINUX_STATE_ERROR"); error && *error)
        { const std::string message = error; unsetenv("NEO98_LINUX_STATE_ERROR"); Notice("Could not load the state", message, true); }
    }
    bool NeedsRun() const { return saving || loading || settling; }
    // After a load the machine stands until the person goes on (a key, a
    // click, Pause), as on macOS; `stand` is the host's switch for it.
    std::function<void()> stand;
    bool HoldUi() const { return HostDosUiState::HoldUi(); }
    bool ListOpen() const { return HostStateWindow::IsOpen(); }
    std::string Blocked(std::vector<std::pair<std::string, std::string>> *info = nullptr) const
    {
        if (folder.empty()) return "States are available only in a game machine.";
        if (saving || loading) return "A state is being saved or loaded.";
        if (!app) return "The machine is still starting.";
        if (app->DosVmProcess() && X86::Application::LiveChildren()) return "Another program is running alongside the DOS game.";
        std::string reason;
        if (!Supplier()->Ready(reason, info)) return reason;
        return {};
    }
    std::string Last() const
    {
        std::string found; long long newest = -1;
        if (folder.empty()) return found;
        for (const auto &state : HostSaveState::List(Context()))
            if (state.usable && state.kind != "auto")
            { if (state.current) return state.id; if (state.time >= newest) { found = state.id; newest = state.time; } }
        return found;
    }
    // `as`: the kind of the state when the caller names it (a manual state
    // without a name); else quick without a title, manual with one.
    void Save(const std::string &title, const std::string &next = {}, const std::string &as = {})
    {
        machine.clear(); const auto reason = Blocked(&machine);
        if (!reason.empty()) { Notice(next.empty() ? "Not saved." : "Not loaded.", reason, true); return; }
        kind = !next.empty() ? "auto" : !as.empty() ? as : title.empty() ? "quick" : "manual";
        name = title; then_load = next; job_supplier = Supplier(); saving = true;
        since = std::chrono::steady_clock::now(); job_supplier->Save();
    }
    void Load(std::string id)
    {
        if (saving || loading || folder.empty()) return;
        if (id.empty()) id = Last();
        std::string error; HostSaveState::Entry wanted;
        if (!Find(id, wanted, error)) { Notice("„" + wanted.name + "” could not be loaded.", error, true); return; }
        HostStateWindow::Close();
        if (Blocked().empty()) { Save("", id); return; }
        pause(true, "a state is loaded"); std::string kept;
        if (!Write("auto", "", nullptr, kept, error)) { pause(false, "the load was given up"); Notice("Not loaded.", "The current state could not be saved, so nothing was loaded. " + error, true); return; }
        setenv("NEO98_STATE_KEPT", kept.c_str(), 1); restart(id);
    }
    // The id of the newest state of that name, for a script that cannot
    // know the id; the name itself when there is none.
    std::string IdByName(const std::string &wanted) const
    {
        std::string id;
        if (!folder.empty())
            for (const auto &state : HostSaveState::List(Context())) if (state.name == wanted) id = state.id;
        return id.empty() ? wanted : id;
    }
    void Open() { if (!folder.empty() && !saving && !loading) HostStateWindow::Open(Model()); }
    void Poll()
    {
        HostStateWindow::Request request;
        while (HostStateWindow::Next(request))
        {
            std::string error;
            switch (request.kind)
            {
            case HostStateWindow::Request::Close: HostStateWindow::Close(); break;
            case HostStateWindow::Request::Save: Save(request.name); break;
            case HostStateWindow::Request::Load: Load(request.id); break;
            case HostStateWindow::Request::Delete: HostSaveState::Delete(folder, request.id, error); break;
            case HostStateWindow::Request::Rename: HostSaveState::Rename(folder, request.id, request.name, error); break;
            case HostStateWindow::Request::Keep: HostSaveState::Keep(folder, request.id, error); break;
            }
            if (!error.empty()) Notice("States", error, true);
            if (ListOpen()) HostStateWindow::Update(Model(), request.id);
        }
        if (settling)
        {
            // The machine ran a moment on the state so its windows paint;
            // now it stands on it.
            if (std::chrono::steady_clock::now() < settle_until) return;
            settling = false;
            pause(true, "a state was loaded");
            load_supplier->Release(); load_supplier = nullptr;
            printf("NEO98 HOST state loaded id=%s name=\"%s\" after %.2f s; the machine stands\n", loaded.id.c_str(), loaded.name.c_str(),
                   std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count());
            fflush(stdout);
            if (stand) stand();
            else pause(false, "the state is loaded");
            return;
        }
        if (!saving && !loading) return;
        std::vector<unsigned char> bytes; std::string error;
        const auto *supplier = saving ? job_supplier : load_supplier;
        int answer = supplier->Poll(saving ? &bytes : nullptr, error);
        if (!answer && std::chrono::steady_clock::now() - since > std::chrono::seconds(45))
        { answer = -1; error = "The machine did not provide its state in time."; }
        if (!answer) return;
        pause(true, saving ? "a state is saved" : "a state was loaded");
        const bool was_saving = saving;
        bool okay = answer > 0;
        std::string kept;
        if (okay && saving) okay = Write(kind, name, &bytes, kept, error);
        if (okay && loading)
        {
            std::vector<std::pair<std::string, std::string>> now; std::string reason;
            if (supplier == HostMachineState::Dos() && !supplier->Ready(reason, &now)) { okay = false; error = reason; }
            for (const auto &needed : loaded.machine)
                for (const auto &have : now) if (have.first == needed.first && have.second != needed.second)
                { okay = false; error = "The DOS settings differ from the saved state."; }
            if (okay)
            {
                std::vector<unsigned> picture; unsigned width = 0, height = 0;
                // The picture and the full screen of a DOS game; a state with
                // the DOS UI (dos-ui) brought its windows along, as on macOS.
                const bool dos = supplier != HostMachineState::DosUi() && app->DosVmProcess();
                if (dos && supplier->Picture && supplier->Picture(picture,width,height)) app->DosVmShowPicture(picture.data(), width, height);
                for (const auto &view : loaded.view) if (view.first == "dos-vollbild" && dos) app->DosVmWantFullscreen(view.second == "1");
                // 200 ms of running for the window to paint, then it stands.
                loading = false; settling = true;
                settle_until = std::chrono::steady_clock::now() + std::chrono::milliseconds(200);
                pause(false, "a state settles");
                return;
            }
        }
        if (!okay && loading)
        {
            printf("NEO98 HOST state load id=%s failed: %s\n", loaded.id.c_str(), error.c_str()); fflush(stdout);
            // A refused load may have replaced only part of the core. Start
            // a fresh process on the restored disks and retain the backup.
            saving = loading = false;
            setenv("NEO98_LINUX_STATE_ERROR", (error + " The game restarts using the state's files. Its previous state is under Before loading.").c_str(), 1);
            restart(""); return;
        }
        saving = loading = false; supplier->Release(); job_supplier = load_supplier = nullptr;
        pause(false, !okay ? "a state was not taken" : was_saving ? "the state is saved" : "the state is loaded");
        if (okay && was_saving && then_load.empty()) Notice("State saved", name);
        if (!okay) { Notice(then_load.empty() ? "Not saved." : "Not loaded.", (then_load.empty() ? "" : "The current state could not be saved, so nothing was loaded. ") + error, true); then_load.clear(); }
        else if (was_saving && !then_load.empty()) {
            printf("NEO98 HOST state load id=%s: the machine starts over with it, before=%s\n", then_load.c_str(), kept.c_str());
            fflush(stdout);
            setenv("NEO98_STATE_KEPT", kept.c_str(), 1); restart(then_load); then_load.clear(); }
        if (ListOpen()) HostStateWindow::Update(Model(), kept);
    }
};
}
