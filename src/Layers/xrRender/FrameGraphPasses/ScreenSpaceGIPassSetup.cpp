#include "stdafx.h"
#include "ScreenSpaceGIPassSetup.h"
#include "Layers/xrRender/FrameGraph/BindingSetBuilder.h"
#include "Layers/xrRender/FrameGraph/PassResourceCache.h"
#include "Layers/xrRender/FrameGraph/RenderPassBuilder.h"
#include "Layers/xrRender/FrameGraph/ShaderLoader.h"
#include "Layers/xrRender/RenderContext/RenderContext.h"
#include "Layers/xrRender/RenderContext/RenderDevice.h"
#include "Layers/xrRender/xrRender_console.h"
#include "xrEngine/IGame_Persistent.h"

namespace xray::render::fg::passes {
using namespace framegraph;
namespace {
const char* giShaders[] = {"ssgi_generate", "ssgi_blur", "ssgi_blur", "ssgi_resolve"};
const char* giNames[] = {"SSGI.Generate", "SSGI.BlurX", "SSGI.BlurY", "SSGI.Resolve"};
struct GIConstants {
    Fmatrix inverseViewProjection, view, viewProjection;
    Fvector4 screen, camera, fog, fogColor, settings, controls;
};
static_assert(sizeof(GIConstants) == 288);
}

DefaultOutputLayout setupScreenSpaceGIPass(FrameGraph& graph, RenderDevice* device,
    const DefaultOutputLayout& inputs, VirtualResourceHandle sourceColor,
    VirtualResourceHandle sky, u32 width, u32 height, ScreenSpaceGIPassState& state)
{
    const bool enabled = ps_r_ssgi && inputs.ambient.is_valid() && inputs.baseColor.is_valid();
    // Quarter resolution bounds low/medium cost; high retains more spatial detail.
    const u32 samples[] = {8, 16, 24};
    const u32 quality = u32(clampr(ps_r_ssgi, 1, 3));
    const u32 scale = quality == 3 ? 2u : 4u;
    if (strstr(Core.Params, "-graphics_trace") && Device.dwFrame % 120 == 0)
        Msg("* [SSGI] frame=%u active=%u quality=%d samples=%u radius=%.3f strength=%.3f debug=%d size=%ux%u",
            Device.dwFrame, enabled ? 1u : 0u, ps_r_ssgi, samples[quality - 1],
            ps_r_ssgi_radius, ps_r_ssgi_strength, ps_r_ssgi_debug, (width + scale - 1) / scale, (height + scale - 1) / scale);
    if (!enabled) return inputs;

    auto& cache = GetPassResourceCache();
    auto* nv = device->GetNVRHIDevice();
    for (u32 i = 0; i < 4; ++i) {
        if (state.pipelines[i]) continue;
        auto cs = GEnv.Render->GetShaderLoader()->LoadComputeShader(giShaders[i]);
        R_ASSERT2(cs.handle && cs.reflection, "SSGI shader compilation failed");
        state.layouts[i] = cache.GetOrCreateBindingLayoutFromReflection(giNames[i], *cs.reflection, nv);
        nvrhi::ComputePipelineDesc desc;
        desc.CS = cs.handle;
        desc.bindingLayouts = {state.layouts[i]};
        state.pipelines[i] = cache.GetOrCreateComputePipeline(giNames[i], desc, nv);
        R_ASSERT2(state.pipelines[i], "SSGI pipeline creation failed");
    }
    state.constants = cache.GetOrCreateVolatileCB("SSGI", "SSGIParams", sizeof(GIConstants), device);
    const auto& env = g_pGamePersistent->Environment().CurrentEnv;
    GIConstants constants{};
    constants.inverseViewProjection = Device.mInvFullTransform;
    constants.view = Device.mView;
    constants.viewProjection = Device.mFullTransform;
    constants.screen.set(float(width), float(height), 1.f / width, 1.f / height);
    constants.camera.set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, 0.f);
    const float fogRange = env.fog_far - env.fog_near;
    constants.fog.set(env.fog_near, fogRange > .001f ? 1.f / fogRange : 0.f, 0.f, 0.f);
    constants.fogColor.set(env.fog_color.x, env.fog_color.y, env.fog_color.z, 0.f);
    constants.settings.set(ps_r_ssgi_radius, ps_r_ssgi_strength, float(samples[quality - 1]), .03f);
    constants.controls.set(0.f, float(ps_r_ssgi_debug), float(scale), 0.f);

