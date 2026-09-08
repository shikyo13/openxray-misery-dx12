// DetailPassSetup.cpp - Framegraph pass for detail objects (grass, etc.)
#include "stdafx.h"
#include "PassCommon.h"
#include "DetailPassSetup.h"
#include "ShaderConstants.h"
#include "Layers/xrRender/FGDetailManager.h"
#include "Layers/xrRender/FrameGraph/IPass.h"
#include "Layers/xrRender/FrameGraph/FrameGraph.h"
#include "Layers/xrRender/FrameGraph/RenderPassBuilder.h"
#include "Layers/xrRender/RenderContext/RenderDevice.h"
#include "Layers/xrRender/RenderContext/RenderContext.h"
#include "Layers/xrRender/Profiler/GPUProfiler.h"
#include "Layers/xrRender/FrameGraph/PassResourceCache.h"
#include "Layers/xrRender/FrameGraph/BindingSetBuilder.h"
#include "Layers/xrRender/FrameGraph/ShaderLoader.h"
#include "Layers/xrRender/ClusteredLightManager.h"

// Detail rendering console variables
extern ENGINE_API float ps_r__Detail_l_aniso;
extern ENGINE_API float ps_r__Detail_l_ambient;

// Phase 5: Grass wind tuning parameters (defined in xrEngine)
extern ENGINE_API float ps_r3_grass_wind_multiplier;
extern ENGINE_API float ps_r3_grass_wind_min;
extern ENGINE_API float ps_r3_grass_wind_lerp_rate;
extern ENGINE_API float ps_r3_grass_wind_displacement;
extern ENGINE_API float ps_r3_grass_interaction_displacement;

// Grass color parameters (defined in xrEngine)
extern ENGINE_API Fvector3 ps_r3_grass_color_tip;
extern ENGINE_API Fvector3 ps_r3_grass_color_base;
extern ENGINE_API float ps_r3_grass_color_variation;
extern ENGINE_API Fvector3 ps_r3_grass_sss_color;
extern ENGINE_API float ps_r3_grass_sss_intensity;
extern ENGINE_API Fvector3 ps_r3_grass_object_tints[64];

// Grass blade geometry parameters (defined in xrEngine)
extern ENGINE_API float ps_r3_grass_blade_width;
extern ENGINE_API float ps_r3_grass_blade_height;

namespace xray::render::fg
{
    extern int ps_r__detail_gpu;
}

