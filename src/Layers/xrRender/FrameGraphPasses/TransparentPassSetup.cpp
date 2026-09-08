#include "stdafx.h"
#include "TransparentPassSetup.h"
#include "ShaderConstants.h"
#include "Layers/xrRender/FrameGraph/FrameGraph.h"
#include "Layers/xrRender/FrameGraph/IPass.h"
#include "Layers/xrRender/FrameGraph/RenderPassBuilder.h"
#include "Layers/xrRender/FrameGraph/ShaderLoader.h"
#include "Layers/xrRender/RenderContext/RenderContext.h"
#include "Layers/xrRender/RenderContext/RenderDevice.h"
#include "Layers/xrRender/Backend/D3D12Backend.h"
#include "Layers/xrRender/Bindless/MaterialBuffer.h"
#include "Layers/xrRender/Bindless/VariantTextureBuffer.h"
#include "Layers/xrRender/ShaderVariant/VariantPSOCache.h"
#include "Layers/xrRender/FrameGraph/PassResourceCache.h"
#include "Layers/xrRender/FrameGraph/BindingSetBuilder.h"
#include "PassCommon.h"
#include "Layers/xrRender/ClusteredLightManager.h"
#include "Layers/xrRender/fgEnvironmentRender.h"
#include "Layers/xrRender/xrRender_console.h"

