#include "pch.h"
#include "DlssNr_Dx12.h"

#include "precompile/DlssNr_Shader.h"
#include <diagnostics/DredMarkers.h>

#include <cstdio>
#include <cstring>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct DlssNr_Dx12::DispatchBinding
{
    FrameDescriptorHeap heap;
    ComPtr<ID3D12Resource> constantBuffer;
    std::array<ComPtr<ID3D12Resource>, kSrvCount> srvs;
    std::array<ComPtr<ID3D12Resource>, kUavCount> uavs;
    DlssNrConstants constants {};

    [[nodiscard]] bool Matches(const DlssNrConstants& other,
                               ID3D12Resource* const (&otherSrvs)[kSrvCount],
                               ID3D12Resource* const (&otherUavs)[kUavCount]) const noexcept
    {
        const bool constantsMatch =
            constants.Mode == other.Mode && constants.WhitePoint == other.WhitePoint &&
            constants.Width == other.Width && constants.Height == other.Height &&
            constants.TransferStrength == other.TransferStrength &&
            constants.ColourStrength == other.ColourStrength &&
            constants.DebugView == other.DebugView && constants.MaxRatio == other.MaxRatio &&
            constants.Passthrough == other.Passthrough && constants.MvScaleX == other.MvScaleX &&
            constants.MvScaleY == other.MvScaleY && constants.GuideWidth == other.GuideWidth &&
            constants.GuideHeight == other.GuideHeight &&
            constants.CompareMode == other.CompareMode &&
            constants.CompareSplit == other.CompareSplit &&
            constants.CompareZoom == other.CompareZoom &&
            constants.CompareSwap == other.CompareSwap;

        if (!constantsMatch)
            return false;

        for (uint32_t i = 0; i < kSrvCount; ++i)
        {
            if (srvs[i].Get() != otherSrvs[i])
                return false;
        }

        for (uint32_t i = 0; i < kUavCount; ++i)
        {
            if (uavs[i].Get() != otherUavs[i])
                return false;
        }

        return true;
    }
};

DlssNr_Dx12::DlssNr_Dx12(std::string InName, ID3D12Device* InDevice)
    : Shader_Dx12(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    // Five inputs, two outputs, one constant buffer, and a clamped linear sampler.
    //
    // The sampler exists because the model may be run below full resolution, in which case its answer
    // has to be read back at a different size from the frame it is being transferred onto.
    D3D12_STATIC_SAMPLER_DESC sampler {};
    sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxLOD = D3D12_FLOAT32_MAX;
    sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    if (!SetupRootSignature(InDevice, kSrvCount, kUavCount, 1, 0, 0, 1, &sampler))
    {
        LOG_ERROR("[{0}] Failed to setup root signature", _name);
        return;
    }

    // Precompiled, with no source fallback. The shader used to be compiled at runtime from a string,
    // which would have meant no shader at all for anyone leaving UsePrecompiledShaders at its
    // default.
    if (!CreateComputePipeline(InDevice, &_pipelineState, DlssNr_cso, sizeof(DlssNr_cso), nullptr))
    {
        LOG_ERROR("[{0}] Failed to create the compute pipeline", _name);
        return;
    }

    _init = true;
}

