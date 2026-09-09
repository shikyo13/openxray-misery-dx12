#include "stdafx.h"
#include "FsrFrameGeneration.h"
#include "Layers/xrRender/xrRender_console.h"
#include <d3d12.h>
#include <dxgi1_5.h>
#include <mutex>
using xray::render::fg::ps_r_aa;
using xray::render::fg::ps_r_fsr_fg;
using xray::render::fg::ps_r_fsr_fg_capture;
#ifdef XRAY_HAVE_FSR3
#include <ffx_api_loader.h>
#include <dx12/ffx_api_dx12.h>
#include <ffx_framegeneration.h>
#include <dx12/ffx_api_framegeneration_dx12.h>
#include <filesystem>
#endif

struct FsrFrameGeneration::Impl {
#ifdef XRAY_HAVE_FSR3
    HMODULE module = nullptr;
    ffxFunctions api{};
    ffxContext swapContext = nullptr, context = nullptr;
    IDXGISwapChain3* swapchain = nullptr; // owned by the backend / SDK swap context
    nvrhi::DeviceHandle device;
    nvrhi::TextureHandle hudless;
    std::mutex mutex;
    u32 width = 0, height = 0, lastFrame = 0, preparedFrame = ~0u;
    u64 generatedDispatches = 0, renderedPresents = 0;
    bool enabled = false, failed = false;
    // Explicit diagnostics only: neighboring real images, generated image and HUD-less input.
    ID3D12Resource* captureBuffers[4]{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT captureFootprint{};
    UINT64 captureBytes = 0;
    u64 captureFrame = 0;
    u32 captureMask = 0, captureWidth = 0, captureHeight = 0;
    int captureRequest = 0;
    ID3D12Resource* inputCaptureBuffers[2]{};
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT inputCaptureFootprints[2]{};
    UINT64 inputCaptureBytes[2]{};

    void ClearCapture() {
        for (auto*& buffer : captureBuffers) { if (buffer) buffer->Release(); buffer = nullptr; }
        for (auto*& buffer : inputCaptureBuffers) { if (buffer) buffer->Release(); buffer = nullptr; }
        captureMask = 0; captureFrame = 0;
    }
    void BeginCapture(nvrhi::ITexture* texture) {
        if (!strstr(Core.Params, "-graphics_trace") || ps_r_fsr_fg_capture <= 0 ||
            ps_r_fsr_fg_capture == captureRequest || captureFrame) return;
        captureRequest = ps_r_fsr_fg_capture;
        auto* native = static_cast<ID3D12Device*>(device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device).pointer);
        auto* resource = static_cast<ID3D12Resource*>(texture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource).pointer);
        const auto desc = resource->GetDesc();
        native->GetCopyableFootprints(&desc, 0, 1, 0, &captureFootprint, nullptr, nullptr, &captureBytes);
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK; heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC buffer{};
        buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = captureBytes;
        buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = buffer.SampleDesc.Count = 1;
        buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        for (auto*& output : captureBuffers) {
            if (FAILED(native->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&output)))) {
                Msg("! [FSR3FG] Capture allocation failed"); ClearCapture(); return;
            }
        }
        captureWidth = u32(desc.Width); captureHeight = desc.Height;
        captureFrame = u64(Device.dwFrame) + 1;
    }
    static D3D12_RESOURCE_STATES NativeState(uint32_t state) {
        D3D12_RESOURCE_STATES value = D3D12_RESOURCE_STATE_COMMON;
        if (state & FFX_API_RESOURCE_STATE_UNORDERED_ACCESS) value |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        if (state & FFX_API_RESOURCE_STATE_COMPUTE_READ) value |= D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        if (state & FFX_API_RESOURCE_STATE_PIXEL_READ) value |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        if (state & FFX_API_RESOURCE_STATE_COPY_SRC) value |= D3D12_RESOURCE_STATE_COPY_SOURCE;
        if (state & FFX_API_RESOURCE_STATE_COPY_DEST) value |= D3D12_RESOURCE_STATE_COPY_DEST;
        if (state & FFX_API_RESOURCE_STATE_RENDER_TARGET) value |= D3D12_RESOURCE_STATE_RENDER_TARGET;
        return value;
    }
    void Capture(ID3D12GraphicsCommandList* cmd, const FfxApiResource& source, u32 slot) {
        if (!captureBuffers[slot] || !source.resource) return;
        auto* resource = static_cast<ID3D12Resource*>(source.resource);
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = NativeState(source.state);
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        if (barrier.Transition.StateBefore != barrier.Transition.StateAfter) cmd->ResourceBarrier(1, &barrier);
        D3D12_TEXTURE_COPY_LOCATION from{}, to{};
        from.pResource = resource; from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        to.pResource = captureBuffers[slot]; to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        to.PlacedFootprint = captureFootprint;
        cmd->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
        if (barrier.Transition.StateBefore != barrier.Transition.StateAfter) cmd->ResourceBarrier(1, &barrier);
        captureMask |= 1u << slot;
    }
    void WriteCapture() {
        if (!captureFrame) return;
        const char* labels[] = {"previous", "generated", "current", "hudless"};
        for (u32 slot = 0; slot < 4; ++slot) {
            if (!(captureMask & (1u << slot))) continue;
            void* pixels = nullptr;
            D3D12_RANGE range{0, size_t(captureBytes)};
            if (FAILED(captureBuffers[slot]->Map(0, &range, &pixels))) continue;
            string_path name;
            xr_sprintf(name, "fsr_fg_%02d_%llu_%s.tga", captureRequest, captureFrame, labels[slot]);
            if (auto* writer = FS.w_open("$app_data_root$", name)) {
                u8 header[18]{};
                header[2] = 2; header[12] = u8(captureWidth); header[13] = u8(captureWidth >> 8);
                header[14] = u8(captureHeight); header[15] = u8(captureHeight >> 8);
                header[16] = 32; header[17] = 0x28;
                writer->w(header, sizeof(header));
                xr_vector<u8> row(captureWidth * 4);
                for (u32 y = 0; y < captureHeight; ++y) {
                    const auto* source = static_cast<const u8*>(pixels) + captureFootprint.Offset +
                        size_t(y) * captureFootprint.Footprint.RowPitch;
                    for (u32 x = 0; x < captureWidth; ++x) {
                        row[x*4] = source[x*4+2]; row[x*4+1] = source[x*4+1];
                        row[x*4+2] = source[x*4]; row[x*4+3] = 255;
                    }
                    writer->w(row.data(), u32(row.size()));
                }
                FS.w_close(writer);
                Msg("* [FSR3FG] Capture %s %ux%u mask=%u", name, captureWidth, captureHeight, captureMask);
            }
            D3D12_RANGE noWrites{0, 0}; captureBuffers[slot]->Unmap(0, &noWrites);
        }
        for (u32 slot = 0; slot < 2; ++slot) {
            if (!inputCaptureBuffers[slot]) continue;
            void* pixels = nullptr;
            D3D12_RANGE range{0, size_t(inputCaptureBytes[slot])};
            if (FAILED(inputCaptureBuffers[slot]->Map(0, &range, &pixels))) continue;
            string_path name;
            xr_sprintf(name, "fsr_fg_%02d_%llu_%s.raw", captureRequest, captureFrame, slot ? "depth_r32f" : "motion_rg16f");
            if (auto* writer = FS.w_open("$app_data_root$", name)) {
                const auto& fp = inputCaptureFootprints[slot];
                writer->w_u32(fp.Footprint.Width); writer->w_u32(fp.Footprint.Height);
                for (u32 y = 0; y < fp.Footprint.Height; ++y)
                    writer->w(static_cast<const u8*>(pixels) + fp.Offset + size_t(y)*fp.Footprint.RowPitch, fp.Footprint.Width*4);
                FS.w_close(writer);
                Msg("* [FSR3FG] Input capture %s %ux%u", name, fp.Footprint.Width, fp.Footprint.Height);
            }
            D3D12_RANGE noWrites{0,0}; inputCaptureBuffers[slot]->Unmap(0, &noWrites);
        }
        ClearCapture();
    }
    void CaptureInputs(nvrhi::ICommandList* command, nvrhi::ITexture* motion, nvrhi::ITexture* depth) {
        if (!captureFrame || captureFrame != Device.dwFrame) return;
        auto* native = static_cast<ID3D12Device*>(device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device).pointer);
        auto* cmd = static_cast<ID3D12GraphicsCommandList*>(command->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList).pointer);
        nvrhi::ITexture* inputs[] = {motion, depth};
        for (u32 slot=0; slot<2; ++slot) {
            auto* resource = static_cast<ID3D12Resource*>(inputs[slot]->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource).pointer);
            const auto desc = resource->GetDesc();
            native->GetCopyableFootprints(&desc, 0, 1, 0, &inputCaptureFootprints[slot], nullptr, nullptr, &inputCaptureBytes[slot]);
            D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK; heap.CreationNodeMask = heap.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC buffer{};
            buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; buffer.Width = inputCaptureBytes[slot];
            buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = buffer.SampleDesc.Count = 1;
            buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(native->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer, D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr, IID_PPV_ARGS(&inputCaptureBuffers[slot])))) continue;
            D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = resource; barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            cmd->ResourceBarrier(1, &barrier);
            D3D12_TEXTURE_COPY_LOCATION from{}, to{};
            from.pResource=resource; from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            to.pResource=inputCaptureBuffers[slot]; to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            to.PlacedFootprint=inputCaptureFootprints[slot];
            cmd->CopyTextureRegion(&to,0,0,0,&from,nullptr);
            std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter); cmd->ResourceBarrier(1,&barrier);
        }
    }
    void Wait() {
        if (!swapContext) return;
        ffxDispatchDescFrameGenerationSwapChainWaitForPresentsDX12 wait{};
        wait.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12;
        Check(api.Dispatch(&swapContext, &wait.header), "Wait for presents");
    }

    bool Check(ffxReturnCode_t code, pcstr operation) {
        if (code == FFX_API_RETURN_OK) return true;
        Msg("! [FSR3FG] %s failed: %u; frame generation disabled", operation, unsigned(code));
        failed = true; ps_r_fsr_fg = 0;
        return false;
    }
    void Version(ffxContext* value, pcstr name) {
        ffxQueryGetProviderVersion query{};
        query.header.type = FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION;
        if (api.Query(value, &query.header) == FFX_API_RETURN_OK)
            Msg("* [FSR3FG] %s provider: %s", name, query.versionName ? query.versionName : "unknown");
    }
    void Disable() {
        if (!context || !enabled) return;
        ffxConfigureDescFrameGeneration desc{};
        desc.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
        desc.swapChain = swapchain;
        Check(api.Configure(&context, &desc.header), "Disable");
        enabled = false;
        Msg("* [FSR3FG] Disabled; successful generation dispatches=%llu rendered presents=%llu",
            generatedDispatches, renderedPresents);
    }
    static ffxReturnCode_t Generate(ffxDispatchDescFrameGeneration* desc, void* user) {
        auto& s = *static_cast<Impl*>(user);
        // Called by Present under the same context lock. No NVRHI access here.
        const auto result = s.api.Dispatch(&s.context, &desc->header);
        if (result == FFX_API_RETURN_OK) {
            s.generatedDispatches += desc->numGeneratedFrames;
            if (s.captureFrame && desc->numGeneratedFrames) {
                auto* cmd = static_cast<ID3D12GraphicsCommandList*>(desc->commandList);
                if (desc->frameID + 1 == s.captureFrame) s.Capture(cmd, desc->presentColor, 0);
                if (desc->frameID == s.captureFrame) {
                    s.Capture(cmd, desc->outputs[0], 1); s.Capture(cmd, desc->presentColor, 2);
                    s.Capture(cmd, ffxApiGetResourceDX12(static_cast<ID3D12Resource*>(s.hudless->getNativeObject(
                        nvrhi::ObjectTypes::D3D12_Resource).pointer), FFX_API_RESOURCE_STATE_COMMON), 3);
                }
            }
            if (strstr(Core.Params, "-graphics_trace") && (desc->reset || desc->frameID % 120 == 0))
                Msg("* [FSR3FG] Generate frame=%llu frames=%u reset=%d transfer=%u output=%ux%u total=%llu",
                    desc->frameID, desc->numGeneratedFrames, desc->reset, desc->backbufferTransferFunction,
                    desc->outputs[0].description.width, desc->outputs[0].description.height, s.generatedDispatches);
        } else {
            s.Check(result, "Generate");
        }
        return result;
    }
