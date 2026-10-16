#include "../OptiScaler/dlssnr/NrRetirementWait.h"
#include <cassert>

int main()
{
    using DlssNr::NrRetirementResult;
    // Queue pressure beyond both old budgets must still produce an NR frame.
    for (const std::uint64_t latency : { 0ull, 8ull, 16ull, 17ull, 40ull, 100ull })
    {
        std::uint64_t now = 0;
        const auto result = DlssNr::WaitForNrRetirement(7,
            [&] { return now >= latency ? 7ull : 6ull; },
            [&] { return now; }, [&] { ++now; });
        assert(result == NrRetirementResult::Retired);
        assert(now == latency);
    }
    std::uint64_t now = 0;
    volatile std::uint64_t stuckFence = 6;
    auto result = DlssNr::WaitForNrRetirement(7,
        [&] { return stuckFence; }, [&] { return now; }, [&] { ++now; });
    assert(result == NrRetirementResult::Stalled && now == 2000);
    now = 0;
    result = DlssNr::WaitForNrRetirement(7,
        [&] { return now == 20 ? ~std::uint64_t { 0 } : 6; },
        [&] { return now; }, [&] { ++now; });
    assert(result == NrRetirementResult::DeviceLost && now == 20);
}
