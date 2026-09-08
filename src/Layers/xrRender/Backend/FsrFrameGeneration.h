#pragma once
#include <nvrhi/nvrhi.h>
#include <memory>

struct ID3D12CommandQueue;
struct IDXGIFactory4;
struct IDXGISwapChain3;
struct DXGI_SWAP_CHAIN_DESC1;

// The SDK owns presentation queues; keep their lifetime outside the frame graph.
class FsrFrameGeneration {
public:
    FsrFrameGeneration();
    ~FsrFrameGeneration();
    bool CreateSwapChain(IDXGIFactory4* factory, ID3D12CommandQueue* queue,
        HWND window, DXGI_SWAP_CHAIN_DESC1& desc, IDXGISwapChain3** output);
    bool EnsureContext(nvrhi::IDevice* device, u32 width, u32 height);
    bool Prepare(nvrhi::ICommandList* cmd, nvrhi::ITexture* depth,
        nvrhi::ITexture* motion, nvrhi::ITexture* hudless, float jitterX, float jitterY, bool reset);
    HRESULT Present(IDXGISwapChain3* swapchain, u32 interval, u32 flags);
    void WaitForPresents();
    void ReleaseContext();
    void Shutdown();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