    VirtualResourceHandle previous;
    auto outputs = inputs;
    for (u32 i = 0; i < 4; ++i) {
        const bool resolve = i == 3;
        const u32 passWidth = resolve ? width : (width + scale - 1) / scale;
        const u32 passHeight = resolve ? height : (height + scale - 1) / scale;
        ResourceDesc desc;
        desc.type = ResourceDesc::Type::Texture2D;
        desc.debugName = giNames[i];
        desc.width = passWidth;
        desc.height = passHeight;
        desc.format = nvrhi::Format::RGBA16_FLOAT;
        desc.isUAV = true;
        desc.isRenderTarget = resolve;
        desc.isTransient = true;
        auto target = graph.CreateTexture(giNames[i], desc);
        struct PassData {
            VirtualResourceHandle depth, normal, color, ambient, baseColor, sky, indirect, output;
            ScreenSpaceGIPassState* state;
            RenderDevice* device;
            GIConstants constants;
            u32 index, width, height;
        };
        auto& data = graph.addCallbackPass<PassData>(giNames[i],
            [&](FrameGraph& builder, PassHandle pass, PassData& data) {
                RenderPassBuilder pb(builder, pass);
                data.normal = pb.read(inputs.normal, ResourceState::ShaderResource);
                if (i == 0 || resolve) {
                    data.depth = pb.read(inputs.depth, ResourceState::ShaderResource);
                    data.color = pb.read(i == 0 ? sourceColor : inputs.albedo, ResourceState::ShaderResource);
                }
                if (i == 0) {
                    data.ambient = pb.read(inputs.ambient, ResourceState::ShaderResource);
                    data.sky = pb.read(sky, ResourceState::ShaderResource);
                }
                if (resolve) data.baseColor = pb.read(inputs.baseColor, ResourceState::ShaderResource);
                if (previous.is_valid()) data.indirect = pb.read(previous, ResourceState::ShaderResource);
                data.output = pb.write(target, ResourceState::UnorderedAccess);
                data.state = &state;
                data.device = device;
                data.constants = constants;
                data.constants.controls.x = i == 2 ? 1.f : 0.f;
                data.index = i;
                data.width = passWidth;
                data.height = passHeight;
            },
            [](const PassData& data, const FrameGraph& graph, RenderContext* ctx) {
                auto* nv = data.device->GetNVRHIDevice();
                auto* reflection = GEnv.Render->GetShaderLoader()->GetCachedReflection(giShaders[data.index], ".cs");
                BindingSetBuilder bindings(*reflection, nv, giNames[data.index]);
                bindings.ConstantBuffer("SSGIParams", data.state->constants)
                    .Texture("t_Normal", graph.GetPhysicalTexture(data.normal))
                    .TextureUAV("u_Output", graph.GetPhysicalTexture(data.output));
                if (data.depth.is_valid()) bindings.Texture("t_Depth", graph.GetPhysicalTexture(data.depth));
                if (data.color.is_valid()) bindings.Texture("t_Color", graph.GetPhysicalTexture(data.color));
                if (data.ambient.is_valid()) bindings.Texture("t_Ambient", graph.GetPhysicalTexture(data.ambient));
                if (data.sky.is_valid()) bindings.Texture("t_Sky", graph.GetPhysicalTexture(data.sky));
                if (data.baseColor.is_valid()) bindings.Texture("t_BaseColor", graph.GetPhysicalTexture(data.baseColor));
                if (data.indirect.is_valid()) bindings.Texture("t_Indirect", graph.GetPhysicalTexture(data.indirect));
                auto set = GetPassResourceCache().GetOrCreateBindingSet(bindings.Build(), data.state->layouts[data.index], nv);
                R_ASSERT2(set, "SSGI binding set creation failed");
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
