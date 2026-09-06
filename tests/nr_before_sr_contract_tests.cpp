#include "../OptiScaler/dlssnr/NrBeforeSrContract.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>

struct Parameters
{
    std::unordered_map<std::string, unsigned int> integers;
    std::unordered_map<std::string, float> floats;
    NVSDK_NGX_Result Get(const char* key, unsigned int* value)
    {
        auto it = integers.find(key);
        if (it == integers.end()) return NVSDK_NGX_Result_FAIL_UnsupportedParameter;
        *value = it->second;
        return NVSDK_NGX_Result_Success;
    }
    NVSDK_NGX_Result Get(const char* key, float* value)
    {
        auto it = floats.find(key);
        if (it == floats.end()) return NVSDK_NGX_Result_FAIL_UnsupportedParameter;
        *value = it->second;
        return NVSDK_NGX_Result_Success;
    }
    void Set(const char* key, unsigned int value) { integers[key] = value; }
};

int main()
{
    using namespace DlssNr;
    auto extent = BeforeSrExtent({1536, 1632}, true, {3072, 3264}, {3072, 3264});
    assert(extent.width == 1536 && extent.height == 1632);
    extent = BeforeSrExtent({1536, 0}, true, {2048, 2176}, {3072, 3264});
    assert(extent.width == 2048 && extent.height == 2176);
    extent = BeforeSrExtent({1536, 1632}, false, {0, 2176}, {3072, 3264});
    assert(extent.width == 3072 && extent.height == 3264);
    assert(FitsBeforeSr({1536, 1632}, 3072, 3264));
    assert(!FitsBeforeSr({3072, 3264}, 1536, 1632));
    assert(!FitsBeforeSr({0, 1632}, 3072, 3264));
    assert(!FitsBeforeSr({1536, 0}, 3072, 3264));

    Parameters p;
    p.integers = {{"Width", 1536}, {"Height", 1632}, {"OutWidth", 3072}, {"OutHeight", 3264},
        {"DLSS.Feature.Create.Flags", 0x4b}, {"DLSS.Indicator.Invert.X.Axis", 1},
        {"DLSS.Indicator.Invert.Y.Axis", 1}, {"CreationNodeMask", 2}, {"VisibilityNodeMask", 2}};
    p.floats = {{"DLSS.Pre.Exposure", 0.125f}, {"DLSS.Exposure.Scale", 2.5f}};
    const auto originalInts = p.integers;
    const auto originalFloats = p.floats;
    auto setFloat = [](Parameters* params, const char* key, float value) { params->floats[key] = value; };
    auto invokeNr = [&](bool creationFrame) {
        SharedContractRestore restore(&p, setFloat, RenderExtent{1536, 1632}, RenderExtent{3072, 3264});
        for (auto& [key, value] : p.integers) value = 42;
        for (auto& [key, value] : p.floats) value = 1.0f;
        p.integers["DLSSNR.Width"] = 768;
        if (creationFrame) return;
    };
    // Alternating eyes, creation and normal returns, preserve all eleven shared SR values.
    invokeNr(true);
    p.integers.erase("DLSSNR.Width");
    assert(p.integers == originalInts && p.floats == originalFloats);
    invokeNr(false);
    p.integers.erase("DLSSNR.Width");
    assert(p.integers == originalInts && p.floats == originalFloats);

    Parameters empty;
    {
        SharedContractRestore restore(&empty, setFloat, RenderExtent{1536, 1632}, RenderExtent{3072, 3264});
        empty.integers["Width"] = 768;
        empty.floats["DLSS.Pre.Exposure"] = 9.0f;
    }
    assert(empty.integers["Width"] == 1536 && empty.integers["OutWidth"] == 3072);
    assert(empty.floats["DLSS.Pre.Exposure"] == 1.0f);
    std::puts("PASS: extent precedence/bounds and shared SR contract restoration (creation + evaluation)");
}