#endif
};

FsrFrameGeneration::FsrFrameGeneration() : impl(std::make_unique<Impl>()) {}
FsrFrameGeneration::~FsrFrameGeneration() { Shutdown(); }

bool FsrFrameGeneration::CreateSwapChain(IDXGIFactory4* factory, ID3D12CommandQueue* queue,
    HWND window, DXGI_SWAP_CHAIN_DESC1& desc, IDXGISwapChain3** output)
{
#ifdef XRAY_HAVE_FSR3
    auto& s = *impl;
    wchar_t exe[MAX_PATH]{};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    const auto dll = std::filesystem::path(exe).parent_path() / L"amd_fidelityfx_loader_dx12.dll";
    s.module = LoadLibraryExW(dll.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!s.module) {
        Msg("! [FSR3FG] SDK loader unavailable (%lu); using the native swapchain", GetLastError());
        return false;
    }
    ffxLoadFunctions(&s.api, s.module);
    if (!s.api.CreateContext || !s.api.DestroyContext || !s.api.Configure || !s.api.Dispatch || !s.api.Query)
        return false;

    ffxCreateContextDescFrameGenerationSwapChainVersionDX12 version{};
    version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_VERSION_DX12;
    version.version = FFX_FRAMEGENERATION_SWAPCHAIN_DX12_VERSION;
    IDXGISwapChain4* swapchain = nullptr;
    ffxCreateContextDescFrameGenerationSwapChainForHwndDX12 create{};
    create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12;
    create.header.pNext = &version.header;
    create.swapchain = &swapchain; create.hwnd = window; create.desc = &desc;
    create.dxgiFactory = factory; create.gameQueue = queue;
    if (!s.Check(s.api.CreateContext(&s.swapContext, &create.header, nullptr), "Create swapchain"))
        return false;
    const auto hr = swapchain->QueryInterface(IID_PPV_ARGS(output));
    swapchain->Release();
    // The context retains the SDK's reference. The backend owns the QI reference.
    if (FAILED(hr)) return false;
    s.swapchain = *output;
    s.Version(&s.swapContext, "Swapchain");
    Msg("* [FSR3FG] SDR RGBA8 proxy swapchain ready; frame generation remains independently selectable");
    return true;
#else
    return false;
#endif
}

