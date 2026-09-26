#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include "../OptiScaler/dlssnr/NrGuideSnapshot.h"
#include <iostream>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
static unsigned checks;
static void check(bool value, const char* name) { ++checks; if (!value) throw std::runtime_error(name); }
static void hr(HRESULT result) { if (FAILED(result)) throw std::runtime_error("GPU HRESULT"); }
static void transition(ID3D12GraphicsCommandList* commands, ID3D12Resource* image,
                       D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, const wchar_t*) {
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = image;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = before;
    barrier.Transition.StateAfter = after;
    commands->ResourceBarrier(1, &barrier);
}
int main() {
    ID3D12Resource *left = nullptr, *right = nullptr;
    try {
        ComPtr<ID3D12Debug> debug;
        hr(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))); debug->EnableDebugLayer();
        ComPtr<IDXGIFactory4> factory; hr(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));
        ComPtr<IDXGIAdapter> adapter; hr(factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter)));
        ComPtr<ID3D12Device> device; hr(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)));
        ComPtr<ID3D12InfoQueue> info; hr(device.As(&info));
        D3D12_COMMAND_QUEUE_DESC queueDesc{};
        ComPtr<ID3D12CommandQueue> queue; hr(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&queue)));
        ComPtr<ID3D12CommandAllocator> allocator; hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)));
        ComPtr<ID3D12GraphicsCommandList> commands;
        hr(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&commands)));
        ComPtr<ID3D12Fence> fence; hr(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
        const HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        unsigned long long serial = 0;
        std::vector<ComPtr<ID3D12Resource>> uploads;
        const auto submit = [&] {
            hr(commands->Close()); ID3D12CommandList* lists[]{commands.Get()}; queue->ExecuteCommandLists(1, lists);
            hr(queue->Signal(fence.Get(), ++serial)); hr(fence->SetEventOnCompletion(serial, done));
            check(WaitForSingleObject(done, 10000) == WAIT_OBJECT_0, "GPU timeout");
            hr(allocator->Reset()); hr(commands->Reset(allocator.Get(), nullptr)); uploads.clear();
        };
        const auto image = [&](unsigned width, DXGI_FORMAT format) {
            D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            desc.Width = width; desc.Height = 3; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
            desc.Format = format; desc.SampleDesc.Count = 1;
            D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            ComPtr<ID3D12Resource> result;
            hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&result)));
            return result;
        };
        const auto buffer = [&](D3D12_HEAP_TYPE type, UINT64 size) {
            D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = size; desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
            desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            D3D12_HEAP_PROPERTIES heap{}; heap.Type = type;
            ComPtr<ID3D12Resource> result;
            hr(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                type == D3D12_HEAP_TYPE_UPLOAD ? D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, IID_PPV_ARGS(&result)));
            return result;
        };
        const auto fill = [&](ID3D12Resource* target, unsigned pattern) {
            const auto desc = target->GetDesc();
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 bytes{};
            device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
            auto upload = buffer(D3D12_HEAP_TYPE_UPLOAD, bytes); void* mapped{}; hr(upload->Map(0, nullptr, &mapped));
            for (unsigned y=0; y<desc.Height; ++y) for (unsigned x=0; x<desc.Width; ++x)
                reinterpret_cast<unsigned*>(static_cast<char*>(mapped) + footprint.Offset + y*footprint.Footprint.RowPitch)[x] = pattern;
            upload->Unmap(0, nullptr);
            transition(commands.Get(), target, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_DEST, L"");
            D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = target;
            D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = upload.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; src.PlacedFootprint = footprint;
            commands->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            transition(commands.Get(), target, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, L"");
            uploads.push_back(upload);
        };
        const auto retire = [](ID3D12Resource*& old) { old->Release(); old = nullptr; };
        auto source = image(8, DXGI_FORMAT_R32_FLOAT);
        fill(source.Get(), 10);
        hr(DlssNr::SnapshotNrGuide(device.Get(), commands.Get(), source.Get(), &left, transition, retire));
        check(left != source.Get(), "typed guide still borrowed");
        fill(source.Get(), 20);
        hr(DlssNr::SnapshotNrGuide(device.Get(), commands.Get(), source.Get(), &right, transition, retire));
        check(right != left && right != source.Get(), "eye snapshots not independent");
        fill(source.Get(), 99); // The game's shared surface is overwritten before model consumption.
        const auto readback = [&](ID3D12Resource* snapshot) {
            const auto desc = snapshot->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 bytes{};
            device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, nullptr, nullptr, &bytes);
            auto read = buffer(D3D12_HEAP_TYPE_READBACK, bytes);
            transition(commands.Get(), snapshot, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COPY_SOURCE, L"");
            D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = snapshot;
            D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = read.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = footprint;
            commands->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            transition(commands.Get(), snapshot, D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COPY_DEST, L"");
            return std::pair{read, footprint};
        };
        auto a = readback(left), b = readback(right); submit();
        const auto verify = [&](auto& read, unsigned width, unsigned expected) {
            void* mapped{}; hr(read.first->Map(0, nullptr, &mapped));
            for (unsigned y=0; y<3; ++y) for (unsigned x=0; x<width; ++x)
                check(reinterpret_cast<unsigned*>(static_cast<char*>(mapped)+read.second.Offset+y*read.second.Footprint.RowPitch)[x] == expected, "snapshot pixels overwritten");
            read.first->Unmap(0, nullptr);
        };
        verify(a, 8, 10); verify(b, 8, 20);
        // A retired snapshot is reused with fresh pixels, never an unchanged-source shortcut.
        fill(source.Get(), 30); hr(DlssNr::SnapshotNrGuide(device.Get(), commands.Get(), source.Get(), &left, transition, retire));
        a = readback(left); submit(); verify(a, 8, 30);
        auto resized = image(4, DXGI_FORMAT_R32_TYPELESS);
        fill(resized.Get(), 40); hr(DlssNr::SnapshotNrGuide(device.Get(), commands.Get(), resized.Get(), &left, transition, retire));
        check(left->GetDesc().Width == 4 && left->GetDesc().Format == DXGI_FORMAT_R32_FLOAT, "resize/typed replacement");
        a = readback(left); submit(); verify(a, 4, 40);
        check(DlssNr::NrTypedGuideFormat(DXGI_FORMAT_R16G16_TYPELESS) == DXGI_FORMAT_R16G16_FLOAT, "motion typed format");
        for (UINT64 i=0; i<info->GetNumStoredMessages(); ++i) {
            SIZE_T bytes{}; hr(info->GetMessage(i, nullptr, &bytes)); std::vector<char> storage(bytes);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data()); hr(info->GetMessage(i, message, &bytes));
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) throw std::runtime_error(message->pDescription);
        }
        hr(commands->Close()); CloseHandle(done); left->Release(); right->Release();
        std::cout << checks << " private NR guide GPU checks passed\n"; return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
