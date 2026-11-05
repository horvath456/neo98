// SPDX-License-Identifier: GPL-2.0-or-later
#include "win98_metrics.h"
struct MetricsEnvironment final : Win98Metrics::Environment
{
    unsigned short *words;
    unsigned version, loaded, *cache, *counts;
    MetricsEnvironment(unsigned short *w, unsigned v, unsigned l, unsigned *c, unsigned *n)
        : words(w), version(v), loaded(l), cache(c), counts(n)
    {
    }
    unsigned short Word(unsigned offset) const override { return words[offset / 2]; }
    void SetWord(unsigned offset, unsigned short value) override { words[offset / 2] = value; }
    unsigned short ExpectedVersion() const override
    {
        ++counts[0];
        return version;
    }
    unsigned PenWindows() override
    {
        ++counts[1];
        if (!*cache)
        {
            ++counts[2];
            *cache = loaded;
        }
        return *cache;
    }
};
extern "C" unsigned metrics_query(unsigned short *words, int index, unsigned version, unsigned loaded,
                                  unsigned *cache, unsigned *counts)
{
    MetricsEnvironment e(words, version, loaded, cache, counts);
    return Win98Metrics::Query(e, index);
}
extern "C" void metrics_apply(unsigned short *words, const Win98Metrics::NonClient *settings,
                              unsigned recompute)
{
    MetricsEnvironment e(words, 0, 0, nullptr, nullptr);
    if (recompute)
        Win98Metrics::Recompute(e);
    else
        Win98Metrics::ApplyNonClientMetrics(e, *settings);
}