bool FsrFrameGeneration::EnsureContext(nvrhi::IDevice* device, u32 width, u32 height)
{
#ifdef XRAY_HAVE_FSR3
    auto& s = *impl;
    std::lock_guard lock(s.mutex);
    if (!s.swapContext || s.failed) { ps_r_fsr_fg = 0; return false; }
    if (s.context) return s.width == width && s.height == height;
    s.device = device;
    auto* native = static_cast<ID3D12Device*>(device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device).pointer);
    ffxCreateBackendDX12Desc backend{};
    backend.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12;
    backend.device = native;
    ffxCreateContextDescFrameGenerationVersion version{};
    version.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION_VERSION;
    version.header.pNext = &backend.header;
    version.version = FFX_FRAMEGENERATION_VERSION;
    ffxCreateContextDescFrameGeneration create{};
    create.header.type = FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION;
    create.header.pNext = &version.header;
    create.displaySize = {width, height}; create.maxRenderSize = {width, height};
    create.backBufferFormat = FFX_API_SURFACE_FORMAT_R8G8B8A8_UNORM;
    // The prepare shader supplies motion with sampling jitter already removed.
    create.flags = FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED;
    // This image outlives frame-graph recording and is read by the SDK during
    // Present. COMMON must remain its state at command-list close as well.
    nvrhi::TextureDesc hudlessDesc;
    hudlessDesc.width = width; hudlessDesc.height = height;
    hudlessDesc.format = nvrhi::Format::RGBA8_UNORM;
    hudlessDesc.isRenderTarget = true;
    hudlessDesc.initialState = nvrhi::ResourceStates::Common;
    hudlessDesc.keepInitialState = true;
    hudlessDesc.debugName = "FSR3FG.Hudless";
    auto hudless = device->createTexture(hudlessDesc);
    if (!hudless) {
        Msg("! [FSR3FG] HUD-less target allocation failed");
        ps_r_fsr_fg = 0;
        return false;
    }
    // Synchronous interpolation orders HUD-less reuse on the game graphics queue.
    // Presentation still uses the SDK's separately paced queue.
    if (!s.Check(s.api.CreateContext(&s.context, &create.header, nullptr), "Create frame generation")) return false;
    s.hudless = hudless;
    s.width = width; s.height = height; s.lastFrame = 0;
    s.Version(&s.context, "Frame generation");
    Msg("* [FSR3FG] Context %ux%u, reverse Z, render-resolution motion, SDR, HUD-less composition, async compute=0",
        width, height);
    return true;
