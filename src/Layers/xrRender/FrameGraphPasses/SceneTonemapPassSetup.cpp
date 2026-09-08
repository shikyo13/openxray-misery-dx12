#include "stdafx.h"
#include "SceneTonemapPassSetup.h"
#include "Layers/xrRender/FrameGraph/BindingSetBuilder.h"
#include "Layers/xrRender/FrameGraph/PassResourceCache.h"
#include "Layers/xrRender/FrameGraph/RenderPassBuilder.h"
#include "Layers/xrRender/FrameGraph/ShaderLoader.h"
#include "Layers/xrRender/RenderContext/RenderContext.h"
#include "Layers/xrRender/RenderContext/RenderDevice.h"

namespace xray::render::fg::passes {
using namespace framegraph;
VirtualResourceHandle setupSceneTonemapPass(FrameGraph& graph, fg::RenderDevice* device,
    VirtualResourceHandle color, VirtualResourceHandle exposure,
    u32 width, u32 height, SceneTonemapPassState& state)
{
    auto& cache = GetPassResourceCache();
    auto* nv = device->GetNVRHIDevice();
    if (!state.pipeline) {
        auto cs = GEnv.Render->GetShaderLoader()->LoadComputeShader("scene_tonemap");
        R_ASSERT2(cs.handle && cs.reflection, "Scene tone mapping shader compilation failed");
        state.layout = cache.GetOrCreateBindingLayoutFromReflection("SceneTonemap", *cs.reflection, nv);
        nvrhi::ComputePipelineDesc pipeline;
        pipeline.CS = cs.handle;
        pipeline.bindingLayouts = {state.layout};
        state.pipeline = cache.GetOrCreateComputePipeline("SceneTonemap", pipeline, nv);
        R_ASSERT2(state.pipeline, "Scene tone mapping pipeline creation failed");
    }
    ResourceDesc desc;
    desc.debugName = "rt_SceneTonemap";
    desc.width = width;
    desc.height = height;
    desc.format = nvrhi::Format::RGBA16_FLOAT;
    desc.isRenderTarget = true;
    desc.isUAV = true;
    auto target = graph.CreateTexture(desc.debugName.c_str(), desc);
    struct PassData {
        VirtualResourceHandle color, exposure, output;
        SceneTonemapPassState* state;
        fg::RenderDevice* device;
        u32 width, height;
    };
    auto& data = graph.addCallbackPass<PassData>("SceneTonemap",
        [&](FrameGraph& builder, PassHandle pass, PassData& data) {
            RenderPassBuilder pb(builder, pass);
            data.color = pb.read(color);
            data.exposure = pb.read(exposure);
            data.output = pb.write(target, ResourceState::UnorderedAccess);
            data.state = &state;
            data.device = device;
            data.width = width;
            data.height = height;
        },
        [](const PassData& data, const FrameGraph& graph, fg::RenderContext* ctx) {
            auto* nv = data.device->GetNVRHIDevice();
            auto* reflection = GEnv.Render->GetShaderLoader()->GetCachedReflection("scene_tonemap", ".cs");
            BindingSetBuilder bindings(*reflection, nv, "SceneTonemap");
            bindings.Texture("t_Color", graph.GetPhysicalTexture(data.color))
                .Texture("t_Exposure", graph.GetPhysicalTexture(data.exposure))
                .TextureUAV("u_Output", graph.GetPhysicalTexture(data.output));
            auto set = GetPassResourceCache().GetOrCreateBindingSet(bindings.Build(), data.state->layout, nv);
            R_ASSERT2(set, "Scene tone mapping binding set creation failed");
            ctx->SetComputePipeline(data.state->pipeline);
            ctx->SetComputeBindingSet(0, set);
            ctx->Dispatch((data.width + 7) / 8, (data.height + 7) / 8, 1);
        });
    return data.output;
}
}