bool DlssNr_Dx12::Dispatch(ID3D12GraphicsCommandList* InCmdList, const DlssNrConstants& InConstants,
                                  ID3D12Resource* InSource, ID3D12Resource* InModel,
                                  ID3D12Resource* InOriginal, ID3D12Resource* InMotion,
                                  ID3D12Resource* InPrevEdit, ID3D12Resource* OutTarget,
                                  ID3D12Resource* OutKeep)
{
    if (!_init || InCmdList == nullptr || _device == nullptr || InSource == nullptr || OutTarget == nullptr)
        return false;

    // Every slot in the table gets a view, whether the mode reads it or not. An unbound descriptor is
    // not an empty read; it is a read from nothing, and the source stands in wherever a mode has
    // nothing of its own to put there.
    ID3D12Resource* const srvs[kSrvCount] = {
        InSource,
        InModel != nullptr ? InModel : InSource,
        InOriginal != nullptr ? InOriginal : InSource,
        InMotion != nullptr ? InMotion : InSource,
        InPrevEdit != nullptr ? InPrevEdit : InSource,
    };

    ID3D12Resource* const uavs[kUavCount] = {
        OutTarget,
        OutKeep != nullptr ? OutKeep : OutTarget,
    };

    std::lock_guard<std::mutex> bindingLock(_bindingsMutex);
    DispatchBinding* binding = nullptr;
    size_t bindingIndex = 0;

    for (size_t i = 0; i < _bindings.size(); ++i)
    {
        if (_bindings[i]->Matches(InConstants, srvs, uavs))
        {
            binding = _bindings[i].get();
            bindingIndex = i;
            break;
        }
    }

    if (binding == nullptr)
    {
        auto candidate = std::make_unique<DispatchBinding>();
        candidate->constants = InConstants;

        for (uint32_t i = 0; i < kSrvCount; ++i)
            candidate->srvs[i] = srvs[i];
        for (uint32_t i = 0; i < kUavCount; ++i)
            candidate->uavs[i] = uavs[i];

        D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(DlssNrConstants));
        auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        const HRESULT resourceResult = _device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
            IID_PPV_ARGS(candidate->constantBuffer.ReleaseAndGetAddressOf()));
        if (FAILED(resourceResult))
        {
            LOG_ERROR("[{0}] immutable constants allocation failed {1:x}", _name,
                      (unsigned int) resourceResult);
            if (resourceResult == DXGI_ERROR_DEVICE_REMOVED)
                Util::GetDeviceRemovedReason(_device);
            return false;
        }

        if (!InitHeaps(_device, &candidate->heap, 1))
        {
            LOG_ERROR("[{0}] immutable descriptor heap allocation failed", _name);
            return false;
        }

        for (uint32_t i = 0; i < kSrvCount; ++i)
            CreateShaderResourceView(_device, candidate->srvs[i].Get(), candidate->heap.GetSrvCPU(i));
        for (uint32_t i = 0; i < kUavCount; ++i)
            CreateUnorderedAccessView(_device, candidate->uavs[i].Get(), candidate->heap.GetUavCPU(i), 0);

        if (!CreateConstantsBuffer(_device, candidate->constantBuffer.Get(), candidate->constants,
                                   candidate->heap.GetCbvCPU(0)))
        {
            LOG_ERROR("[{0}] Failed to create immutable constants", _name);
            return false;
        }

        bindingIndex = _bindings.size();
        binding = candidate.get();
        _bindings.emplace_back(std::move(candidate));
        LOG_INFO("V23070 DLSS-NR created immutable binding {} for mode {}", bindingIndex,
                 InConstants.Mode);
    }

    wchar_t marker[128] {};
    swprintf_s(marker, L"OptiScaler V23080 DLSS-NR shader mode=%u immutable-binding=%zu",
               InConstants.Mode, bindingIndex);
    DredMarkers::Set(InCmdList, marker);
    LOG_DEBUG("V23070 NR shader mode {} immutable binding {} cmd {:p} src {:p} model {:p} original {:p} motion {:p} out {:p}",
              InConstants.Mode, bindingIndex, (void*) InCmdList, (void*) InSource, (void*) InModel,
              (void*) InOriginal, (void*) InMotion, (void*) OutTarget);

    FrameDescriptorHeap& currentHeap = binding->heap;

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);
    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    // Sized from the constants rather than from a resource, because the pass that shrinks the proxy
    // writes fewer pixels than its source has.
    const UINT dispatchWidth = (InConstants.Width + _numThreadsX - 1) / _numThreadsX;
    const UINT dispatchHeight = (InConstants.Height + _numThreadsY - 1) / _numThreadsY;
    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

DlssNr_Dx12::~DlssNr_Dx12()
{
}
