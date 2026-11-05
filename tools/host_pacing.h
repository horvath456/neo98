// SPDX-License-Identifier: GPL-2.0-or-later
// Host-side pacing observer for how long the host event loop is kept
// away from its turn, who kept it, and how long the drawn pointer needs from
// an input event to the frame that shows it at the new place.
// This is measurement, not runtime: nothing here is Windows behaviour and
// nothing here belongs in src/. It observes the public state of the runtime
// (Application::ApiCalls, the always-on API ring,
// User::FrameSerial) from the host thread, between guest runs, and writes
// lines to stderr. Off unless the caller switches it on.
#pragma once
#include <chrono>
#include <cstdio>
#include <cstring>

namespace HostPacing
{
using Clock = std::chrono::steady_clock;
inline double MillisecondsBetween(Clock::time_point a, Clock::time_point b)
{
    return std::chrono::duration<double, std::milli>(b - a).count();
}

// A pause of the host event loop, with the process that ran through it.
struct Pause
{
    double milliseconds = 0;
    unsigned process = 0, thread = 0, calls = 0;
    char program[64] = {};
    char api[96] = {};
};

// Every API call the machine dispatched, this process and its children. The
// hang indicator of the host has to count them all: a child that answers the
// mouse leaves the root process without a single call for seconds, and the
// title then claimed a hang that was none.
inline unsigned TotalCalls(X86::Application &root)
{
    unsigned total = root.Progress();
    for (unsigned slot = 0; slot < X86::Application::ChildCapacity; ++slot)
    {
        bool done = false;
        if (auto *child = root.ChildApplication(slot, done); child && !done) total += child->Progress();
    }
    return total;
}

class Observer
{
  public:
    bool enabled = false;
    // A turn of the loop that takes longer than this is reported one by one.
    double report_ms = 40;
    // No call anywhere for this long counts as a stall of the whole machine.
    double stall_ms = 250;
    void Start()
    {
        run_start = last_turn = stall_since = Clock::now();
        started = true;
    }
    // The machine went on after a pause : the time it stood
    // is neither a long turn nor a stall.
    void Resumed()
    {
        last_turn = stall_since = Clock::now();
        stall_open = false;
    }
    // Called right before the host hands the machine to the guest.
    void BeginRun() { guest_start = Clock::now(); }
    // Called when the guest run of this turn is over. app is the root process.
    void EndRun(X86::Application &app)
    {
        if (!enabled || !started) return;
        const auto now = Clock::now();
        const double ms = MillisecondsBetween(guest_start, now);
        Note(app, ms, now);
    }
    // Every turn of the event loop. Catches pauses the guest run did not cause
    // (a crash report, a flush, the presentation itself).
    void Turn(X86::Application &app)
    {
        if (!enabled || !started) return;
        const auto now = Clock::now();
        const double ms = MillisecondsBetween(last_turn, now);
        last_turn = now;
        turns++;
        if (ms > turn_worst) { turn_worst = ms; }
        if (ms >= 1000) turn_over_1000++;
        if (ms >= 200) turn_over_200++;
        if (ms >= 100) turn_over_100++;
        if (ms >= 40) turn_over_40++;
        (void)app;
    }
    // An input event the guest has just been given. The pointer is drawn by
    // USER's composition, so the frame that shows it is the next one whose
    // serial has moved on.
    void Input()
    {
        if (!enabled || !started) return;
        if (pending) return; // one outstanding sample is enough
        pending = true;
        input_time = Clock::now();
        input_serial = User::FrameSerial();
    }
    // A presented frame. Resolves the outstanding input sample.
    void Frame()
    {
        if (!enabled || !started || !pending) return;
        if (User::FrameSerial() == input_serial) return;
        pending = false;
        const double ms = MillisecondsBetween(input_time, Clock::now());
        pointer_total += ms;
        pointer_samples++;
        if (ms > pointer_worst) pointer_worst = ms;
        if (ms >= 100) pointer_over_100++;
        if (pointer_slots < kSlots) pointer_ms[pointer_slots++] = ms;
    }
    // Anything the host itself does that may take long (a crash report).
    void HostWork(const char *what, double ms)
    {
        if (!enabled) return;
        fprintf(stderr, "HOSTPACE host what=%s ms=%.1f\n", what, ms);
    }
    // The other kind of pause: the scheduler keeps handing slices to threads it
    // considers runnable and not one of them dispatches a call. A machine in
    // which nothing is runnable is idle, not stalled, and is not counted. The
    // dump names every thread with its block reason and its deadline.
    void Stall(X86::Application &root)
    {
        if (!enabled || !started) return;
        if (!root.SystemRunnable()) { stall_since = Clock::now(); stall_open = false; return; }
        const auto now = Clock::now();
        const unsigned total = TotalCalls(root);
        if (total != stall_calls) { stall_calls = total; stall_since = now; stall_open = false; return; }
        const double ms = MillisecondsBetween(stall_since, now);
        if (ms < stall_ms) return;
        if (!stall_open)
        {
            stall_open = true;
            stalls++;
            fprintf(stderr, "HOSTPACE stall begin calls=%u runnable=%u\n", total,
                    root.SystemRunnable() ? 1u : 0u);
            Dump(root, "-");
            for (unsigned slot = 0; slot < X86::Application::ChildCapacity; ++slot)
            {
                bool done = false;
                if (auto *child = root.ChildApplication(slot, done); child && !done) Dump(*child, "child");
            }
        }
        if (ms > stall_worst) stall_worst = ms;
        stall_last = ms;
    }
    // Closes an open stall: called when the calls move on again.
    void StallEnd()
    {
        if (!stall_open) return;
        stall_open = false;
        fprintf(stderr, "HOSTPACE stall end ms=%.1f\n", stall_last);
    }
    double StallWorst() const { return stall_worst; }
    unsigned Stalls() const { return stalls; }
    unsigned Samples() const { return pointer_samples; }
    double PointerWorst() const { return pointer_worst; }
    double PointerAverage() const { return pointer_samples ? pointer_total / pointer_samples : 0; }
    double PointerMedian() const
    {
        if (!pointer_slots) return 0;
        double copy[kSlots];
        memcpy(copy, pointer_ms, pointer_slots * sizeof(double));
        for (unsigned i = 1; i < pointer_slots; ++i)
        {
            const double key = copy[i];
            unsigned j = i;
            while (j && copy[j - 1] > key) { copy[j] = copy[j - 1]; --j; }
            copy[j] = key;
        }
        return copy[pointer_slots / 2];
    }
    double PauseWorst() const { return worst.milliseconds; }
    const Pause &Worst() const { return worst; }
    double TurnWorst() const { return turn_worst; }
    unsigned Pauses() const { return pauses; }
    // One block of numbers, for scheduler diagnostics.
    void Report(const char *label) const
    {
        const double seconds = MillisecondsBetween(run_start, Clock::now()) / 1000;
        printf("HOSTPACE %s seconds=%.1f turns=%u loop_worst_ms=%.1f "
               "loop_over_40=%u loop_over_100=%u loop_over_200=%u loop_over_1000=%u\n",
               label, seconds, turns, turn_worst, turn_over_40, turn_over_100, turn_over_200,
               turn_over_1000);
        printf("HOSTPACE %s guest_worst_ms=%.1f guest_pauses=%u worst_program=%s worst_process=%u "
               "worst_thread=%u worst_api=%s worst_calls=%u\n",
               label, worst.milliseconds, pauses, worst.program[0] ? worst.program : "-", worst.process,
               worst.thread, worst.api[0] ? worst.api : "-", worst.calls);
        printf("HOSTPACE %s pointer_samples=%u pointer_worst_ms=%.1f pointer_median_ms=%.1f "
               "pointer_mean_ms=%.1f pointer_over_100=%u\n",
               label, pointer_samples, pointer_worst, PointerMedian(), PointerAverage(),
               pointer_over_100);
        printf("HOSTPACE %s stalls=%u stall_worst_ms=%.1f\n", label, stalls, stall_worst);
        fflush(stdout);
    }

