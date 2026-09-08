#include "stdafx.h"
#include "AntialiasingPassSetup.h"
#include "Layers/xrRender/FrameGraph/BindingSetBuilder.h"
#include "Layers/xrRender/FrameGraph/PassResourceCache.h"
#include "Layers/xrRender/FrameGraph/RenderPassBuilder.h"
#include "Layers/xrRender/FrameGraph/ShaderLoader.h"
#include "Layers/xrRender/RenderContext/RenderContext.h"
#include "Layers/xrRender/RenderContext/RenderDevice.h"
#include "Layers/xrRender/xrRender_console.h"

namespace xray::render::fg::passes {
using namespace framegraph;
VirtualResourceHandle setupAntialiasingPass(FrameGraph& graph, fg::RenderDevice* device,
    VirtualResourceHandle color, u32 width, u32 height, AntialiasingPassState& state)
{
    if (ps_r_aa != 1) return color;
    auto& cache = GetPassResourceCache();
    auto* nv = device->GetNVRHIDevice();
    if (!state.pipeline) {
        auto cs = GEnv.Render->GetShaderLoader()->LoadComputeShader("antialiasing");
        R_ASSERT2(cs.handle && cs.reflection, "Antialiasing shader compilation failed");
        state.layout = cache.GetOrCreateBindingLayoutFromReflection("Antialiasing", *cs.reflection, nv);
        nvrhi::ComputePipelineDesc desc;
        desc.CS = cs.handle;
        desc.bindingLayouts = {state.layout};
        state.pipeline = cache.GetOrCreateComputePipeline("Antialiasing", desc, nv);
        R_ASSERT2(state.pipeline, "Antialiasing pipeline creation failed");
    }
    state.constants = cache.GetOrCreateVolatileCB("Antialiasing", "AAParams", 32, device);
    ResourceDesc desc;
    desc.type = ResourceDesc::Type::Texture2D;
    desc.debugName = "rt_Antialiasing";
    desc.width = width;
    desc.height = height;
    desc.format = nvrhi::Format::RGBA16_FLOAT;
    desc.isRenderTarget = true;
    desc.isUAV = true;
    desc.isTransient = true;
    auto target = graph.CreateTexture(desc.debugName.c_str(), desc);
    struct PassData {
        VirtualResourceHandle color, output;
        AntialiasingPassState* state;
        fg::RenderDevice* device;
        u32 width, height;
    };
    auto& data = graph.addCallbackPass<PassData>("Antialiasing",
        [&](FrameGraph& builder, PassHandle pass, PassData& data) {
            RenderPassBuilder pb(builder, pass);
            data.color = pb.read(color, ResourceState::ShaderResource);
            data.output = pb.write(target, ResourceState::UnorderedAccess);
            data.state = &state;
            data.device = device;
            data.width = width;
            data.height = height;
        },
        [](const PassData& data, const FrameGraph& graph, fg::RenderContext* ctx) {
            auto* nv = data.device->GetNVRHIDevice();
            auto* reflection = GEnv.Render->GetShaderLoader()->GetCachedReflection("antialiasing", ".cs");
            BindingSetBuilder bindings(*reflection, nv, "Antialiasing");
            bindings.ConstantBuffer("AAParams", data.state->constants)
                .Texture("t_Color", graph.GetPhysicalTexture(data.color))
                .TextureUAV("u_Output", graph.GetPhysicalTexture(data.output));
            auto set = GetPassResourceCache().GetOrCreateBindingSet(bindings.Build(), data.state->layout, nv);
            R_ASSERT2(set, "Antialiasing binding set creation failed");
            Fvector4 constants[2];
            constants[0].set(float(data.width), float(data.height), 1.f / data.width, 1.f / data.height);
            constants[1].set(.65f, .125f, .0312f, 0.f);
            ctx->GetCommandList()->writeBuffer(data.state->constants, constants, sizeof(constants));
            ctx->SetComputePipeline(data.state->pipeline);
            ctx->SetComputeBindingSet(0, set);
            ctx->Dispatch((data.width + 7) / 8, (data.height + 7) / 8, 1);
        });
    return data.output;
}
}