namespace xray::render::fg::passes {

void InitializeTransparentResources(fg::RenderDevice* device, const nvrhi::FramebufferInfoEx& fbInfo, TransparentPassState& state)
{
    if (state.initialized)
        return;

    nvrhi::IDevice* nvDevice = device->GetNVRHIDevice();
    if (!nvDevice)
        return;

    auto* shaderLoader = GEnv.Render->GetShaderLoader();
    if (!shaderLoader)
        return;

    auto vsResult = shaderLoader->LoadVertexShader("bindless_transparent", "main");
    auto psResult = shaderLoader->LoadPixelShader("bindless_transparent", "main");
    if (!vsResult.handle || !psResult.handle)
        return;

    state.vs = vsResult.handle;
    state.ps = psResult.handle;

    auto& cache = framegraph::GetPassResourceCache();
    state.layout = cache.GetOrCreateBindingLayoutFromReflection("TransparentPass", *vsResult.reflection, *psResult.reflection, nvDevice);
    if (!state.layout)
        return;

    u32 attrCount = 0;
    auto* attrs = GetUnifiedVertexAttributes(attrCount);
    state.inputLayout = nvDevice->createInputLayout(attrs, attrCount, state.vs);

    auto drawIndexBuffer = GetOrCreateDrawIndexBuffer("TransparentPass", nvDevice);
    if (!drawIndexBuffer)
        return;

    nvrhi::GraphicsPipelineDesc pipeDesc;
    pipeDesc.VS = state.vs;
    pipeDesc.PS = state.ps;
    pipeDesc.inputLayout = state.inputLayout;

    auto* backend = device->GetBackend();
    nvrhi::IBindingLayout* bindlessLayout = backend ? backend->GetBindlessLayout() : nullptr;
    if (bindlessLayout)
        pipeDesc.bindingLayouts = { state.layout, bindlessLayout };
    else
        pipeDesc.bindingLayouts = { state.layout };

    pipeDesc.primType = nvrhi::PrimitiveType::TriangleList;
    pipeDesc.renderState.depthStencilState.depthTestEnable = true;
    pipeDesc.renderState.depthStencilState.depthWriteEnable = false;
    pipeDesc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::GreaterOrEqual;
    pipeDesc.renderState.rasterState.frontCounterClockwise = false;
    pipeDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::Back;

    auto& rt0 = pipeDesc.renderState.blendState.targets[0];
    rt0.blendEnable = true;
    rt0.srcBlend = nvrhi::BlendFactor::SrcAlpha;
    rt0.destBlend = nvrhi::BlendFactor::InvSrcAlpha;
    rt0.blendOp = nvrhi::BlendOp::Add;
    rt0.srcBlendAlpha = nvrhi::BlendFactor::One;
    rt0.destBlendAlpha = nvrhi::BlendFactor::InvSrcAlpha;
    rt0.blendOpAlpha = nvrhi::BlendOp::Add;

    state.pipeline = cache.GetOrCreatePipeline("TransparentPass", pipeDesc, fbInfo, nvDevice);
    if (!state.pipeline)
        return;

    QueryBindingLayoutFromPipeline(state.pipeline, state.layout);

    state.initialized = true;
    Msg("* [TransparentPass] Pipeline initialized");
}

framegraph::DefaultOutputLayout setupTransparentPass(
    framegraph::FrameGraph& fg,
    fg::RenderDevice* device,
    const framegraph::DefaultOutputLayout& inputs,
    const TransparentPassConfig& config,
    u32 width, u32 height,
    TransparentPassState& state)
{
    using namespace framegraph;

    if (!config.IsValid()) {
        return inputs;
    }

    nvrhi::FramebufferInfoEx fbInfo;
    fbInfo.colorFormats.push_back(nvrhi::Format::RGBA16_FLOAT);
    fbInfo.colorFormats.push_back(nvrhi::Format::RGBA16_FLOAT);
    fbInfo.colorFormats.push_back(nvrhi::Format::RGBA8_UNORM);
    fbInfo.depthFormat = nvrhi::Format::D32;
    InitializeTransparentResources(device, fbInfo, state);

    // Keep the opaque scene immutable and composite into a separate target.
    // Water refraction must never sample the target receiving its own draws.
    ResourceDesc copyDesc;
    copyDesc.width = width;
    copyDesc.height = height;
    copyDesc.format = nvrhi::Format::RGBA16_FLOAT;
    copyDesc.debugName = "Water.CompositeColor";
    copyDesc.isRenderTarget = true;
    auto compositeColor = fg.CreateTexture("Water.CompositeColor", copyDesc);
    copyDesc.format = nvrhi::Format::D32;
    copyDesc.debugName = "Water.DepthCopy";
    copyDesc.isRenderTarget = false;
    copyDesc.isDepthStencil = true;
    auto waterDepth = fg.CreateTexture("Water.DepthCopy", copyDesc);
    auto copyPass = fg.AddPass("Water.CopyInputs");
    fg.PassRead(copyPass, inputs.albedo, ResourceState::CopySource);
    fg.PassRead(copyPass, inputs.depth, ResourceState::CopySource);
    fg.PassWrite(copyPass, compositeColor, ResourceState::CopyDest);
    fg.PassWrite(copyPass, waterDepth, ResourceState::CopyDest);
    fg.SetPassCallback(copyPass, [sourceColor = inputs.albedo, sourceDepth = inputs.depth, compositeColor, waterDepth]
        (fg::RenderContext& ctx, const FrameGraph& graph) {
        auto* cmd = ctx.GetCommandList();
        cmd->copyTexture(graph.GetPhysicalTexture(compositeColor), nvrhi::TextureSlice(),
            graph.GetPhysicalTexture(sourceColor), nvrhi::TextureSlice());
        cmd->copyTexture(graph.GetPhysicalTexture(waterDepth), nvrhi::TextureSlice(),
            graph.GetPhysicalTexture(sourceDepth), nvrhi::TextureSlice());
    });

    auto& passData = fg.addCallbackPass<TransparentPassData>(
        "Transparent Pass",

        [&, width, height, config](FrameGraph& builder, PassHandle passHandle, TransparentPassData& data) {
            data.width = width;
            data.height = height;
            data.device = device;
            data.config = config;
            data.passState = &state;

            RenderPassBuilder passBuilder(builder, passHandle);
            data.waterScene = passBuilder.read(inputs.albedo, ResourceState::ShaderResource);
            data.waterDepth = passBuilder.read(waterDepth, ResourceState::ShaderResource);
            ReadWorldShadowMaps(builder, passHandle);
            ReadSkyBackground(builder, passHandle);
            data.color = passBuilder.readWrite(compositeColor, ResourceState::RenderTarget);
            data.normal = passBuilder.readWrite(inputs.normal, ResourceState::RenderTarget);
            data.depth = passBuilder.read(inputs.depth, ResourceState::DepthStencilRead);
            if (inputs.baseColor.is_valid())
                data.baseColor = passBuilder.readWrite(inputs.baseColor, ResourceState::RenderTarget);
        },

        [](const TransparentPassData& data,
            const FrameGraph& fg,
            fg::RenderContext* ctx) {

            auto* colorRT = fg.GetPhysicalTexture(data.color);
            auto* normalRT = fg.GetPhysicalTexture(data.normal);
            auto* depthRT = fg.GetPhysicalTexture(data.depth);
            if (!colorRT || !depthRT)
                return;

            nvrhi::IDevice* nvDevice = data.device->GetNVRHIDevice();
            nvrhi::ICommandList* cmdList = ctx->GetCommandList();
            if (!nvDevice || !cmdList)
                return;

            auto* baseColorRT = data.baseColor.is_valid() ? fg.GetPhysicalTexture(data.baseColor) : nullptr;

            nvrhi::FramebufferDesc fbDesc;
            fbDesc.addColorAttachment(colorRT);
            if (normalRT)
                fbDesc.addColorAttachment(normalRT);
            if (baseColorRT)
                fbDesc.addColorAttachment(baseColorRT);
            fbDesc.setDepthAttachment(depthRT);
            auto& cache = framegraph::GetPassResourceCache();
            auto framebuffer = cache.GetOrCreateFramebuffer("TransparentPass", fbDesc, nvDevice);
            if (!framebuffer)
                return;

            if (!data.passState->initialized || !data.passState->pipeline)
                return;

            using namespace fg::bindless;
            auto& matBuffer = MaterialBuffer::Instance();

            auto lightingCB = cache.GetOrCreateVolatileCB("TransparentPass", "LightingCB", sizeof(LightingConstants), data.device);
            auto staticGlobalsCB = cache.GetOrCreateVolatileCB("Frame", "StaticGlobals", sizeof(StaticGlobals), data.device);
            auto drawIndexBuffer = GetOrCreateDrawIndexBuffer("TransparentPass", nvDevice);

            auto lightingData = FillLightingConstants();
            cmdList->writeBuffer(lightingCB, &lightingData, sizeof(lightingData));

            const auto& cfg = data.config;

            auto& variantTexBuffer = bindless::VariantTextureBuffer::Instance();

            auto* shaderLoader = GEnv.Render->GetShaderLoader();
            auto* vsReflection = shaderLoader->GetCachedReflection("bindless_transparent", ".vs");
            auto* psReflection = shaderLoader->GetCachedReflection("bindless_transparent", ".ps");

            framegraph::BindingSetBuilder bsb(*vsReflection, *psReflection, nvDevice, "Transparent");
            struct WaterConstants {
                Fmatrix inverseVP;
                Fvector4 options;
            };
            static_assert(sizeof(WaterConstants) == 80);
            WaterConstants water{};
            water.inverseVP = Device.mInvFullTransform;
            auto& environment = g_pGamePersistent->Environment();
            const auto& env = environment.CurrentEnv;
            water.options.set(ps_r2_ls_flags.test(R2FLAG_SOFT_WATER) ? 1.f : 0.f,
                _cos(env.sky_rotation), _sin(env.sky_rotation), env.weight);
            auto waterCB = cache.GetOrCreateVolatileCB("TransparentPass", "WaterParams", sizeof(water), data.device);
            cmdList->writeBuffer(waterCB, &water, sizeof(water));
            auto* environmentRenderer = static_cast<FGEnvironmentRender*>(&*environment.m_pRender);
            bsb.ConstantBuffer("WaterParams", waterCB);
            bsb.Texture("g_WaterDepth", fg.GetPhysicalTexture(data.waterDepth));
            bsb.Texture("g_WaterScene", fg.GetPhysicalTexture(data.waterScene));
            bsb.Texture("g_WaterSky0", environmentRenderer->GetSkyTexture(&environment, 0));
            bsb.Texture("g_WaterSky1", environmentRenderer->GetSkyTexture(&environment, 1));
            if (strstr(Core.Params, "-graphics_trace") && Device.dwFrame % 120 == 0)
                Msg("* [WaterSurface] frame=%u soft=%u sky_weight=%.4f rotation=%.4f size=%ux%u",
                    Device.dwFrame, water.options.x > .5f ? 1u : 0u, env.weight, env.sky_rotation, data.width, data.height);
            bsb.ConstantBuffer("static_globals", staticGlobalsCB);
            bsb.BufferSRV("g_Materials", matBuffer.GetBuffer());
            bsb.BufferSRV("g_InstanceData", cfg.instanceBuffer);
            bsb.BufferSRV("g_CompactBatchIndices", cfg.compactBatchIndicesBuffer);
            bsb.BufferSRV("g_CompactMaterialIDs", cfg.compactMaterialIDBuffer);
            bsb.BufferSRV("g_LightData", ClusteredLightManager::Instance().GetLightDataBuffer());
            bsb.BufferSRV("g_ClusterGrid", ClusteredLightManager::Instance().GetClusterGridBuffer());
            bsb.BufferSRV("g_LightIndexList", ClusteredLightManager::Instance().GetLightIndexListBuffer());

            auto transparentBindDesc = bsb.Build();
            auto bindingSet = framegraph::GetPassResourceCache().GetOrCreateBindingSet(transparentBindDesc, data.passState->layout, nvDevice);
            R_ASSERT2(bindingSet, "Transparent binding set creation failed");

            nvrhi::GraphicsState state;
            state.pipeline = data.passState->pipeline;
            state.framebuffer = framebuffer;
            state.bindings = { bindingSet };

            auto* backend = data.device->GetBackend();
            if (backend) {
                auto* bindlessTable = backend->GetBindlessDescriptorTable();
                if (bindlessTable)
                    state.addBindingSet(bindlessTable);
            }

            state.vertexBuffers = {
                {cfg.megaVertexBuffer, 0, 0},
                {drawIndexBuffer, 1, 0}
            };
            state.indexBuffer = { cfg.megaIndexBuffer, nvrhi::Format::R32_UINT, 0 };
            state.indirectParams = cfg.compactDrawArgsBuffer;
            state.indirectCountBuffer = cfg.compactCountBuffer;

            const auto& rtDesc = colorRT->getDesc();
            nvrhi::Viewport viewport(0.0f, static_cast<float>(rtDesc.width), 0.0f, static_cast<float>(rtDesc.height), 0.0f, 1.0f);
            state.viewport.addViewport(viewport);
            state.viewport.addScissorRect(nvrhi::Rect(rtDesc.width, rtDesc.height));

            if (cfg.variantPartition.Enabled()) {
                auto* backendDev = data.device->GetBackend();

                VariantPartitionDrawConfig vpCfg;
                vpCfg.defaultPipeline = data.passState->pipeline.Get();
                vpCfg.inputLayout = data.passState->inputLayout;
                vpCfg.passLayout = data.passState->layout;
                vpCfg.bindlessLayout = backendDev ? backendDev->GetBindlessLayout() : nullptr;
                vpCfg.bindlessTable = backend ? backend->GetBindlessDescriptorTable() : nullptr;
                vpCfg.megaVertexBuffer = cfg.megaVertexBuffer;
                vpCfg.baseBindings = transparentBindDesc;
                vpCfg.objectCount = cfg.objectCount;
                vpCfg.partition = cfg.variantPartition;
                vpCfg.selectTransparent = true;

                DrawVariantPartition(cmdList, nvDevice, framebuffer, state, vpCfg);
            } else {
                cmdList->setGraphicsState(state);
                DrawIndexedIndirectCountOrFallback(cmdList, 0, 0, cfg.objectCount);
            }
        }
    );

    DefaultOutputLayout outputs;
    outputs.albedo = passData.color;
    outputs.normal = passData.normal;
    outputs.baseColor = passData.baseColor;
    outputs.depth = passData.depth;
    return outputs;
}

} // namespace xray::render::fg::passes