#else
    ps_r_fsr_fg = 0;
    return false;
#endif
}

nvrhi::ITexture* FsrFrameGeneration::GetHudlessTexture() const
{
#ifdef XRAY_HAVE_FSR3
    return impl->hudless;
#else
    return nullptr;
#endif
}

bool FsrFrameGeneration::Prepare(nvrhi::ICommandList* cmd, nvrhi::ITexture* depth,
    nvrhi::ITexture* motion, nvrhi::ITexture* hudless, float jitterX, float jitterY, bool reset)
{
#ifdef XRAY_HAVE_FSR3
    auto& s = *impl;
    std::lock_guard lock(s.mutex);
    if (!s.context || s.failed || !depth || !motion || !hudless) return false;
    if (s.captureMask == 15) { s.Wait(); s.WriteCapture(); }
    s.BeginCapture(hudless);
    const auto& d = depth->getDesc();
    if (motion->getDesc().width != d.width || motion->getDesc().height != d.height ||
        d.width > s.width || d.height > s.height || hudless != s.hudless.Get() ||
        hudless->getDesc().width != s.width || hudless->getDesc().height != s.height)
        return s.Check(FFX_API_RETURN_ERROR_PARAMETER, "Input dimensions");
    auto resource = [](nvrhi::ITexture* texture) {
        return ffxApiGetResourceDX12(static_cast<ID3D12Resource*>(
            texture->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource).pointer), FFX_API_RESOURCE_STATE_COMMON);
    };
    // FFX uses legacy barriers and restores the declared input state. Bridge
    // both directions through COMMON without disabling enhanced barriers.
    cmd->setTextureState(depth, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
    cmd->setTextureState(motion, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
    cmd->setTextureState(hudless, nvrhi::AllSubresources, nvrhi::ResourceStates::Common);
    cmd->commitBarriers();
    s.CaptureInputs(cmd, motion, depth);
    ffxConfigureDescFrameGeneration config{};
    config.header.type = FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION;
    config.swapChain = s.swapchain;
    config.frameGenerationEnabled = true;
    config.frameGenerationCallback = &Impl::Generate;
    config.frameGenerationCallbackUserContext = &s;
    config.HUDLessColor = resource(hudless);
    config.generationRect = {0, 0, int32_t(s.width), int32_t(s.height)};
    config.frameID = Device.dwFrame;
    if (!s.Check(s.api.Configure(&s.context, &config.header), "Configure")) return false;
    if (!s.enabled) Msg("* [FSR3FG] Enabled with r_aa=%u at frame=%u", ps_r_aa, Device.dwFrame);
    s.enabled = true;
    ffxDispatchDescFrameGenerationPrepareV2 prep{};
    prep.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_V2;
    prep.commandList = cmd->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList).pointer;
    prep.frameID = Device.dwFrame; prep.renderSize = {d.width, d.height};
    prep.jitterOffset = {jitterX, jitterY};
    prep.motionVectorScale = {float(d.width), float(d.height)};
    prep.frameTimeDelta = Device.fTimeDelta * 1000.f;
    prep.reset = reset || !s.lastFrame || s.lastFrame + 1 != Device.dwFrame;
    // Recover finite reverse-Z projection planes directly, including zoom FOV.
    const auto& p = Device.mProject;
    prep.cameraNear = p._43 / (1.f - p._33);
    prep.cameraFar = -p._43 / p._33;
    prep.cameraFovAngleVertical = 2.f * std::atan(1.f / p._22);
    prep.viewSpaceToMetersFactor = 1.f;
    prep.depth = resource(depth); prep.motionVectors = resource(motion);
    const Fvector vectors[] = {Device.vCameraPosition, Device.vCameraTop, Device.vCameraRight, Device.vCameraDirection};
    memcpy(prep.cameraPosition, &vectors[0], 3 * sizeof(float));
    memcpy(prep.cameraUp, &vectors[1], 3 * sizeof(float));
    memcpy(prep.cameraRight, &vectors[2], 3 * sizeof(float));
    memcpy(prep.cameraForward, &vectors[3], 3 * sizeof(float));
    const auto result = s.api.Dispatch(&s.context, &prep.header);
    // FFX restores COMMON input states but changes native descriptor heaps/pipelines.
    cmd->clearState();
    if (!s.Check(result, "Prepare")) return false;
    s.lastFrame = s.preparedFrame = Device.dwFrame;
    if (strstr(Core.Params, "-graphics_trace") && (prep.reset || Device.dwFrame % 120 == 0))
        Msg("* [FSR3FG] Prepare frame=%u reset=%d render=%ux%u display=%ux%u jitter=(%.5f,%.5f) near=%.4f far=%.2f fovY=%.5f",
            Device.dwFrame, prep.reset, d.width, d.height, s.width, s.height, jitterX, jitterY,
            prep.cameraNear, prep.cameraFar, prep.cameraFovAngleVertical);
    return true;
#else
    return false;
#endif
}

