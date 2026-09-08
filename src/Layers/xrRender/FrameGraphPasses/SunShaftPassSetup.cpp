#include "stdafx.h"
#include "SunShaftPassSetup.h"
#include "SunShadowPassSetup.h"
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
const char* shaftShaders[] = {"sunshaft_generate", "sunshaft_blur", "sunshaft_blur", "sunshaft_resolve"};
const char* shaftPassNames[] = {"SunShafts.Generate", "SunShafts.BlurX", "SunShafts.BlurY", "SunShafts.Resolve"};
struct SunShaftConstants {
    Fmatrix inverseViewProjection;
    Fmatrix shadowMatrices[3];
    Fvector4 splits, camera, cameraDirection, sunColor, sunDirection, screen, sampling;
};
static_assert(sizeof(SunShaftConstants) == 368);
}

VirtualResourceHandle setupSunShaftPass(FrameGraph& graph, RenderDevice* device,
    VirtualResourceHandle color, VirtualResourceHandle depth, VirtualResourceHandle shadowMap,
    const SunShadowPassState& sun, u32 width, u32 height, SunShaftPassState& state)
{
    const auto& environment = g_pGamePersistent->Environment().CurrentEnv;
    const bool enabled = ps_r_sun_shafts && sun.enabled && shadowMap.is_valid() &&
        environment.m_fSunShaftsIntensity > .0001f && ps_r2_sun_lumscale > 0.f;
    const u32 quality = clampr(ps_r_sun_shafts, 1u, 3u);
    const u32 reduction = quality == 1 ? 4 : 2;
    const u32 samples = quality == 3 ? 40 : 20;
    const u32 reducedWidth = (width + reduction - 1) / reduction;
    const u32 reducedHeight = (height + reduction - 1) / reduction;
    if (strstr(Core.Params, "-graphics_trace") && Device.dwFrame % 120 == 0)
        Msg("* [SunShafts] frame=%u active=%u quality=%u intensity=%.4f sun=%.4f,%.4f,%.4f size=%ux%u samples=%u",
            Device.dwFrame, enabled ? 1u : 0u, ps_r_sun_shafts, environment.m_fSunShaftsIntensity,
            environment.sun_dir.x, environment.sun_dir.y, environment.sun_dir.z,
            reducedWidth, reducedHeight, samples);
    if (!enabled) return color;

    auto& cache = GetPassResourceCache();
    auto* nv = device->GetNVRHIDevice();
    for (u32 i = 0; i < 4; ++i) {
        if (state.pipelines[i]) continue;
        auto cs = GEnv.Render->GetShaderLoader()->LoadComputeShader(shaftShaders[i]);
        R_ASSERT2(cs.handle && cs.reflection, "Sun shaft shader compilation failed");
        state.layouts[i] = cache.GetOrCreateBindingLayoutFromReflection(shaftPassNames[i], *cs.reflection, nv);
        nvrhi::ComputePipelineDesc pipeline;
        pipeline.CS = cs.handle;
        pipeline.bindingLayouts = {state.layouts[i]};
        state.pipelines[i] = cache.GetOrCreateComputePipeline(shaftPassNames[i], pipeline, nv);
        R_ASSERT2(state.pipelines[i], "Sun shaft pipeline creation failed");
    }
    state.constants = cache.GetOrCreateVolatileCB("SunShafts", "SunShaftParams", sizeof(SunShaftConstants), device);
    SunShaftConstants constants{};
    constants.inverseViewProjection = Device.mInvFullTransform;
    for (u32 i = 0; i < 3; ++i) constants.shadowMatrices[i] = sun.viewProjection[i];
    constants.splits = sun.splits;
    constants.camera.set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, 0.f);
    constants.cameraDirection.set(Device.vCameraDirection.x, Device.vCameraDirection.y, Device.vCameraDirection.z, 0.f);
    constants.sunColor.set(environment.sun_color.x * ps_r2_sun_lumscale,
        environment.sun_color.y * ps_r2_sun_lumscale, environment.sun_color.z * ps_r2_sun_lumscale,
        environment.m_fSunShaftsIntensity);
    Fvector direction = environment.sun_dir;
    direction.normalize_safe();
    constants.sunDirection.set(-direction.x, -direction.y, -direction.z, 0.f);
    constants.screen.set(float(width), float(height), 1.f / width, 1.f / height);
    constants.sampling.set(float(samples), float(reduction), 0.f, 0.f);

    VirtualResourceHandle previous;
    for (u32 i = 0; i < 4; ++i) {
        const bool resolve = i == 3;
        const u32 passWidth = resolve ? width : reducedWidth;
        const u32 passHeight = resolve ? height : reducedHeight;
        ResourceDesc desc;
        desc.debugName = shaftPassNames[i];
        desc.width = passWidth;
        desc.height = passHeight;
        desc.format = resolve ? nvrhi::Format::RGBA16_FLOAT : nvrhi::Format::RG16_FLOAT;
        desc.isUAV = true;
        desc.isRenderTarget = resolve;
        desc.isTransient = true;
        auto target = graph.CreateTexture(shaftPassNames[i], desc);
        struct PassData {
            VirtualResourceHandle depth, shadow, shafts, color, output;
            SunShaftPassState* state;
            RenderDevice* device;
            SunShaftConstants constants;
            u32 index, width, height;
        };
        auto& data = graph.addCallbackPass<PassData>(shaftPassNames[i],
            [&](FrameGraph& builder, PassHandle pass, PassData& data) {
                RenderPassBuilder pb(builder, pass);
                if (i == 0 || resolve) data.depth = pb.read(depth, ResourceState::ShaderResource);
                if (i == 0) data.shadow = pb.read(shadowMap, ResourceState::ShaderResource);
                if (previous.is_valid()) data.shafts = pb.read(previous, ResourceState::ShaderResource);
                if (resolve) data.color = pb.read(color, ResourceState::ShaderResource);
                data.output = pb.write(target, ResourceState::UnorderedAccess);
                data.state = &state;
                data.device = device;
                data.constants = constants;
                data.constants.sampling.z = i == 2 ? 1.f : 0.f;
                data.index = i;
                data.width = passWidth;
                data.height = passHeight;
            },
            [](const PassData& data, const FrameGraph& graph, RenderContext* ctx) {
                auto* nv = data.device->GetNVRHIDevice();
                auto* reflection = GEnv.Render->GetShaderLoader()->GetCachedReflection(shaftShaders[data.index], ".cs");
                BindingSetBuilder bindings(*reflection, nv, shaftPassNames[data.index]);
                bindings.ConstantBuffer("SunShaftParams", data.state->constants)
                    .TextureUAV("u_Output", graph.GetPhysicalTexture(data.output));
                if (data.depth.is_valid()) bindings.Texture("t_Depth", graph.GetPhysicalTexture(data.depth));
                if (data.shadow.is_valid()) bindings.Texture("t_Shadow", graph.GetPhysicalTexture(data.shadow));
                if (data.shafts.is_valid()) bindings.Texture("t_Shafts", graph.GetPhysicalTexture(data.shafts));
                if (data.color.is_valid()) bindings.Texture("t_Color", graph.GetPhysicalTexture(data.color));
                auto set = GetPassResourceCache().GetOrCreateBindingSet(bindings.Build(), data.state->layouts[data.index], nv);
                R_ASSERT2(set, "Sun shaft binding set creation failed");
                ctx->GetCommandList()->writeBuffer(data.state->constants, &data.constants, sizeof(data.constants));
                ctx->SetComputePipeline(data.state->pipelines[data.index]);
                ctx->SetComputeBindingSet(0, set);
                ctx->Dispatch((data.width + 7) / 8, (data.height + 7) / 8, 1);
            });
        previous = data.output;
    }
    return previous;
}
}
