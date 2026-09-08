#include "stdafx.h"
#include "FsrFrameGenerationPassSetup.h"
#include "Layers/xrRender/FrameGraph/BindingSetBuilder.h"
#include "Layers/xrRender/FrameGraph/PassResourceCache.h"
#include "Layers/xrRender/FrameGraph/RenderPassBuilder.h"
#include "Layers/xrRender/FrameGraph/ShaderLoader.h"
#include "Layers/xrRender/RenderContext/RenderContext.h"
#include "Layers/xrRender/RenderContext/RenderDevice.h"
#include "Layers/xrRender/xrRender_console.h"
#ifdef _WIN32
#include "Layers/xrRender/Backend/D3D12Backend.h"
#include "Layers/xrRender/Backend/FsrFrameGeneration.h"
#endif

namespace xray::render::fg::passes {
using namespace framegraph;
bool prepareFsrFrameGeneration(fg::RenderDevice* device, u32 width, u32 height)
{
#ifdef _WIN32
    if (GEnv.Backend && GEnv.Backend->GetAPI() == IRenderBackend::API::D3D12) {
        auto* fg = static_cast<D3D12Backend*>(GEnv.Backend)->GetFrameGeneration();
        return fg && fg->EnsureContext(device->GetNVRHIDevice(), width, height);
    }
#endif
    ps_r_fsr_fg = 0;
    return false;
}

VirtualResourceHandle setupFsrFrameGenerationPass(FrameGraph& graph,
    fg::RenderDevice* device, VirtualResourceHandle backbuffer, VirtualResourceHandle hudless,
    VirtualResourceHandle depth, VirtualResourceHandle motion, u32 width, u32 height,
    float jitterX, float jitterY, bool reset, FsrFrameGenerationPassState& state)
{
#ifdef _WIN32
    auto* nv = device->GetNVRHIDevice();
    auto& cache = GetPassResourceCache();
    if (!state.depthPipeline) {
        auto cs = GEnv.Render->GetShaderLoader()->LoadComputeShader("fsr_fg_prepare");
        R_ASSERT2(cs.handle && cs.reflection, "Temporal depth conversion shader compilation failed");
        state.depthLayout = cache.GetOrCreateBindingLayoutFromReflection("FSR3FG.Depth", *cs.reflection, nv);
        nvrhi::ComputePipelineDesc pipeline;
        pipeline.CS = cs.handle; pipeline.bindingLayouts = {state.depthLayout};
        state.depthPipeline = cache.GetOrCreateComputePipeline("FSR3FG.Depth", pipeline, nv);
        R_ASSERT2(state.depthPipeline, "FSR frame generation depth pipeline creation failed");
    }
    ResourceDesc desc;
    desc.width = width; desc.height = height; desc.format = nvrhi::Format::R32_FLOAT;
    desc.isUAV = true; desc.debugName = "rt_FSR3Depth";
    auto converted = graph.CreateTexture(desc.debugName.c_str(), desc);
    desc.format = nvrhi::Format::RG16_FLOAT; desc.debugName = "rt_FSR3Motion";
    auto convertedMotion = graph.CreateTexture(desc.debugName.c_str(), desc);
    reset = reset || !state.lastFrame || state.lastFrame + 1 != Device.dwFrame || state.width != width || state.height != height;
    Fvector4 motionParams;
    motionParams.set((state.previousJitterX-jitterX)/float(width), (state.previousJitterY-jitterY)/float(height), reset ? 1.f : 0.f, 0.f);
    state.previousJitterX = jitterX; state.previousJitterY = jitterY;
    state.lastFrame = Device.dwFrame; state.width = width; state.height = height;
    struct Data {
        VirtualResourceHandle depth, converted, motion, convertedMotion, hudless, backbuffer;
        FsrFrameGenerationPassState* state;
        fg::RenderDevice* device;
        Fvector4 motionParams;
        u32 width, height; float jitterX, jitterY; bool reset;
    };
    auto& data = graph.addCallbackPass<Data>("FSR3FG.Prepare",
        [&](FrameGraph& builder, PassHandle pass, Data& data) {
            RenderPassBuilder pb(builder, pass);
            data.depth = pb.read(depth); data.motion = pb.read(motion); data.hudless = pb.read(hudless);
            data.converted = pb.write(converted, ResourceState::UnorderedAccess);
            data.convertedMotion = pb.write(convertedMotion, ResourceState::UnorderedAccess);
            data.backbuffer = pb.write(backbuffer, ResourceState::Present);
            pb.sideEffects();
            data.state = &state; data.width = width; data.height = height;
            data.device = device; data.motionParams = motionParams;
            data.jitterX = jitterX; data.jitterY = jitterY; data.reset = reset;
        }, [](const Data& data, const FrameGraph& graph, fg::RenderContext* ctx) {
            auto* cmd = ctx->GetCommandList();
            auto* nv = cmd->getDevice();
            auto* reflection = GEnv.Render->GetShaderLoader()->GetCachedReflection("fsr_fg_prepare", ".cs");
            auto constants = GetPassResourceCache().GetOrCreateVolatileCB("FSR3FG", "Motion", sizeof(Fvector4), data.device);
            cmd->writeBuffer(constants, &data.motionParams, sizeof(data.motionParams));
            BindingSetBuilder binding(*reflection, nv, "FSR3FG.Depth");
            binding.Texture("t_Depth", graph.GetPhysicalTexture(data.depth))
                .Texture("t_Motion", graph.GetPhysicalTexture(data.motion))
                .TextureUAV("u_Depth", graph.GetPhysicalTexture(data.converted))
                .TextureUAV("u_Motion", graph.GetPhysicalTexture(data.convertedMotion))
                .ConstantBuffer("FsrFrameGenerationParams", constants);
            auto set = GetPassResourceCache().GetOrCreateBindingSet(binding.Build(), data.state->depthLayout, nv);
            R_ASSERT2(set, "FSR frame generation depth binding failed");
            ctx->SetComputePipeline(data.state->depthPipeline); ctx->SetComputeBindingSet(0, set);
            ctx->Dispatch((data.width + 7) / 8, (data.height + 7) / 8, 1);
            auto* fg = static_cast<D3D12Backend*>(GEnv.Backend)->GetFrameGeneration();
            fg->Prepare(cmd, graph.GetPhysicalTexture(data.converted), graph.GetPhysicalTexture(data.convertedMotion),
                graph.GetPhysicalTexture(data.hudless), data.jitterX, data.jitterY, data.reset);
        });
    return data.backbuffer;
#else
    return backbuffer;
#endif
}
}