namespace xray::render::fg::passes
{
using namespace framegraph;

void UpdateDetailWind(FGDetailManager* wind)
{
    if (g_pGamePersistent)
    {
        const auto& env = g_pGamePersistent->Environment().CurrentEnv;
        // Convert authored velocity (m/s, sometimes hundreds in mods) to
        // bounded bending pressure with a 10 m/s response scale.
        const float pressure = 1.0f - std::exp(-_max(env.wind_velocity, 0.0f) * 0.1f);
        const float target = _max(pressure * ps_r3_grass_wind_multiplier, ps_r3_grass_wind_min);
        const float dt = _max(Device.fTimeDelta, 0.0f);
        if (!wind->windStateReady) {
            wind->windSpeed = target;
            wind->windAngle = env.wind_direction;
            wind->windStateReady = true;
        } else {
            const float blend = 1.0f - std::exp(-_max(ps_r3_grass_wind_lerp_rate, 0.1f) * dt);
            wind->windSpeed += (target - wind->windSpeed) * blend;
            wind->windAngle += std::remainder(env.wind_direction - wind->windAngle, PI_MUL_2) * blend;
        }
        wind->windDirection.set(_cos(wind->windAngle), _sin(wind->windAngle));
        // Integrate a continuous, bounded scroll rate. Multiplying absolute
        // time by a changing wind speed jumps to unrelated noise samples.
        wind->windNoisePhase = std::fmod(wind->windNoisePhase + dt *
            (0.25f + 0.75f * clampr(wind->windSpeed, 0.0f, 1.0f)), 200.0f);
        if (strstr(Core.Params, "-weather_trace") && Device.dwFrame % 120 == 0)
            Msg("* [GrassWind] frame=%u velocity=%.3f pressure=%.4f phase=%.4f radians=%.4f",
                Device.dwFrame, env.wind_velocity, wind->windSpeed, wind->windNoisePhase, wind->windAngle);
    }
}

FGDetailManager::DetailFrameConstants BuildDetailFrameConstants(FGDetailManager* dm, const Fmatrix& viewProjection)
{
    // b3: DetailGlobals
    float windAngleDeg = rad2deg(dm->windAngle);
    float windSpeed = dm->windSpeed;

    FGDetailManager::DetailFrameConstants frameConstants;
    const float quant = 16384.0f;
    frameConstants.consts.set(1.0f / quant, 1.0f / quant, ps_r__Detail_l_aniso, ps_r__Detail_l_ambient);
    frameConstants.wave.set(1.0f / 5.0f, 1.0f / 7.0f, 1.0f / 3.0f, Device.fTimeGlobal);
    frameConstants.dir2D.set(dm->windDirection.x, dm->windDirection.y, 0.0f, 0.0f);
    frameConstants.dir2D_2.set(-dm->windDirection.y, dm->windDirection.x, 0.0f, 0.0f);
    frameConstants.viewProj = viewProjection;
    frameConstants.detail_params.set(
        float(dm->dtH.x_size()), float(dm->dtH.z_size()),
        float(dm->dtH.x_offs()), float(dm->dtH.z_offs()));
    frameConstants.g_wind_direction.set(windAngleDeg, windSpeed, dm->windNoisePhase, 0.0f);
    frameConstants.grass_wind_displacement = ps_r3_grass_wind_displacement;
    frameConstants.grass_interaction_displacement = ps_r3_grass_interaction_displacement;
    frameConstants.interaction_atlas_index = 0;
    frameConstants.perlin4d_texture_index = dm->perlin4dBindlessIndex;
    frameConstants.grass_color_tip.set(ps_r3_grass_color_tip.x, ps_r3_grass_color_tip.y, ps_r3_grass_color_tip.z, 0.0f);
    frameConstants.grass_color_base.set(ps_r3_grass_color_base.x, ps_r3_grass_color_base.y, ps_r3_grass_color_base.z, 0.0f);
    frameConstants.grass_sss_color.set(ps_r3_grass_sss_color.x, ps_r3_grass_sss_color.y, ps_r3_grass_sss_color.z, ps_r3_grass_sss_intensity);
    frameConstants.grass_color_variation = ps_r3_grass_color_variation;
    frameConstants.grass_blade_height = ps_r3_grass_blade_height;
    frameConstants.buildDetailsIndex = dm->buildDetailsBindlessIndex;
    frameConstants.buildDetailsPbrIndex = dm->buildDetailsPbrBindlessIndex;
    frameConstants.shadowRange.set(Device.vCameraPosition.x, Device.vCameraPosition.y,
        Device.vCameraPosition.z, ps_r_detail_shadow_distance);
    return frameConstants;
}

DefaultOutputLayout setupDetailPass(
    FrameGraph& fg,
    fg::RenderDevice* device,
    fg::FGDetailManager* detailManager,
    const DefaultOutputLayout& forwardInputs,
    u32 width,
    u32 height,
    xray::profiler::GPUProfiler* gpuProfiler
)
{
    if (detailManager && !detailManager->graphicsPipeline)
    {
        nvrhi::FramebufferInfo fbInfo;
        fbInfo.colorFormats.push_back(nvrhi::Format::RGBA16_FLOAT);
        fbInfo.colorFormats.push_back(nvrhi::Format::RGBA16_FLOAT);
        fbInfo.colorFormats.push_back(nvrhi::Format::RGBA8_UNORM);
        fbInfo.colorFormats.push_back(nvrhi::Format::RGBA16_FLOAT);
        fbInfo.depthFormat = nvrhi::Format::D32;
        detailManager->CreateGraphicsPipeline(device, fbInfo);
    }

    auto& passData = fg.addCallbackPass<DetailPassData>(
        "DetailDraw",
        [&, width, height, gpuProfiler](
            FrameGraph& builder, PassHandle passHandle, DetailPassData& data) {
            RenderPassBuilder passBuilder(builder, passHandle);
            ReadWorldShadowMaps(builder, passHandle);
            ReadSkyBackground(builder, passHandle);

            data.width = width;
            data.height = height;
            data.device = device;
            data.detailManager = detailManager;
            data.gpuProfiler = gpuProfiler;

            data.inputColor = passBuilder.read(forwardInputs.albedo);
            data.depth = passBuilder.readWrite(forwardInputs.depth, ResourceState::DepthStencilWrite);
            data.outputColor = passBuilder.write(forwardInputs.albedo);
            data.outputNormal = passBuilder.readWrite(forwardInputs.normal, ResourceState::RenderTarget);
            if (forwardInputs.baseColor.is_valid())
                data.baseColor = passBuilder.readWrite(forwardInputs.baseColor, ResourceState::RenderTarget);
            data.ambient = passBuilder.readWrite(forwardInputs.ambient, ResourceState::RenderTarget);

            data.outputs.albedo = data.outputColor;
            data.outputs.normal = data.outputNormal;
            data.outputs.baseColor = data.baseColor;
            data.outputs.ambient = data.ambient;
            data.outputs.depth = data.depth;
        },
        [](const DetailPassData& data, const FrameGraph& fg, fg::RenderContext* ctx)
        {
            ZoneScoped;
            ZoneName("DetailPass", 10);

            if (!data.detailManager)
                return;

            // Check if detail rendering is enabled
            if (!psDeviceFlags.is(rsDrawDetails))
                return;

            bool detailPipelineValid = (data.detailManager->instanceGenPipeline && data.detailManager->slotDataBuffer);
            if (!detailPipelineValid)
                return;

            // Get physical resources
            nvrhi::ITexture* colorTexture = fg.GetPhysicalTexture(data.outputColor);
            nvrhi::ITexture* depthTexture = fg.GetPhysicalTexture(data.depth);

            if (!colorTexture || !depthTexture)
                return;

            // Get command list (already opened by framegraph system)
            nvrhi::ICommandList* cmdList = ctx->GetCommandList();
            if (!cmdList)
                return;

            nvrhi::ITexture* normalTexture = fg.GetPhysicalTexture(data.outputNormal);
            auto* baseColorRT = data.baseColor.is_valid() ? fg.GetPhysicalTexture(data.baseColor) : nullptr;
            auto* ambientRT = fg.GetPhysicalTexture(data.ambient);

            nvrhi::FramebufferDesc fbDesc;
            fbDesc.addColorAttachment(colorTexture);
            if (normalTexture)
                fbDesc.addColorAttachment(normalTexture);
            if (baseColorRT)
                fbDesc.addColorAttachment(baseColorRT);
            fbDesc.addColorAttachment(ambientRT);
            fbDesc.setDepthAttachment(depthTexture);

            nvrhi::FramebufferHandle framebuffer = data.device->GetNVRHIDevice()->createFramebuffer(fbDesc);
            if (!framebuffer)
                return;

            if (!data.detailManager->graphicsPipeline)
                return;

            auto* dm = data.detailManager;
            auto* renderDevice = data.device;
            auto& cache = framegraph::GetPassResourceCache();

            auto staticGlobalsCB = cache.GetOrCreateVolatileCB("Frame", "StaticGlobals", sizeof(StaticGlobals), renderDevice);
            auto detailGlobalsCB = cache.GetOrCreateVolatileCB("Detail", "DetailGlobals", sizeof(FGDetailManager::DetailFrameConstants), renderDevice);
            auto dynLightCB = cache.GetOrCreateVolatileCB("Detail", "DynLight", 48, renderDevice);

            const auto frameConstants = BuildDetailFrameConstants(dm, Device.mFullTransform);
            cmdList->writeBuffer(detailGlobalsCB, &frameConstants, sizeof(frameConstants));

            u8 dummyLight[48] = {};
            cmdList->writeBuffer(dynLightCB, dummyLight, 48);

            // Update grass tints
            FGDetailManager::GrassObjectTint tintData[64];
            for (int i = 0; i < 64; i++)
            {
                tintData[i].r = ps_r3_grass_object_tints[i].x;
                tintData[i].g = ps_r3_grass_object_tints[i].y;
                tintData[i].b = ps_r3_grass_object_tints[i].z;
                tintData[i].pad = 1.0f;
            }
            cmdList->writeBuffer(dm->cachedGrassTintsBuffer, tintData, sizeof(tintData));

            // SM6.6 bindless: Get the descriptor table from D3D12 backend
            nvrhi::IBindingSet* bindlessTable = nullptr;
            auto* backend = data.device->GetBackend();
            if (backend) {
                bindlessTable = backend->GetBindlessDescriptorTable();
            }

            if (data.gpuProfiler)
                data.gpuProfiler->BeginPass(cmdList, "Details.Draw");

            auto* nvDev = data.device->GetNVRHIDevice();

            auto* shaderLoader = GEnv.Render->GetShaderLoader();
            auto* grassVsRefl = shaderLoader->GetCachedReflection("detail_gpu", ".vs");
            auto* grassPsRefl = shaderLoader->GetCachedReflection("detail_gpu", ".ps");

            auto makeGrassBindingSet = [&](nvrhi::BufferHandle visibleIndicesBuffer) {
                framegraph::BindingSetBuilder bsb(*grassVsRefl, *grassPsRefl, nvDev, "Detail.Grass");
                bsb.ConstantBuffer("static_globals", staticGlobalsCB);
                bsb.ConstantBuffer("DetailGlobals", detailGlobalsCB);
                bsb.ConstantBuffer("$Globals", dynLightCB);
                bsb.BufferSRV("visible_indices", visibleIndicesBuffer);
                bsb.BufferSRV("grass_object_tints", dm->cachedGrassTintsBuffer);
                bsb.BufferSRV("all_instances", dm->generatedInstancesBuffer);
                bsb.BufferSRV("slot_data", dm->slotDataBuffer);
                bsb.Texture("g_Perlin4D", dm->perlin4dTexture);
                bsb.BufferSRV("g_LightData", ClusteredLightManager::Instance().GetLightDataBuffer());
                bsb.BufferSRV("g_ClusterGrid", ClusteredLightManager::Instance().GetClusterGridBuffer());
                bsb.BufferSRV("g_LightIndexList", ClusteredLightManager::Instance().GetLightIndexListBuffer());
                auto bindDesc = bsb.Build();
                bindDesc.bindings.push_back(nvrhi::BindingSetItem::TypedBuffer_SRV(32, dm->cachedDummySlotIndirection));
                return cache.GetOrCreateBindingSet(bindDesc, dm->graphicsBindingLayout, nvDev);
            };

            auto* bbVsRefl = shaderLoader->GetCachedReflection("detail_billboard", ".vs");
            auto* bbPsRefl = shaderLoader->GetCachedReflection("detail_billboard", ".ps");

            auto makePulledBindingSet = [&](nvrhi::BufferHandle visibleIndicesBuffer, nvrhi::BindingLayoutHandle layout) {
                framegraph::BindingSetBuilder bsb(*bbVsRefl, *bbPsRefl, nvDev, "Detail.Billboard");
                bsb.ConstantBuffer("static_globals", staticGlobalsCB);
                bsb.ConstantBuffer("DetailGlobals", detailGlobalsCB);
                bsb.BufferSRV("visible_indices", visibleIndicesBuffer);
                bsb.BufferSRV("detail_models", dm->detailModelsBuffer);
                bsb.BufferSRV("pulled_vertices", dm->pulledVertexBuffer);
                bsb.BufferSRV("all_instances", dm->generatedInstancesBuffer);
                bsb.BufferSRV("slot_data", dm->slotDataBuffer);
                bsb.Texture("g_Perlin4D", dm->perlin4dTexture);
                bsb.BufferSRV("g_LightData", ClusteredLightManager::Instance().GetLightDataBuffer());
                bsb.BufferSRV("g_ClusterGrid", ClusteredLightManager::Instance().GetClusterGridBuffer());
                bsb.BufferSRV("g_LightIndexList", ClusteredLightManager::Instance().GetLightIndexListBuffer());
                return cache.GetOrCreateBindingSet(bsb.Build(), layout, nvDev);
            };

            bool billboardMode = !ps_r__detail_gpu;

            if (billboardMode && dm->billboardGraphicsPipeline && dm->visibleBillboardInstancesBuffer &&
                dm->billboardDrawArgsBuffer && dm->pulledIndexBuffer && dm->maxPulledIndexCount > 0)
            {
                nvrhi::BindingSetHandle bbBindingSet = makePulledBindingSet(dm->visibleBillboardInstancesBuffer, dm->billboardBindingLayout);

                nvrhi::GraphicsState state;
                state.framebuffer = framebuffer;
                state.viewport.addViewportAndScissorRect(nvrhi::Viewport((float)data.width, (float)data.height));
                state.pipeline = dm->billboardGraphicsPipeline;
                state.bindings = { bbBindingSet };
                if (bindlessTable)
                    state.addBindingSet(bindlessTable);
                state.indexBuffer = { dm->pulledIndexBuffer, nvrhi::Format::R16_UINT, 0 };
                state.indirectParams = dm->billboardDrawArgsBuffer;

                cmdList->setGraphicsState(state);
                cmdList->drawIndexedIndirect(0);
            }
            else
            {
                for (u32 lod = 0; lod < FGDetailManager::LOD_COUNT; lod++)
                {
                    nvrhi::BindingSetHandle bindingSet = makeGrassBindingSet(dm->visibleInstancesBuffer[lod]);

                    nvrhi::GraphicsState state;
                    state.framebuffer = framebuffer;
                    state.viewport.addViewportAndScissorRect(nvrhi::Viewport((float)data.width, (float)data.height));
                    state.pipeline = dm->graphicsPipeline;
                    state.bindings = { bindingSet };
                    if (bindlessTable)
                        state.addBindingSet(bindlessTable);
                    state.indexBuffer = { dm->bladeIndexBuffer[lod], nvrhi::Format::R16_UINT, 0 };
                    state.vertexBuffers = {{ dm->bladeVertexBuffer[lod], 0, 0 }};
                    state.indirectParams = dm->drawArgsBuffer[lod];

                    cmdList->setGraphicsState(state);
                    cmdList->drawIndexedIndirect(0);
                }
            }

            if (dm->decalGraphicsPipeline && dm->visibleDecalInstancesBuffer && dm->decalDrawArgsBuffer && dm->pulledIndexBuffer && dm->maxPulledIndexCount > 0)
            {
                auto* decalVsRefl = shaderLoader->GetCachedReflection("detail_decal", ".vs");
                auto* decalPsRefl = shaderLoader->GetCachedReflection("detail_decal", ".ps");
                framegraph::BindingSetBuilder decalBsb(*decalVsRefl, *decalPsRefl, nvDev, "Detail.Decal");
                decalBsb.ConstantBuffer("static_globals", staticGlobalsCB);
                decalBsb.ConstantBuffer("DetailGlobals", detailGlobalsCB);
                decalBsb.BufferSRV("visible_indices", dm->visibleDecalInstancesBuffer);
                decalBsb.BufferSRV("detail_models", dm->detailModelsBuffer);
                decalBsb.BufferSRV("decal_vertices", dm->pulledVertexBuffer);
                decalBsb.BufferSRV("all_instances", dm->generatedInstancesBuffer);
                decalBsb.BufferSRV("slot_data", dm->slotDataBuffer);
                decalBsb.BufferSRV("g_LightData", ClusteredLightManager::Instance().GetLightDataBuffer());
                decalBsb.BufferSRV("g_ClusterGrid", ClusteredLightManager::Instance().GetClusterGridBuffer());
                decalBsb.BufferSRV("g_LightIndexList", ClusteredLightManager::Instance().GetLightIndexListBuffer());
                nvrhi::BindingSetHandle decalBindingSet = cache.GetOrCreateBindingSet(decalBsb.Build(), dm->decalBindingLayout, nvDev);

                nvrhi::GraphicsState state;
                state.framebuffer = framebuffer;
                state.viewport.addViewportAndScissorRect(nvrhi::Viewport((float)data.width, (float)data.height));
                state.pipeline = dm->decalGraphicsPipeline;
                state.bindings = { decalBindingSet };
                if (bindlessTable)
                    state.addBindingSet(bindlessTable);
                state.indexBuffer = { dm->pulledIndexBuffer, nvrhi::Format::R16_UINT, 0 };
                state.indirectParams = dm->decalDrawArgsBuffer;

                cmdList->setGraphicsState(state);
                cmdList->drawIndexedIndirect(0);
            }

            if (data.gpuProfiler)
                data.gpuProfiler->EndPass(cmdList, "Details.Draw");
        }
    );

    DefaultOutputLayout outputs;
    outputs.albedo = passData.outputColor;
    outputs.normal = passData.outputNormal;
    outputs.baseColor = passData.baseColor;
    outputs.ambient = passData.ambient;
    outputs.depth = passData.depth;
    return outputs;
}

VirtualResourceHandle setupDetailMotionPass(FrameGraph& graph, fg::RenderDevice* device,
    FGDetailManager* dm, VirtualResourceHandle depth, VirtualResourceHandle motion,
    const Fmatrix& previousViewProjection, bool historyValid, DetailMotionPassState& state)
{
    // This is the authored MISERY detail path. Procedural blade history is separate.
    if (!dm || ps_r__detail_gpu || !dm->billboardGraphicsPipeline ||
        !dm->visibleBillboardInstancesBuffer || !dm->billboardDrawArgsBuffer ||
        !dm->pulledIndexBuffer || !dm->maxPulledIndexCount) return motion;
    auto* nv = device->GetNVRHIDevice();
    auto* loader = GEnv.Render->GetShaderLoader();
    auto& cache = GetPassResourceCache();
    if (!state.pipeline) {
        auto vs = loader->LoadVertexShader("detail_motion");
        auto ps = loader->LoadPixelShader("detail_motion");
        R_ASSERT2(vs.handle && ps.handle && vs.reflection && ps.reflection, "Detail motion shader compilation failed");
        state.layout = cache.GetOrCreateBindingLayoutFromReflection("Detail.Motion", *vs.reflection, *ps.reflection, nv);
        nvrhi::GraphicsPipelineDesc desc;
        desc.VS = vs.handle; desc.PS = ps.handle;
        desc.bindingLayouts = {state.layout, device->GetBackend()->GetBindlessLayout()};
        desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
        desc.renderState.depthStencilState.depthTestEnable = true;
        desc.renderState.depthStencilState.depthWriteEnable = false;
        desc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Equal;
        nvrhi::FramebufferInfo info;
        info.colorFormats = {nvrhi::Format::RG16_FLOAT};
        info.depthFormat = nvrhi::Format::D32;
        state.pipeline = nv->createGraphicsPipeline(desc, info);
        R_ASSERT2(state.pipeline, "Detail motion pipeline creation failed");
    }
    struct PassData {
        VirtualResourceHandle depth, motion;
        fg::RenderDevice* device;
        FGDetailManager* dm;
        DetailMotionPassState* state;
        Fmatrix previousVP;
        bool historyValid;
    };
    auto& pass = graph.addCallbackPass<PassData>("Motion Vectors.Grass",
        [&](FrameGraph& builder, PassHandle pass, PassData& data) {
            RenderPassBuilder pb(builder, pass);
            data.depth = pb.read(depth, ResourceState::DepthStencilRead);
            data.motion = pb.readWrite(motion, ResourceState::RenderTarget);
            data.device = device; data.dm = dm; data.state = &state;
            data.previousVP = previousViewProjection; data.historyValid = historyValid;
        }, [](const PassData& data, const FrameGraph& graph, fg::RenderContext* ctx) {
            auto* nv = data.device->GetNVRHIDevice();
            auto* cmd = ctx->GetCommandList();
            auto* dm = data.dm;
            auto& s = *data.state;
            auto& cache = GetPassResourceCache();
            const auto current = BuildDetailFrameConstants(dm, Device.mFullTransform);
            const bool continuous = data.historyValid && s.previousFrame + 1 == Device.dwFrame;
            struct MotionParams { Fmatrix previousVP; Fvector4 wind; Fvector4 controls; } params;
            static_assert(sizeof(params) == 96);
            params.previousVP = continuous ? data.previousVP : Device.mFullTransform;
            params.wind = continuous ? s.previousWind : current.g_wind_direction;
            params.controls.set(continuous ? s.previousDisplacement : current.grass_wind_displacement,
                ps_r_motion_debug == 3 ? 1.f : 0.f, float(dm->buildDetailsBindlessIndex), 0.f);
            auto currentCB = cache.GetOrCreateVolatileCB("Detail.Motion", "DetailGlobals", sizeof(current), data.device);
            auto motionCB = cache.GetOrCreateVolatileCB("Detail.Motion", "DetailMotionParams", sizeof(params), data.device);
            cmd->writeBuffer(currentCB, &current, sizeof(current));
            cmd->writeBuffer(motionCB, &params, sizeof(params));
            auto* loader = GEnv.Render->GetShaderLoader();
            BindingSetBuilder binding(*loader->GetCachedReflection("detail_motion", ".vs"),
                *loader->GetCachedReflection("detail_motion", ".ps"), nv, "Detail.Motion");
            binding.ConstantBuffer("DetailGlobals", currentCB).ConstantBuffer("DetailMotionParams", motionCB)
                .BufferSRV("visible_indices", dm->visibleBillboardInstancesBuffer)
                .BufferSRV("detail_models", dm->detailModelsBuffer)
                .BufferSRV("pulled_vertices", dm->pulledVertexBuffer)
                .BufferSRV("all_instances", dm->generatedInstancesBuffer)
                .Texture("g_Perlin4D", dm->perlin4dTexture);
            auto bindings = cache.GetOrCreateBindingSet(binding.Build(), s.layout, nv);
            R_ASSERT2(bindings, "Detail motion binding creation failed");
            auto* motion = graph.GetPhysicalTexture(data.motion);
            nvrhi::FramebufferDesc fb;
            fb.addColorAttachment(motion).setDepthAttachment(graph.GetPhysicalTexture(data.depth));
            auto framebuffer = cache.GetOrCreateFramebuffer("Detail.Motion", fb, nv);
            R_ASSERT2(framebuffer, "Detail motion framebuffer creation failed");
            nvrhi::GraphicsState draw;
            draw.framebuffer = framebuffer;
            draw.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(motion->getDesc().width), float(motion->getDesc().height)));
            draw.pipeline = s.pipeline;
            draw.bindings = {bindings, data.device->GetBackend()->GetBindlessDescriptorTable()};
            draw.indexBuffer = {dm->pulledIndexBuffer, nvrhi::Format::R16_UINT, 0};
            draw.indirectParams = dm->billboardDrawArgsBuffer;
            cmd->setGraphicsState(draw);
            cmd->drawIndexedIndirect(0);
            if (strstr(Core.Params, "-graphics_trace") && Device.dwFrame % 120 == 0)
                Msg("* [DetailMotion] frame=%u history=%u previous_phase=%.6f current_phase=%.6f",
                    Device.dwFrame, unsigned(continuous), params.wind.z, current.g_wind_direction.z);
            s.previousWind = current.g_wind_direction;
            s.previousDisplacement = current.grass_wind_displacement;
            s.previousFrame = Device.dwFrame;
        });
    return pass.motion;
}

} // namespace xray::render::fg::passes
