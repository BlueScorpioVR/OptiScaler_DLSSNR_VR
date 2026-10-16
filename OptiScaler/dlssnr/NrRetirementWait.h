#pragma once
#include "NrSubmissionLedger.h"

namespace DlssNr
{
enum class NrRetirementResult { Retired, DeviceLost, Stalled };

// The watchdog is a failure boundary, not a per-frame quality-selection budget.
// Call only for submitted work, without holding the submission mutex.
template<class ReadFence, class Clock, class Pause>
NrRetirementResult WaitForNrRetirement(std::uint64_t required, ReadFence readFence,
                                     Clock clock, Pause pause)
{
    const auto started = clock();
    for (;;)
    {
        const auto completed = readFence();
        if (completed == ~std::uint64_t { 0 }) return NrRetirementResult::DeviceLost;
        if (NrFenceRetired(completed, required)) return NrRetirementResult::Retired;
        if (clock() - started >= 2000) return NrRetirementResult::Stalled;
        pause();
    }
}
}
