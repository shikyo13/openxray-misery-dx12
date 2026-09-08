#include "stdafx.h"
#include "DlssPassSetup.h"
#include "Layers/xrRender/FrameGraph/BindingSetBuilder.h"
#include "Layers/xrRender/FrameGraph/PassResourceCache.h"
#include "Layers/xrRender/FrameGraph/RenderPassBuilder.h"
#include "Layers/xrRender/FrameGraph/ShaderLoader.h"
#include "Layers/xrRender/RenderContext/RenderContext.h"
#include "Layers/xrRender/RenderContext/RenderDevice.h"
#include "Layers/xrRender/xrRender_console.h"
#ifdef XRAY_HAVE_DLSS
#include <d3d12.h>
#include <nvsdk_ngx_helpers.h>
#include <filesystem>
#endif

namespace xray::render::fg::passes {
using namespace framegraph;
struct DlssPassState::Impl {
#ifdef XRAY_HAVE_DLSS
    nvrhi::DeviceHandle device;
    ID3D12Device* nativeDevice = nullptr;
    NVSDK_NGX_Parameter* parameters = nullptr;
    NVSDK_NGX_Handle* feature = nullptr;
    bool initialized = false, supported = false;
    u32 width = 0, height = 0, lastFrame = 0;
    ~Impl() {
        if (device) device->waitForIdle();
        if (feature) NVSDK_NGX_D3D12_ReleaseFeature(feature);
        if (parameters) NVSDK_NGX_D3D12_DestroyParameters(parameters);
        if (initialized) NVSDK_NGX_D3D12_Shutdown1(nativeDevice);
    }
#endif
};
DlssPassState::DlssPassState() : impl(std::make_unique<Impl>()) {}
DlssPassState::~DlssPassState() = default;

bool DlssPassState::Prepare(nvrhi::IDevice* device, u32 width, u32 height)
{
#ifdef XRAY_HAVE_DLSS
    if (impl->device && impl->device.Get() != device) impl = std::make_unique<Impl>();
    auto& s = *impl;
    if (s.feature && s.width == width && s.height == height) return true;
    s.nativeDevice = device->getNativeObject(nvrhi::ObjectTypes::D3D12_Device);
    if (!s.nativeDevice || width < 32 || height < 32) return false;
    s.device = device;
    if (!s.initialized) {
        string_path path;
        FS.update_path(path, "$app_data_root$", "ngx");
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        if (ec) {
            Msg("! [DLSS] Cannot create local SDK cache: %s", ec.message().c_str());
            return false;
        }
        const auto widePath = std::filesystem::path(path).wstring();
        auto result = NVSDK_NGX_D3D12_Init_with_ProjectID("ffee5551-1fd0-468f-bdc5-ccb2ce6b9bca",
            NVSDK_NGX_ENGINE_TYPE_CUSTOM, "OpenXRay-MISERY-DX12", widePath.c_str(), s.nativeDevice);
        if (NVSDK_NGX_FAILED(result)) {
            Msg("! [DLSS] NGX initialization failed: 0x%08x; retaining FXAA", unsigned(result));
            return false;
        }
        s.initialized = true;
        result = NVSDK_NGX_D3D12_GetCapabilityParameters(&s.parameters);
        int available = 0;
        if (NVSDK_NGX_SUCCEED(result))
            NVSDK_NGX_Parameter_GetI(s.parameters, NVSDK_NGX_Parameter_SuperSampling_Available, &available);
        s.supported = available != 0;
        if (!available) {
            Msg("! [DLSS] Super Resolution/DLAA unavailable on this device/driver; retaining FXAA");
            return false;
        }
        Msg("* [DLSS] NGX initialized; SDK 310.7.0, native D3D12, Super Resolution/DLAA supported");
    }
    if (!s.supported) return false;
    device->waitForIdle();
    if (s.feature) {
        NVSDK_NGX_D3D12_ReleaseFeature(s.feature);
        s.feature = nullptr;
    }
    // Create on a separate, submitted list before enabling camera jitter. Failure
    // therefore leaves the normal renderer's matrices and command list untouched.
    auto cmd = device->createCommandList();
    cmd->open();
    ID3D12GraphicsCommandList* native = cmd->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
    NVSDK_NGX_DLSS_Create_Params create{};
    create.Feature.InWidth = create.Feature.InTargetWidth = width;
    create.Feature.InHeight = create.Feature.InTargetHeight = height;
    create.Feature.InPerfQualityValue = NVSDK_NGX_PerfQuality_Value_DLAA;
    create.InFeatureCreateFlags = NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
        NVSDK_NGX_DLSS_Feature_Flags_MVLowRes | NVSDK_NGX_DLSS_Feature_Flags_MVJittered |
        NVSDK_NGX_DLSS_Feature_Flags_DepthInverted | NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;
    NVSDK_NGX_Parameter_SetUI(s.parameters, NVSDK_NGX_Parameter_DLSS_Hint_Render_Preset_DLAA,
        NVSDK_NGX_DLSS_Hint_Render_Preset_K);
    const auto result = NGX_D3D12_CREATE_DLSS_EXT(native, 1, 1, &s.feature, s.parameters, &create);
    cmd->clearState();
    cmd->close();
    device->executeCommandList(cmd);
    device->waitForIdle();
    if (NVSDK_NGX_FAILED(result)) {
        Msg("! [DLSS] DLAA creation failed: 0x%08x; retaining FXAA", unsigned(result));
        return false;
    }
    s.width = width; s.height = height; s.lastFrame = 0;
    Msg("* [DLSS] Created DLAA %ux%u -> %ux%u, preset K, HDR scene input; SDR presentation unchanged",
        width, height, width, height);
    return true;
#else
    Msg("! [DLSS] This build has no NVIDIA SDK support; retaining FXAA");
    return false;
#endif
}

bool DlssPassState::Evaluate(nvrhi::ICommandList* cmd, nvrhi::ITexture* color, nvrhi::ITexture* depth,
    nvrhi::ITexture* motion, nvrhi::ITexture* output, float jitterX, float jitterY, bool reset)
{
#ifdef XRAY_HAVE_DLSS
    auto& s = *impl;
    if (!s.feature) return false;
    nvrhi::ITexture* inputs[] = {color, depth, motion};
    for (auto* texture : inputs)
        cmd->setTextureState(texture, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    cmd->setTextureState(output, nvrhi::AllSubresources, nvrhi::ResourceStates::UnorderedAccess);
    cmd->commitBarriers();
    ID3D12GraphicsCommandList* native = cmd->getNativeObject(nvrhi::ObjectTypes::D3D12_GraphicsCommandList);
    D3D12_RESOURCE_BARRIER barriers[3]{};
    for (u32 i = 0; i < 3; ++i) {
        barriers[i].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[i].Transition.pResource = inputs[i]->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
        barriers[i].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[i].Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barriers[i].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    }
    native->ResourceBarrier(3, barriers);
    NVSDK_NGX_D3D12_DLSS_Eval_Params eval{};
    eval.Feature.pInColor = color->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
    eval.Feature.pInOutput = output->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
    eval.pInDepth = depth->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
    eval.pInMotionVectors = motion->getNativeObject(nvrhi::ObjectTypes::D3D12_Resource);
    eval.InRenderSubrectDimensions = {s.width, s.height};
    eval.InJitterOffsetX = jitterX; eval.InJitterOffsetY = jitterY;
    eval.InMVScaleX = float(s.width); eval.InMVScaleY = float(s.height);
    eval.InReset = reset || s.lastFrame + 1 != Device.dwFrame;
    eval.InPreExposure = eval.InExposureScale = 1.f;
    eval.InFrameTimeDeltaInMsec = Device.fTimeDelta * 1000.f;
    const auto result = NGX_D3D12_EVALUATE_DLSS_EXT(native, s.feature, s.parameters, &eval);
    // NGX restores its documented states. Restore the combined SRV state that
    // NVRHI tracks, then discard its cached bindings after native SDK recording.
    for (auto& barrier : barriers)
        std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
    native->ResourceBarrier(3, barriers);
    cmd->clearState();
    if (NVSDK_NGX_FAILED(result)) {
        Msg("! [DLSS] Evaluate failed: 0x%08x; returning to FXAA", unsigned(result));
        ps_r_aa = 1;
        return false;
    }
    s.lastFrame = Device.dwFrame;
    if (strstr(Core.Params, "-graphics_trace") && (eval.InReset || Device.dwFrame % 120 == 0))
        Msg("* [DLSS] Evaluate frame=%u reset=%d jitter=(%.5f,%.5f) input=%ux%u output=%ux%u",
            Device.dwFrame, eval.InReset, jitterX, jitterY, s.width, s.height, s.width, s.height);
    return true;
#else
    return false;
#endif
}

VirtualResourceHandle setupDlssPass(FrameGraph& graph, fg::RenderDevice* device,
    VirtualResourceHandle color, VirtualResourceHandle depth, VirtualResourceHandle motion,
    u32 width, u32 height, float jitterX, float jitterY, bool reset, DlssPassState& state)
{
    auto* nv = device->GetNVRHIDevice();
    auto& cache = GetPassResourceCache();
    if (!state.depthPipeline) {
        auto cs = GEnv.Render->GetShaderLoader()->LoadComputeShader("dlss_depth");
        R_ASSERT2(cs.handle && cs.reflection, "DLSS depth shader compilation failed");
        state.depthLayout = cache.GetOrCreateBindingLayoutFromReflection("DLSS.Depth", *cs.reflection, nv);
        nvrhi::ComputePipelineDesc pipeline;
        pipeline.CS = cs.handle; pipeline.bindingLayouts = {state.depthLayout};
        state.depthPipeline = cache.GetOrCreateComputePipeline("DLSS.Depth", pipeline, nv);
        R_ASSERT2(state.depthPipeline, "DLSS depth pipeline creation failed");
    }
    ResourceDesc desc;
    desc.width = width; desc.height = height;
    desc.format = nvrhi::Format::R32_FLOAT;
    desc.isUAV = true;
    desc.debugName = "rt_DLSSDepth";
    auto sdkDepth = graph.CreateTexture(desc.debugName.c_str(), desc);
    struct DepthData { VirtualResourceHandle depth, output; DlssPassState* state; fg::RenderDevice* device; u32 width, height; };
    auto& converted = graph.addCallbackPass<DepthData>("DLSS.Depth",
        [&](FrameGraph& builder, PassHandle pass, DepthData& data) {
            RenderPassBuilder pb(builder, pass);
            data.depth = pb.read(depth); data.output = pb.write(sdkDepth, ResourceState::UnorderedAccess);
            data.state = &state; data.device = device; data.width = width; data.height = height;
        }, [](const DepthData& data, const FrameGraph& graph, fg::RenderContext* ctx) {
            auto* reflection = GEnv.Render->GetShaderLoader()->GetCachedReflection("dlss_depth", ".cs");
            auto* nv = data.device->GetNVRHIDevice();
            BindingSetBuilder binding(*reflection, nv, "DLSS.Depth");
            binding.Texture("t_Depth", graph.GetPhysicalTexture(data.depth))
                .TextureUAV("u_Depth", graph.GetPhysicalTexture(data.output));
            auto set = GetPassResourceCache().GetOrCreateBindingSet(binding.Build(), data.state->depthLayout, nv);
            R_ASSERT2(set, "DLSS depth binding failed");
            ctx->SetComputePipeline(data.state->depthPipeline); ctx->SetComputeBindingSet(0, set);
            ctx->Dispatch((data.width + 7) / 8, (data.height + 7) / 8, 1);
        });
    desc.format = nvrhi::Format::RGBA16_FLOAT; desc.isRenderTarget = true;
    desc.debugName = "rt_DLAA";
    auto target = graph.CreateTexture(desc.debugName.c_str(), desc);
    struct EvalData { VirtualResourceHandle color, depth, motion, output; DlssPassState* state; float jitterX, jitterY; bool reset; };
    auto& evaluated = graph.addCallbackPass<EvalData>("DLSS.DLAA",
        [&](FrameGraph& builder, PassHandle pass, EvalData& data) {
            RenderPassBuilder pb(builder, pass);
            data.color = pb.read(color); data.depth = pb.read(converted.output); data.motion = pb.read(motion);
            data.output = pb.write(target, ResourceState::UnorderedAccess);
            data.state = &state; data.jitterX = jitterX; data.jitterY = jitterY; data.reset = reset;
        }, [](const EvalData& data, const FrameGraph& graph, fg::RenderContext* ctx) {
            auto* cmd = ctx->GetCommandList();
            auto* color = graph.GetPhysicalTexture(data.color);
            auto* output = graph.GetPhysicalTexture(data.output);
            if (!data.state->Evaluate(cmd, color, graph.GetPhysicalTexture(data.depth),
                graph.GetPhysicalTexture(data.motion), output, data.jitterX, data.jitterY, data.reset))
                cmd->copyTexture(output, nvrhi::TextureSlice(), color, nvrhi::TextureSlice());
        });
    return evaluated.output;
}
}
