// Copyright (c) 2026 Anthony Schemel
// SPDX-License-Identifier: MIT

/// @file audio_mix_monitor.cpp
/// @brief Phase 10 AX12 — per-bus producer-push analysis tap.
#include "audio/audio_mix_monitor.h"

namespace Vestige
{

namespace
{
/// @brief Sum `gain * s[i]` into @a acc, growing @a acc (zero-fill) to cover
///        @a n. The one primitive behind both `accumulateBusFrame` and the
///        monitor's per-frame accumulation, so their semantics can't drift.
void mixAddInto(std::vector<float>& acc, const float* s, std::size_t n, float gain)
{
    if (s == nullptr || n == 0)
    {
        return;
    }
    if (acc.size() < n)
    {
        acc.resize(n, 0.0f);
    }
    for (std::size_t i = 0; i < n; ++i)
    {
        acc[i] += gain * s[i];
    }
}
}  // namespace

void accumulateBusFrame(const std::vector<MixSubmission>& subs,
                        std::vector<float>& out)
{
    out.clear();
    for (const MixSubmission& sub : subs)
    {
        mixAddInto(out, sub.samples, sub.count, sub.gain);
    }
}

void AudioMixMonitor::setActive(bool active)
{
    m_active = active;
    if (!active)
    {
        // Clear pending (a frame's submissions never leak) and free the rings
        // (tab-closed zero-memory reclaim, §5).
        for (BusState& b : m_bus)
        {
            b.pending.clear();
            b.pending.shrink_to_fit();
            b.pendingUsed = false;
            b.content.clear();
            b.content.shrink_to_fit();
            b.hadRecentSignal = false;
        }
    }
}

void AudioMixMonitor::submit(AudioBus bus, const float* mono, std::size_t n,
                             float gain, int sampleRate)
{
    if (!m_active)
    {
        return;  // no-op when inactive (INV-6)
    }
    BusState& b = m_bus[index(bus)];
    mixAddInto(b.pending, mono, n, gain);
    b.pendingUsed = true;
    b.rate = sampleRate;  // most-recent submit's rate (§3.2)
}

void AudioMixMonitor::flushFrame()
{
    if (!m_active)
    {
        return;
    }
    for (BusState& b : m_bus)
    {
        b.hadRecentSignal = b.pendingUsed;
        if (b.pendingUsed)
        {
            b.content.insert(b.content.end(), b.pending.begin(), b.pending.end());

            // Trim to capacity: keep the newest WINDOW + HISTORY_SECONDS·rate
            // samples. rate > 0 here (a submit set it this frame).
            const std::size_t cap =
                kMixMonitorWindow +
                static_cast<std::size_t>(kMixMonitorHistorySeconds *
                                         static_cast<float>(b.rate));
            if (b.content.size() > cap)
            {
                b.content.erase(b.content.begin(),
                                b.content.begin() +
                                    static_cast<std::ptrdiff_t>(b.content.size() - cap));
            }
            b.pending.clear();
            b.pendingUsed = false;
        }
        // else: idle bus — nothing appended, ring holds its last trace (freeze).
    }
}

const std::vector<float>& AudioMixMonitor::ring(AudioBus bus) const
{
    return m_bus[index(bus)].content;
}

int AudioMixMonitor::rateHz(AudioBus bus) const
{
    return m_bus[index(bus)].rate;
}

bool AudioMixMonitor::hadRecentSignal(AudioBus bus) const
{
    return m_bus[index(bus)].hadRecentSignal;
}

} // namespace Vestige
