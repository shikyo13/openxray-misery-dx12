#include "stdafx.h"
#include "WaterTemporalPassSetup.h"
#include "ShaderConstants.h"
#include "PassCommon.h"
#include "Layers/xrRender/FrameGraph/BindingSetBuilder.h"
#include "Layers/xrRender/FrameGraph/PassResourceCache.h"
#include "Layers/xrRender/FrameGraph/RenderPassBuilder.h"
#include "Layers/xrRender/FrameGraph/ShaderLoader.h"
#include "Layers/xrRender/RenderContext/RenderContext.h"
#include "Layers/xrRender/RenderContext/RenderDevice.h"
#include "Layers/xrRender/Bindless/MaterialBuffer.h"
#include "Layers/xrRender/xrRender_console.h"

namespace xray::render::fg::passes {
using namespace framegraph;
WaterTemporalOutput setupWaterTemporalPass(FrameGraph& graph, fg::RenderDevice* device,
    VirtualResourceHandle depth, VirtualResourceHandle motion, const TransparentPassConfig& config,
    const Fmatrix& previousViewProjection, bool historyValid, u32 width, u32 height, WaterTemporalPassState& state)
{
    if (!ps_r_water_temporal || !config.IsValid() || !motion.is_valid() || ps_r_motion_debug == 2) return {motion, depth};
    auto* nv = device->GetNVRHIDevice();
    auto& cache = GetPassResourceCache();
    auto* loader = GEnv.Render->GetShaderLoader();
    if (!state.pipeline) {
        auto vs = loader->LoadVertexShader("water_temporal");
        auto ps = loader->LoadPixelShader("water_temporal");
        R_ASSERT2(vs.handle && ps.handle && vs.reflection && ps.reflection, "Water temporal shader compilation failed");
        state.layout = cache.GetOrCreateBindingLayoutFromReflection("WaterTemporal", *vs.reflection, *ps.reflection, nv);
        u32 count = 0;
        const auto* attributes = GetUnifiedVertexAttributes(count);
        state.inputLayout = nv->createInputLayout(attributes, count, vs.handle);
        nvrhi::GraphicsPipelineDesc desc;
        desc.VS = vs.handle; desc.PS = ps.handle; desc.inputLayout = state.inputLayout;
        desc.bindingLayouts = {state.layout, device->GetBackend()->GetBindlessLayout()};
        desc.primType = nvrhi::PrimitiveType::TriangleList;
        desc.renderState.depthStencilState.depthTestEnable = true;
        desc.renderState.depthStencilState.depthWriteEnable = true;
        desc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::GreaterOrEqual;
        desc.renderState.rasterState.frontCounterClockwise = false;
        desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::Back;
        nvrhi::FramebufferInfoEx info;
        info.colorFormats.push_back(nvrhi::Format::RG16_FLOAT); info.depthFormat = nvrhi::Format::D32;
        state.pipeline = cache.GetOrCreatePipeline("WaterTemporal", desc, info, nv);
        R_ASSERT2(state.pipeline, "Water temporal pipeline creation failed");
    }
    ResourceDesc desc;
    desc.width = width; desc.height = height; desc.format = nvrhi::Format::D32;
    desc.isDepthStencil = true; desc.debugName = "rt_TemporalDepth";
    auto temporalDepth = graph.CreateTexture(desc.debugName.c_str(), desc);
    auto copy = graph.AddPass("Water.TemporalDepthCopy");
    graph.PassRead(copy, depth, ResourceState::CopySource);
    graph.PassWrite(copy, temporalDepth, ResourceState::CopyDest);
    graph.SetPassCallback(copy, [depth, temporalDepth](fg::RenderContext& ctx, const FrameGraph& graph) {
        ctx.GetCommandList()->copyTexture(graph.GetPhysicalTexture(temporalDepth), nvrhi::TextureSlice(),
            graph.GetPhysicalTexture(depth), nvrhi::TextureSlice());
    });
    struct Constants { Fmatrix previousVP, inverseVP; Fvector4 options; };
    static_assert(sizeof(Constants) == 144);
    const bool continuous = historyValid && state.frame + 1 == Device.dwFrame && state.width == width && state.height == height;
    Constants constants{previousViewProjection, Device.mInvFullTransform, {}};
    constants.options.set(continuous ? state.time : Device.fTimeGlobal, continuous ? 1.f : 0.f,
        ps_r2_ls_flags.test(R2FLAG_SOFT_WATER) ? 1.f : 0.f, ps_r_motion_debug == 3 ? 1.f : 0.f);
    state.time = Device.fTimeGlobal; state.frame = Device.dwFrame; state.width = width; state.height = height;
    struct Data {
        VirtualResourceHandle opaqueDepth, temporalDepth, motion;
        fg::RenderDevice* device;
        WaterTemporalPassState* state;
        TransparentPassConfig config;
        Constants constants;
        u32 width, height;
    };
    auto& data = graph.addCallbackPass<Data>("Motion Vectors.Water",
        [&](FrameGraph& builder, PassHandle pass, Data& data) {
            RenderPassBuilder pb(builder, pass);
            data.opaqueDepth = pb.read(depth);
            data.temporalDepth = pb.readWrite(temporalDepth, ResourceState::DepthStencilWrite);
            data.motion = pb.readWrite(motion, ResourceState::RenderTarget);
            data.device = device; data.state = &state; data.config = config; data.constants = constants;
            data.width = width; data.height = height;
        }, [](const Data& data, const FrameGraph& graph, fg::RenderContext* ctx) {
            auto* cmd = ctx->GetCommandList();
            auto* nv = data.device->GetNVRHIDevice();
            auto& cache = GetPassResourceCache();
            auto* loader = GEnv.Render->GetShaderLoader();
            auto constants = cache.GetOrCreateVolatileCB("WaterTemporal", "Options", sizeof(Constants), data.device);
            auto globalsCB = cache.GetOrCreateVolatileCB("WaterTemporal", "Globals", sizeof(StaticGlobals), data.device);
            auto globals = BuildStaticGlobals(2.f, data.width, data.height);
            cmd->writeBuffer(constants, &data.constants, sizeof(data.constants));
            cmd->writeBuffer(globalsCB, &globals, sizeof(globals));
            const auto& config = data.config;
            BindingSetBuilder bsb(*loader->GetCachedReflection("water_temporal", ".vs"),
                *loader->GetCachedReflection("water_temporal", ".ps"), nv, "WaterTemporal");
            bsb.ConstantBuffer("WaterTemporalParams", constants).ConstantBuffer("static_globals", globalsCB)
                .BufferSRV("g_Materials", bindless::MaterialBuffer::Instance().GetBuffer())
                .BufferSRV("g_InstanceData", config.instanceBuffer)
                .BufferSRV("g_CompactBatchIndices", config.compactBatchIndicesBuffer)
                .BufferSRV("g_CompactMaterialIDs", config.compactMaterialIDBuffer)
                .Texture("t_OpaqueDepth", graph.GetPhysicalTexture(data.opaqueDepth));
            auto binding = cache.GetOrCreateBindingSet(bsb.Build(), data.state->layout, nv);
            R_ASSERT2(binding, "Water temporal binding set creation failed");
            nvrhi::FramebufferDesc fb;
            fb.addColorAttachment(graph.GetPhysicalTexture(data.motion));
            fb.setDepthAttachment(graph.GetPhysicalTexture(data.temporalDepth));
            auto framebuffer = cache.GetOrCreateFramebuffer("WaterTemporal", fb, nv);
            R_ASSERT2(framebuffer, "Water temporal framebuffer creation failed");
            nvrhi::GraphicsState draw;
            draw.pipeline = data.state->pipeline; draw.framebuffer = framebuffer;
            draw.bindings = {binding, data.device->GetBackend()->GetBindlessDescriptorTable()};
            draw.vertexBuffers = {{config.megaVertexBuffer, 0, 0}, {GetOrCreateDrawIndexBuffer("WaterTemporal", nv), 1, 0}};
            draw.indexBuffer = {config.megaIndexBuffer, nvrhi::Format::R32_UINT, 0};
            draw.indirectParams = config.compactDrawArgsBuffer;
            draw.indirectCountBuffer = config.compactCountBuffer;
            draw.viewport.addViewportAndScissorRect(nvrhi::Viewport(0.f, float(data.width), 0.f, float(data.height), 0.f, 1.f));
            cmd->setGraphicsState(draw);
            DrawIndexedIndirectCountOrFallback(cmd, 0, 0, config.objectCount);
            if (strstr(Core.Params, "-graphics_trace") && Device.dwFrame % 120 == 0)
                Msg("* [WaterTemporal] frame=%u candidates=%u continuous=%u time=%.5f previous=%.5f size=%ux%u",
                    Device.dwFrame, config.objectCount, data.constants.options.y > .5f ? 1u : 0u,
                    Device.fTimeGlobal, data.constants.options.x, data.width, data.height);
        });
    return {data.motion, data.temporalDepth};
}
}
