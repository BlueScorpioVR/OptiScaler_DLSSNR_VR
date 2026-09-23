#include "../OptiScaler/shaders/dlssnr/NrHandleStateTable.h"

#include <cassert>
#include <cstdint>

namespace
{
struct FakeTemporalState
{
    std::uint64_t frame = 0;
    std::uint32_t historyMarker = 0;
};
}

int main()
{
    DlssNr::detail::HandleStateTable<FakeTemporalState> states;

    auto [left, leftCreated] = states.getOrCreate(101);
    auto [right, rightCreated] = states.getOrCreate(202);
    assert(leftCreated && rightCreated);
    assert(left != right);

    left->frame = 17;
    left->historyMarker = 0xAAAA;
    right->frame = 29;
    right->historyMarker = 0xBBBB;

    // Re-selecting an eye must return its existing history, not the other eye or a reset state.
    auto [leftAgain, leftCreatedAgain] = states.getOrCreate(101);
    auto [rightAgain, rightCreatedAgain] = states.getOrCreate(202);
    assert(!leftCreatedAgain && !rightCreatedAgain);
    assert(leftAgain == left && rightAgain == right);
    assert(leftAgain->frame == 17 && leftAgain->historyMarker == 0xAAAA);
    assert(rightAgain->frame == 29 && rightAgain->historyMarker == 0xBBBB);

    // Growing the map must not invalidate selected state addresses.
    for (unsigned int handle = 1000; handle < 1400; ++handle)
        states.getOrCreate(handle);
    assert(states.find(101)->second.get() == left);
    assert(states.find(202)->second.get() == right);

    // Releasing one source must not disturb the surviving eye's temporal state.
    states.erase(states.find(101));
    assert(states.find(101) == states.end());
    assert(states.find(202)->second.get() == right);
    assert(right->frame == 29 && right->historyMarker == 0xBBBB);

    return 0;
}
