#include "stdafx.h"
#include "AmbientOcclusionPassSetup.h"
#include "Layers/xrRender/FrameGraph/BindingSetBuilder.h"
#include "Layers/xrRender/FrameGraph/PassResourceCache.h"
#include "Layers/xrRender/FrameGraph/RenderPassBuilder.h"
#include "Layers/xrRender/FrameGraph/ShaderLoader.h"
#include "Layers/xrRender/RenderContext/RenderContext.h"
#include "Layers/xrRender/RenderContext/RenderDevice.h"
#include "Layers/xrRender/xrRender_console.h"

namespace xray::render::fg::passes {
using namespace framegraph;
namespace {
const char* shaders[] = { "ssao_generate", "ssao_blur", "ssao_blur", "ssao_resolve" };
const char* names[] = { "SSAO.Generate", "SSAO.BlurX", "SSAO.BlurY", "SSAO.Resolve" };
struct AOConstants {
    Fmatrix inverseViewProjection;
    Fmatrix view;
    Fvector4 screen;
    Fvector4 settings; // world radius, strength, angular bias, projected radius scale
    Fvector4 controls; // directions, steps, blur axis, debug view
};
static_assert(sizeof(AOConstants) == 176);
}

DefaultOutputLayout setupAmbientOcclusionPass(FrameGraph& graph, fg::RenderDevice* device,
    const DefaultOutputLayout& inputs, u32 width, u32 height, AmbientOcclusionPassState& state)
{
    if (ps_r_ssao == 0 || !inputs.ambient.is_valid()) return inputs;
    auto& cache = GetPassResourceCache();
    auto* nv = device->GetNVRHIDevice();
    for (u32 i = 0; i < 4; ++i) {
        if (state.pipelines[i]) continue;
        auto cs = GEnv.Render->GetShaderLoader()->LoadComputeShader(shaders[i]);
        R_ASSERT2(cs.handle && cs.reflection, "SSAO shader compilation failed");
        state.layouts[i] = cache.GetOrCreateBindingLayoutFromReflection(names[i], *cs.reflection, nv);
        nvrhi::ComputePipelineDesc desc;
        desc.CS = cs.handle;
        desc.bindingLayouts = {state.layouts[i]};
        state.pipelines[i] = cache.GetOrCreateComputePipeline(names[i], desc, nv);
        R_ASSERT2(state.pipelines[i], "SSAO pipeline creation failed");
    }
    state.constants = cache.GetOrCreateVolatileCB("SSAO", "SSAOParams", sizeof(AOConstants), device);
    AOConstants constants{};
    constants.inverseViewProjection = Device.mInvFullTransform;
    constants.view = Device.mView;
    constants.screen.set(float(width), float(height), 1.f / width, 1.f / height);
    constants.settings.set(ps_r_ssao_radius, ps_r_ssao_strength, .08f,
        .5f * height * Device.mProject._22);
    const u32 quality = clampr(ps_r_ssao, 1u, 4u) - 1;
    const float directions[] = {4, 6, 8, 12};
    const float steps[] = {2, 3, 4, 4};
    constants.controls.set(directions[quality], steps[quality], 0.f, float(ps_r_ssao_debug));
    VirtualResourceHandle previous;
    DefaultOutputLayout outputs = inputs;
    for (u32 i = 0; i < 4; ++i) {
        const bool resolve = i == 3;
        const u32 passWidth = resolve ? width : (width + 1) / 2;
        const u32 passHeight = resolve ? height : (height + 1) / 2;
        ResourceDesc desc;
        desc.type = ResourceDesc::Type::Texture2D;
        desc.debugName = names[i];
        desc.width = passWidth;
        desc.height = passHeight;
        desc.format = resolve ? nvrhi::Format::RGBA16_FLOAT : nvrhi::Format::RG16_FLOAT;
        desc.isUAV = true;
        // Later graphics passes composite into the resolved color.
        desc.isRenderTarget = resolve;
        desc.isTransient = true;
        auto target = graph.CreateTexture(names[i], desc);
        struct PassData {
            VirtualResourceHandle depth, normal, ao, color, ambient, output;
            AmbientOcclusionPassState* state;
            fg::RenderDevice* device;
            AOConstants constants;
            u32 index, width, height;
        };
        auto& data = graph.addCallbackPass<PassData>(names[i],
            [&](FrameGraph& builder, PassHandle pass, PassData& data) {
                RenderPassBuilder pb(builder, pass);
                if (i == 0 || resolve) data.depth = pb.read(inputs.depth, ResourceState::ShaderResource);
                data.normal = pb.read(inputs.normal, ResourceState::ShaderResource);
                if (previous.is_valid()) data.ao = pb.read(previous, ResourceState::ShaderResource);
                if (resolve) {
                    data.color = pb.read(inputs.albedo, ResourceState::ShaderResource);
                    data.ambient = pb.read(inputs.ambient, ResourceState::ShaderResource);
                }
                data.output = pb.write(target, ResourceState::UnorderedAccess);
                data.state = &state;
                data.device = device;
                data.constants = constants;
                data.constants.controls.z = i == 2 ? 1.f : 0.f;
                data.index = i;
                data.width = passWidth;
                data.height = passHeight;
            },
            [](const PassData& data, const FrameGraph& graph, fg::RenderContext* ctx) {
                auto* nv = data.device->GetNVRHIDevice();
                auto* reflection = GEnv.Render->GetShaderLoader()->GetCachedReflection(shaders[data.index], ".cs");
                BindingSetBuilder bindings(*reflection, nv, names[data.index]);
                bindings.ConstantBuffer("SSAOParams", data.state->constants)
                    .Texture("t_Normal", graph.GetPhysicalTexture(data.normal))
                    .TextureUAV("u_Output", graph.GetPhysicalTexture(data.output));
                if (data.depth.is_valid()) bindings.Texture("t_Depth", graph.GetPhysicalTexture(data.depth));
                if (data.ao.is_valid()) bindings.Texture("t_AO", graph.GetPhysicalTexture(data.ao));
                if (data.color.is_valid()) {
                    bindings.Texture("t_Color", graph.GetPhysicalTexture(data.color));
                    bindings.Texture("t_Ambient", graph.GetPhysicalTexture(data.ambient));
                }
                auto set = GetPassResourceCache().GetOrCreateBindingSet(bindings.Build(), data.state->layouts[data.index], nv);
                R_ASSERT2(set, "SSAO binding set creation failed");
                ctx->GetCommandList()->writeBuffer(data.state->constants, &data.constants, sizeof(data.constants));
                ctx->SetComputePipeline(data.state->pipelines[data.index]);
                ctx->SetComputeBindingSet(0, set);
                ctx->Dispatch((data.width + 7) / 8, (data.height + 7) / 8, 1);
            });
        previous = data.output;
    }
    outputs.albedo = previous;
    return outputs;
}
}
