#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <utility>

namespace xrfg {

// External synchronization. T is a descriptor/reference, never image storage.
// Advancing the sequence changes one index; GPU resources are never moved.
template<class T>
class ThreeSlotBuffer {
public:
    struct Entry { T value; bool synthetic; };

    const std::optional<Entry>& slot(std::size_t logical) const noexcept {
        return slots_[(head_ + logical) % 3];
    }
    bool empty() const noexcept { return !slot(0) && !slot(1) && !slot(2); }
    bool can_accept_real() const noexcept {
        return !slot(1) && !slot(2) && (!slot(0) || !slot(0)->synthetic);
    }
    bool push_real(T value) {
        if (!can_accept_real()) return false;
        slots_[(head_ + 2) % 3].emplace(Entry{std::move(value), false});
        return true;
    }
    // Startup only: walk the first real to the head without displaying it.
    bool shift_empty() noexcept {
        if (slot(0) || empty()) return false;
        head_ = (head_ + 1) % 3;
        return true;
    }
    bool can_insert_synthetic() const noexcept {
        return !slot(0) && slot(1) && !slot(1)->synthetic && !slot(2);
    }
    bool push_synthetic(T value) {
        if (!can_insert_synthetic()) return false;
        slots_[head_].emplace(Entry{std::move(value), true});
        return true;
    }
    bool releasable(bool drain = false) const noexcept {
        return slot(0) && (slot(0)->synthetic || slot(2) || drain);
    }
    // Only after the director's xrEndFrame returns: its handle remains owned
    // by this entry until that point, including while downstream blocks.
    bool release_front(bool drain = false) noexcept {
        if (!releasable(drain)) return false;
        slots_[head_].reset();
        head_ = (head_ + 1) % 3;
        return true;
    }
    void clear() noexcept { slots_ = {}; head_ = 0; }

private:
    std::array<std::optional<Entry>, 3> slots_{};
    std::size_t head_{};
};

} // namespace xrfg
