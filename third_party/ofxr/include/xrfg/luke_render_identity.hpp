#pragma once
#include <cstdint>
#include <optional>

namespace xrfg {
struct LukeRenderIdentity {
    std::uint64_t object{}, render{};
    bool operator==(const LukeRenderIdentity&) const = default;
};
// Read-only, pinned Crimson runtime. Independent of diagnostic logging.
std::optional<LukeRenderIdentity> query_luke_render_identity() noexcept;
} // namespace xrfg
