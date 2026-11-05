// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>

// Optional inclusive wall times. Nested stages overlap; do not add them.
namespace RenderProfile
{
enum Stage { GlCall, GlDraw, GlQueue, GlFlush, GpuDraw, GpuBindings,
             GpuTextures, GpuReadback, GpuWait, GlPresent, NativeLdt, NativeRegisters, NativeResume,
             UserGeometry, UserVisible, UserFrame, UserRender, UserBeginPaint, UserDrawing, Count };
inline bool Enabled()
{
    static const bool enabled = std::getenv("NEO98_RENDER_PROFILE") != nullptr;
    return enabled;
}
struct Totals
{
    std::atomic<unsigned long long> ns[Count] = {}, calls[Count] = {}, samples[Count] = {};
    std::atomic<unsigned long long> slot_ns[1024] = {}, slot_calls[1024] = {};
    ~Totals() { Report(); }
    void Report() const
    {
        static const char *names[] = {"gl_call", "gl_draw", "gl_queue", "gl_flush", "gpu_draw",
            "gpu_bindings", "gpu_textures", "gpu_readback", "gpu_wait", "gl_present",
            "native_ldt", "native_registers", "native_resume", "user_geometry", "user_visible",
            "user_frame", "user_render", "user_begin_paint", "user_drawing"};
        for (unsigned i = 0; i < Count; ++i)
            if (calls[i]) std::fprintf(stderr, "RENDER PROFILE stage=%s calls=%llu samples=%llu estimated_ms=%.3f\n",
                names[i], calls[i].load(), samples[i].load(), ns[i].load() / 1000000.0);
        for (unsigned i = 0; i < 1024; ++i)
            if (slot_calls[i]) std::fprintf(stderr, "GL PROFILE slot=%u calls=%llu estimated_ms=%.3f\n",
                i, slot_calls[i].load(), slot_ns[i].load() / 1000000.0);
    }
};
inline Totals &Counters() { static Totals totals; return totals; }
class Scope
{
    Stage stage;
    bool enabled;
    unsigned weight = 1;
    unsigned slot;
    std::chrono::steady_clock::time_point start;
  public:
    explicit Scope(Stage stage, unsigned slot = ~0u) : stage(stage), enabled(Enabled()), slot(slot)
    {
        if (!enabled) return;
        Counters().calls[stage].fetch_add(1, std::memory_order_relaxed);
        if (slot < 1024) Counters().slot_calls[slot].fetch_add(1, std::memory_order_relaxed);
        // Fine-grained timers otherwise dominate small texture lookups.
        if ((stage <= GpuTextures || (stage >= NativeLdt && stage <= NativeResume) ||
             stage == UserVisible || stage == UserFrame || stage == UserDrawing) && stage != GlFlush &&
            !(stage == GlCall && (slot == 216 || slot == 217)))
        {
            static thread_local unsigned random = 0x9e3779b9u;
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            if (random & 63) { enabled = false; return; }
            weight = 64;
        }
        start = std::chrono::steady_clock::now();
    }
    ~Scope()
    {
        if (!enabled) return;
        const auto elapsed = std::chrono::steady_clock::now() - start;
        auto &c = Counters();
        c.ns[stage].fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() * weight,
                             std::memory_order_relaxed);
        c.samples[stage].fetch_add(1, std::memory_order_relaxed);
        if (slot < 1024) c.slot_ns[slot].fetch_add(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count() * weight,
            std::memory_order_relaxed);
    }
};
}
