#include "xrfg/luke_counter_probe.hpp"
#include <windows.h>

namespace xrfg {
namespace {
bool read_self(std::uintptr_t address, void* out, std::size_t bytes) noexcept {
    SIZE_T got{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void*>(address),
        out, bytes, &got) && got == bytes;
}
struct Binding {
    std::uintptr_t base{};
    bool supported{};
    Binding() noexcept {
        const auto module = GetModuleHandleW(L"dxgi.dll");
        // No module loading, hooks, patches, or added module references.
        if (!module || !GetProcAddress(module, "RealVRVersion") ||
            !GetProcAddress(module, "g_rvrShared")) return;
        base = reinterpret_cast<std::uintptr_t>(module);
        supported = luke_layout_matches(base, read_self);
    }
};
}
std::optional<LukeRenderIdentity> query_luke_render_identity() noexcept {
    static const Binding binding;
    if (!binding.supported || GetModuleHandleW(L"dxgi.dll") != reinterpret_cast<HMODULE>(binding.base)) return {};
    return read_luke_identity(binding.base, read_self);
}
} // namespace xrfg