HRESULT FsrFrameGeneration::Present(IDXGISwapChain3* swapchain, u32 interval, u32 flags)
{
#ifdef XRAY_HAVE_FSR3
    auto& s = *impl;
    std::lock_guard lock(s.mutex);
    // Loading/menu-only frames have no depth/motion preparation and must not interpolate stale inputs.
    if (!ps_r_fsr_fg || s.preparedFrame != Device.dwFrame || s.failed) s.Disable();
    const auto result = swapchain->Present(interval, flags);
    if (SUCCEEDED(result) && s.enabled) ++s.renderedPresents;
    if (strstr(Core.Params, "-graphics_trace") && Device.dwFrame % 120 == 0) {
        UINT presents = 0;
        if (SUCCEEDED(swapchain->GetLastPresentCount(&presents)))
            Msg("* [FSR3FG] Present frame=%u enabled=%d dxgi_presents=%u generated_dispatches=%llu",
                Device.dwFrame, int(s.enabled), presents, s.generatedDispatches);
    }
    s.preparedFrame = ~0u;
    return result;
#else
    return swapchain->Present(interval, flags);
#endif
}

void FsrFrameGeneration::WaitForPresents()
{
#ifdef XRAY_HAVE_FSR3
    auto& s = *impl;
    std::lock_guard lock(s.mutex);
    s.Wait();
#endif
}

void FsrFrameGeneration::ReleaseContext()
{
#ifdef XRAY_HAVE_FSR3
    WaitForPresents();
    auto& s = *impl;
    std::lock_guard lock(s.mutex);
    s.Disable();
    if (s.device) s.device->waitForIdle();
    s.WriteCapture();
    if (s.context) s.Check(s.api.DestroyContext(&s.context, nullptr), "Destroy context");
    s.context = nullptr; s.hudless = nullptr; s.device = nullptr;
    s.width = s.height = s.lastFrame = 0;
#endif
}

void FsrFrameGeneration::Shutdown()
{
#ifdef XRAY_HAVE_FSR3
    ReleaseContext();
    auto& s = *impl;
    std::lock_guard lock(s.mutex);
    if (s.swapContext) s.Check(s.api.DestroyContext(&s.swapContext, nullptr), "Destroy swapchain");
    s.swapContext = nullptr; s.swapchain = nullptr;
    if (s.module) FreeLibrary(s.module);
    s.module = nullptr;
#endif
}
