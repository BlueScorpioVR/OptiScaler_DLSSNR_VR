#pragma once

#include <array>
#include <cstdint>
#include <nvsdk_ngx_defs.h>

namespace DlssNr
{
struct RenderExtent
{
    unsigned int width;
    unsigned int height;
};

// A pair is authoritative only when both components are present and nonzero.
inline RenderExtent BeforeSrExtent(RenderExtent subrect, bool hasSubrect, RenderExtent generic,
                                   RenderExtent allocation)
{
    if (hasSubrect && subrect.width && subrect.height)
        return subrect;
    if (generic.width && generic.height)
        return generic;
    return allocation;
}

inline bool FitsBeforeSr(RenderExtent active, uint64_t allocationWidth, unsigned int allocationHeight)
{
    return active.width && active.height && active.width <= allocationWidth && active.height <= allocationHeight;
}

// The signed NR model needs the driver's capability block, which can alias the game's SR block.
// Restore every non-NR key written by the unchanged V23029 forwarder before invoking SR, including
// first-use and failure returns. Absent optional keys regain NGX's neutral/default value; dimensions
// regain the captured render/output contract. NR-only keys deliberately remain in the block.
template <typename Parameters, typename FloatSetter> class SharedContractRestore
{
    struct UIntValue { const char* key; unsigned int value; };
    struct FloatValue { const char* key; float value; };
    Parameters* params_;
    FloatSetter setFloat_;
    std::array<UIntValue, 9> integers_;
    std::array<FloatValue, 2> floats_;

public:
    SharedContractRestore(Parameters* params, FloatSetter setFloat, RenderExtent render, RenderExtent output)
        : params_(params), setFloat_(setFloat), integers_ {{
            {"Width", render.width}, {"Height", render.height},
            {"OutWidth", output.width}, {"OutHeight", output.height},
            {"DLSS.Feature.Create.Flags", 0},
            {"DLSS.Indicator.Invert.X.Axis", 0}, {"DLSS.Indicator.Invert.Y.Axis", 0},
            {"CreationNodeMask", 1}, {"VisibilityNodeMask", 1}
        }}, floats_ {{{"DLSS.Pre.Exposure", 1.0f}, {"DLSS.Exposure.Scale", 1.0f}}}
    {
        for (auto& entry : integers_)
        {
            auto value = entry.value;
            if (params_->Get(entry.key, &value) == NVSDK_NGX_Result_Success)
                entry.value = value;
        }
        for (auto& entry : floats_)
        {
            auto value = entry.value;
            if (params_->Get(entry.key, &value) == NVSDK_NGX_Result_Success)
                entry.value = value;
        }
    }
    SharedContractRestore(const SharedContractRestore&) = delete;
    SharedContractRestore& operator=(const SharedContractRestore&) = delete;
    ~SharedContractRestore()
    {
        for (const auto& entry : integers_)
            params_->Set(entry.key, entry.value);
        for (const auto& entry : floats_)
            setFloat_(params_, entry.key, entry.value);
    }
};
}
