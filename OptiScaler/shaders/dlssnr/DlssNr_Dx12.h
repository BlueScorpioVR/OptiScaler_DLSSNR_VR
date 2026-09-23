#pragma once

// The composition pass for Neural Rendering.
//
// Neural Rendering is two things, and only one of them is a shader. The model is an NGX feature --
// created and evaluated, not dispatched -- and that stays where it is. This is the other half: the
// pass that builds the tone-mapped proxy the model is shown, and then transfers the model's answer
// back onto the real frame.
//
// It is an ordinary compute shader with a constant struct, so it belongs here alongside RCAS and
// Output Scaling rather than owning a bespoke root signature and descriptor ring of its own.
//
// One shader, three modes, because all three read and write the same set of resources and differ
// only in what they compute:
//
//   Encode   the frame -> a tone-mapped proxy, plus an untouched copy to transfer against later
//   Down     the proxy -> a smaller proxy, when the model is asked to work below full resolution
//   Resolve  proxy + model answer + untouched copy -> the frame, edited

#include "DlssNr_Common.h"

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <shaders/Shader_Dx12.h>
#include <shaders/Shader_Dx12Utils.h>
#include <memory>
#include <mutex>
#include <vector>

class DlssNr_Dx12 : public Shader_Dx12, public DlssNr_Common
{
  private:
    struct DispatchBinding;

    // A descriptor heap and its constants are immutable after creation. Fixed rotating heaps are
    // not safe here: OptiScaler records into command lists owned and submitted by the game, so it
    // cannot know when a slot has retired. OFXR can deepen that queue beyond any guessed ring size.
    // Identical dispatches share one read-only binding; a changed resource or constant set creates
    // another binding which remains alive for this shader's lifetime.
    std::vector<std::unique_ptr<DispatchBinding>> _bindings;
    std::mutex _bindingsMutex;

    // The shader reads five inputs and writes two, and not every mode uses all of them. Unused slots
    // still need a view bound -- an unbound descriptor is not an empty read, it is a read from
    // nothing -- so a stand-in is written into whichever are spare.
    static constexpr uint32_t kSrvCount = 5;
    static constexpr uint32_t kUavCount = 2;

    uint32_t _numThreadsX = 8;
    uint32_t _numThreadsY = 8;

  public:
    DlssNr_Dx12(std::string InName, ID3D12Device* InDevice);
    ~DlssNr_Dx12();

    // Records one pass. Resources that a given mode does not read may be null; a stand-in is bound in
    // their place so every descriptor in the table is valid.
    bool Dispatch(ID3D12GraphicsCommandList* InCmdList, const DlssNrConstants& InConstants,
                  ID3D12Resource* InSource, ID3D12Resource* InModel, ID3D12Resource* InOriginal,
                  ID3D12Resource* InMotion, ID3D12Resource* InPrevEdit, ID3D12Resource* OutTarget,
                  ID3D12Resource* OutKeep);

};