  private:
    static void Dump(X86::Application &app, const char *kind)
    {
        const auto *call = app.api_ring.Recent(0);
        fprintf(stderr, "HOSTPACE stall process kind=%s name=%s pid=%u state=%u calls=%u api=%s!%s\n", kind,
                app.program_name[0] ? app.program_name : "?", app.ProcessId(), (unsigned)app.Status(),
                app.ApiCalls(), call && call->dll ? call->dll : "-", call && call->name ? call->name : "-");
        const unsigned long long now = app.Now();
        for (unsigned i = 0; i < 16; ++i)
        {
            const auto &t = app.threads[i];
            if (!t.fiber || t.done) continue;
            long long left = t.deadline ? (long long)(t.deadline - now) / 1000 : -1;
            // For a thread that waits for messages: which of the conditions in
            // Application::Runnable keeps it awake.
            unsigned queued = 0, timer = 0, track = 0;
            if (t.block == 1) {
                const unsigned saved_pid = User::CurrentProcess(), saved_tid = User::CurrentThread();
                User::BindTask(app.ProcessId(), t.id);
                User::Message message = {};
                queued = User::PeekMessage(&message, t.message_window, t.message_minimum,
                                           t.message_maximum, false) ? 1u : 0u;
                timer = app.TimerReady(i) ? 1u : 0u;
                track = User::FrameTrackDue() ? 1u : 0u;
                User::BindTask(saved_pid, saved_tid);
            }
            fprintf(stderr, "HOSTPACE stall thread pid=%u index=%u id=%u block=%u wait=%08x "
                            "deadline_ms=%lld suspended=%u waiting=%u eip=%08x runnable=%u "
                            "input=%u queued=%u timer=%u track=%u dirty=%u\n",
                    app.ProcessId(), i, t.id, t.block, t.wait_handle, left, t.suspended, t.waiting ? 1u : 0u,
                    app.ThreadEip(i), app.Runnable(i) ? 1u : 0u, app.input_count, queued, timer, track,
                    User::Dirty() ? 1u : 0u);
        }
    }
    // Attribution of one guest run. The process that was current when the run
    // ended and the last API call of the ring are what the host can see from
    // outside without touching the scheduler.
    void Note(X86::Application &root, double ms, Clock::time_point now)
    {
        (void)now;
        if (ms < report_ms) return;
        pauses++;
        Pause p;
        p.milliseconds = ms;
        X86::Application *busy = Busiest(root, p.calls);
        if (busy)
        {
            p.process = busy->ProcessId();
            snprintf(p.program, sizeof p.program, "%s", busy->program_name);
            p.thread = busy->threads[busy->current_thread < 16 ? busy->current_thread : 0].id;
            if (const auto *call = busy->api_ring.Recent(0))
                snprintf(p.api, sizeof p.api, "%s!%s", call->dll ? call->dll : "?",
                         call->name ? call->name : "?");
        }
        if (ms > worst.milliseconds) worst = p;
        fprintf(stderr, "HOSTPACE pause ms=%.1f program=%s process=%u thread=%u calls=%u api=%s\n",
                p.milliseconds, p.program[0] ? p.program : "-", p.process, p.thread, p.calls,
                p.api[0] ? p.api : "-");
    }
    // Which process dispatched the most API calls during this run. The counters
    // are per Application, so the difference names the process that used the
    // turn even when the scheduler moved between them.
    X86::Application *Busiest(X86::Application &root, unsigned &calls)
    {
        X86::Application *best = nullptr;
        calls = 0;
        const unsigned now_root = root.ApiCalls();
        unsigned delta = now_root - last_root_calls;
        last_root_calls = now_root;
        if (delta) { best = &root; calls = delta; }
        for (unsigned slot = 0; slot < X86::Application::ChildCapacity; ++slot)
        {
            bool done = false;
            auto *child = root.ChildApplication(slot, done);
            const unsigned value = child && !done ? child->ApiCalls() : 0;
            const unsigned before = last_child_calls[slot];
            last_child_calls[slot] = value;
            if (!child || done || value < before) continue;
            delta = value - before;
            if (delta > calls) { best = child; calls = delta; }
        }
        // No call at all: the turn was spent inside one call or inside the
        // host. Name the process the scheduler was last bound to.
        if (!best) best = &root;
        return best;
    }
    static const unsigned kSlots = 4096;
    bool started = false, pending = false;
    Clock::time_point run_start, last_turn, guest_start, input_time;
    unsigned input_serial = 0;
    unsigned turns = 0, turn_over_40 = 0, turn_over_100 = 0, turn_over_200 = 0, turn_over_1000 = 0;
    double turn_worst = 0;
    unsigned pauses = 0;
    Pause worst;
    unsigned last_root_calls = 0, last_child_calls[16] = {};
    unsigned stall_calls = 0, stalls = 0;
    bool stall_open = false;
    Clock::time_point stall_since;
    double stall_worst = 0, stall_last = 0;
    unsigned pointer_samples = 0, pointer_over_100 = 0, pointer_slots = 0;
    double pointer_total = 0, pointer_worst = 0;
    double pointer_ms[kSlots] = {};
};
} // namespace HostPacing
