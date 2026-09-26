#pragma once

#include <d3d12.h>
#include <nvsdk_ngx.h>
#include "NrSubmissionLedger.h"

// V23070: queue-depth-safe DLSS Neural Rendering route, selectable before or after super resolution.
//
// Neural Rendering is a post-process, not an upscaler and not a denoiser: it takes a finished frame plus
// depth and motion vectors and synthesises detail. NVIDIA ships no public integration for it, so it is
// driven directly through nvngx_dlssnr.dll as feature 18.
//
// OptiScaler is the right host for it because of one thing it knows that an external hook cannot: which
// NGX evaluate belongs to the upscaler and which to frame generation. Both are handed depth and motion
// vectors, so anything guessing from the parameter block alone attaches to both and runs the model twice
// per rendered frame. Here it is a lookup on the feature handle.
class Config;

namespace DlssNr
{
// The model runs immediately before the game's upscaler, before the interface is drawn. It is shown a
// display-referred proxy of that frame -- the sort of picture it was trained on -- and its answer is
// composed back over the untouched original.
// Runs the model over Color on the same command list, leaving it readable for super resolution.
// Called only for upscaler evaluates -- never for frame generation, which is the whole point.
//
// Safe to call every frame; it builds what it needs on first use and disables itself for the session if
// anything fails, rather than retrying into a crash.
// timingQueue is the queue this command list will be executed on, when the caller knows it.
// State::currentCommandQueue only exists once a D3D12 swapchain has been created, which a Vulkan
// game never does -- so without this the pass runs and never reports what it cost.
void EvaluateBeforeUpscale(unsigned int sourceHandleId, ID3D12GraphicsCommandList* cmdList,
                          NVSDK_NGX_Parameter* params,
                          ID3D12CommandQueue* timingQueue = nullptr);

// Runs the same single NR model after SR, over the completed Output texture.
void EvaluateAfterUpscale(unsigned int sourceHandleId, ID3D12GraphicsCommandList* cmdList,
                          NVSDK_NGX_Parameter* params,
                          ID3D12CommandQueue* timingQueue = nullptr);

// Called by the D3D12 queue hook after a batch has actually been submitted. NR records work into
// command lists owned by the game, so this is the first point where a fence can be attached to that
// work. The next evaluate for the same source handle will not reuse the NVIDIA feature or its scratch
// surfaces until this fence has retired.
std::vector<NrSubmissionReceipt> TakeCommandListSubmissionReceipts(
    unsigned int commandListCount, ID3D12CommandList* const* commandLists);
void CompleteCommandListSubmission(ID3D12CommandQueue* queue,
                                   const std::vector<NrSubmissionReceipt>& receipts);

// Retires the model feature and resources associated with one source upscaler handle. The resources
// are parked rather than destroyed immediately because the GPU can still reference them.
void ReleaseForHandle(unsigned int sourceHandleId);


// Frame generation titles tag their UI layer through Streamline; a copy of it makes the HUD mask
// exact at the finished frame. Called at tag time.




// The settings panel, drawn inside OptiScaler's menu.
void RenderMenu(::Config* config, float menuResScale);

// Clears the session failure latch, so a failure caused by transient thrash does not cost a restart.
void RetryAfterFailure();


// Whether the model is loaded and running, for the overlay.
bool IsRunning();

// Why it is not, if it is not. Empty while it is running or has not been tried yet.
const char* FailureReason();

// The white point the exposure meter has settled on, or 0 if it has not taken a reading yet. For the
// overlay, so the number in use is visible rather than inferred.

// What the pass last cost on the GPU, in milliseconds, or nothing if it has not been measured yet.
std::optional<double> LastGpuTime();

// Writes a run of pre-SR frames, each before and after the model's edit (not the final SR output).
// The pair is a control: same frames, same run, one variable.
void RequestCapture(unsigned int frames);
bool CaptureInProgress();

void Shutdown();
} // namespace DlssNr
