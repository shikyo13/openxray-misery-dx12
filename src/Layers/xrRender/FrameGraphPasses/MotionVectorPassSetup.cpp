#include "stdafx.h"
#include "MotionVectorPassSetup.h"
#include "SkinningPassSetup.h"
#include "Layers/xrRender/FrameGraph/BindingSetBuilder.h"
#include "Layers/xrRender/FrameGraph/FrameGraph.h"
#include "Layers/xrRender/FrameGraph/PassResourceCache.h"
#include "Layers/xrRender/FrameGraph/RenderPassBuilder.h"
#include "Layers/xrRender/RenderContext/RenderContext.h"
#include "Layers/xrRender/RenderContext/RenderDevice.h"
#include "Layers/xrRender/FrameGraph/ShaderLoader.h"
#include "Layers/xrRender/xrRender_console.h"

namespace fg
{
    extern xray::render::FrameGraphRenderer RImplementation;
}

namespace xray::render::fg::passes {
using namespace framegraph;

static void InitializeResources(fg::RenderDevice* device, MotionVectorPassState& state)
{
    if (state.initialized) return;

    auto& cache = GetPassResourceCache();
    nvrhi::IDevice* nvDevice = device->GetNVRHIDevice();

    auto csResult = GEnv.Render->GetShaderLoader()->LoadComputeShader("restir_motion_vectors");
    if (!csResult.handle) return;

    state.layout = cache.GetOrCreateBindingLayoutFromReflection("MotionVector", *csResult.reflection, nvDevice);

    nvrhi::ComputePipelineDesc pipeDesc;
    pipeDesc.CS = csResult.handle;
    pipeDesc.bindingLayouts = { state.layout };
    state.pipeline = cache.GetOrCreateComputePipeline("MotionVector", pipeDesc, nvDevice);

    state.cb = cache.GetOrCreateVolatileCB("MotionVector", "MotionVectorCB", 160, device);

    state.initialized = true;
}

MotionVectorOutput setupMotionVectorPass(
    FrameGraph& fg,
    fg::RenderDevice* device,
    VirtualResourceHandle depthInput,
    const Fmatrix& invViewProj,
    const Fmatrix& prevViewProj,
    u32 width, u32 height,
    MotionVectorPassState& state,
    const GeometryCollector* geometry, const xr_vector<GeometryBatch>* hudBatches,
    GPUCullingManager* gpuCulling, decals::OverlayManager* overlays,
    SkinningPassState& skinning, bool historyValid)
{
    InitializeResources(device, state);

    if (!state.pipeline)
        return {};

    ResourceDesc mvDesc;
    mvDesc.type = ResourceDesc::Type::Texture2D;
    mvDesc.debugName = "rt_MotionVectors";
    mvDesc.width = width;
    mvDesc.height = height;
    mvDesc.format = nvrhi::Format::RG16_FLOAT;
    mvDesc.isUAV = true;
    mvDesc.isRenderTarget = true;
    mvDesc.isTransient = true;
    auto mvHandle = fg.CreateTexture("rt_MotionVectors", mvDesc);

    struct PassData {
        VirtualResourceHandle depth;
        VirtualResourceHandle motionVectors;
        fg::RenderDevice* device;
        MotionVectorPassState* state;
        Fmatrix invViewProj;
        Fmatrix prevViewProj;
        u32 width, height;
    };

    auto& passData = fg.addCallbackPass<PassData>(
        "Motion Vectors",
        [&, mvHandle](FrameGraph& builder, PassHandle pass, PassData& data) {
            RenderPassBuilder pb(builder, pass);
            data.depth = pb.read(depthInput, ResourceState::ShaderResource);
            data.motionVectors = pb.write(mvHandle, ResourceState::UnorderedAccess);
            data.device = device;
            data.state = &state;
            data.invViewProj = invViewProj;
            data.prevViewProj = prevViewProj;
            data.width = width;
            data.height = height;
        },
        [](const PassData& data, const FrameGraph& fg, fg::RenderContext* ctx) {
            auto* depthTex = fg.GetPhysicalTexture(data.depth);
            auto* mvTex = fg.GetPhysicalTexture(data.motionVectors);
            if (!depthTex || !mvTex) return;

            struct {
                Fmatrix invViewProj;
                Fmatrix prevViewProj;
                float screenW, screenH;
                float invScreenW, invScreenH;
                Fvector4 camera;
            } cb;
            cb.invViewProj = data.invViewProj;
            cb.prevViewProj = data.prevViewProj;
            cb.screenW = (float)data.width;
            cb.screenH = (float)data.height;
            cb.invScreenW = 1.0f / data.width;
            cb.invScreenH = 1.0f / data.height;
            cb.camera.set(Device.vCameraPosition.x, Device.vCameraPosition.y, Device.vCameraPosition.z, 0);
            static_assert(sizeof(cb) == 160);

            nvrhi::IDevice* nvDevice = data.device->GetNVRHIDevice();
            nvrhi::ICommandList* cmdList = ctx->GetCommandList();

            cmdList->writeBuffer(data.state->cb, &cb, sizeof(cb));

            auto* mvRefl = GEnv.Render->GetShaderLoader()->GetCachedReflection("restir_motion_vectors", ".cs");
            BindingSetBuilder bsb(*mvRefl, nvDevice, "MotionVector");
            bsb.ConstantBuffer("MotionVectorParams", data.state->cb)
               .Texture("t_Depth", depthTex)
               .TextureUAV("u_MotionVectors", mvTex);
            auto bindDesc = bsb.Build();
            auto& cache = GetPassResourceCache();
            auto bindingSet = cache.GetOrCreateBindingSet(bindDesc, data.state->layout, nvDevice);

            ctx->SetComputePipeline(data.state->pipeline.Get());
            ctx->SetComputeBindingSet(0, bindingSet.Get());
            ctx->Dispatch((data.width + 7) / 8, (data.height + 7) / 8, 1);
        }
    );

    if (ps_r_motion_debug == 2) return {passData.motionVectors};
    struct ObjectPassData {
        VirtualResourceHandle motion, depth;
        fg::RenderDevice* device;
        const GeometryCollector* geometry;
        const xr_vector<GeometryBatch>* hudBatches;
        GPUCullingManager* gpuCulling;
        decals::OverlayManager* overlays;
        SkinningPassState* skinning;
        ObjectMotionState* state;
        Fmatrix previousViewProjection;
        bool historyValid;
    };
    auto& objects = fg.addCallbackPass<ObjectPassData>("Motion Vectors.Objects",
        [&](FrameGraph& graph, PassHandle pass, ObjectPassData& data) {
            RenderPassBuilder pb(graph, pass);
            data.motion = pb.readWrite(passData.motionVectors, ResourceState::RenderTarget);
            data.depth = pb.read(depthInput, ResourceState::DepthStencilRead);
            data.device = device; data.geometry = geometry; data.hudBatches = hudBatches;
            data.gpuCulling = gpuCulling; data.overlays = overlays; data.skinning = &skinning;
            data.state = &state.objects; data.previousViewProjection = prevViewProj; data.historyValid = historyValid;
        },
        [](const ObjectPassData& data, const FrameGraph& graph, fg::RenderContext* ctx) {
            nvrhi::FramebufferDesc desc;
            desc.addColorAttachment(graph.GetPhysicalTexture(data.motion));
            desc.setDepthAttachment(graph.GetPhysicalTexture(data.depth));
            auto framebuffer = GetPassResourceCache().GetOrCreateFramebuffer("ObjectMotion", desc, data.device->GetNVRHIDevice());
            R_ASSERT2(framebuffer, "Object motion framebuffer creation failed");
            DrawObjectMotion(ctx, data.device, data.gpuCulling, data.geometry, data.hudBatches,
                data.overlays, framebuffer, data.previousViewProjection, data.historyValid, *data.skinning, *data.state);
        });
    return { objects.motion };
}

VirtualResourceHandle setupMotionVectorDebugPass(FrameGraph& graph, fg::RenderDevice* device,
    VirtualResourceHandle motion, u32 width, u32 height, MotionVectorPassState& state)
{
    auto& cache = GetPassResourceCache();
    auto* nv = device->GetNVRHIDevice();
    if (!state.debugPipeline) {
        auto shader = GEnv.Render->GetShaderLoader()->LoadComputeShader("motion_debug");
        R_ASSERT2(shader.handle && shader.reflection, "Motion debug shader compilation failed");
        state.debugLayout = cache.GetOrCreateBindingLayoutFromReflection("MotionDebug", *shader.reflection, nv);
        nvrhi::ComputePipelineDesc desc;
        desc.CS = shader.handle; desc.bindingLayouts = {state.debugLayout};
        state.debugPipeline = cache.GetOrCreateComputePipeline("MotionDebug", desc, nv);
        R_ASSERT2(state.debugPipeline, "Motion debug pipeline creation failed");
    }
    ResourceDesc desc;
    desc.debugName = "MotionDebug"; desc.width = width; desc.height = height;
    desc.format = nvrhi::Format::RGBA16_FLOAT; desc.isUAV = true; desc.isRenderTarget = true;
    auto output = graph.CreateTexture("MotionDebug", desc);
    auto pass = graph.AddPass("Motion Vectors.Debug");
    graph.PassRead(pass, motion, ResourceState::ShaderResource);
    graph.PassWrite(pass, output, ResourceState::UnorderedAccess);
    graph.SetPassCallback(pass, [motion, output, width, height, device, &state](fg::RenderContext& ctx, const FrameGraph& graph) {
        auto* nv = device->GetNVRHIDevice();
        auto* reflection = GEnv.Render->GetShaderLoader()->GetCachedReflection("motion_debug", ".cs");
        BindingSetBuilder bsb(*reflection, nv, "MotionDebug");
        auto constants = GetPassResourceCache().GetOrCreateVolatileCB("MotionDebug", "Options", 16, device);
        Fvector4 options;
        options.set(ps_r_motion_debug_scale, 0, 0, 0);
        ctx.GetCommandList()->writeBuffer(constants, &options, sizeof(options));
        bsb.ConstantBuffer("MotionDebugOptions", constants);
        bsb.Texture("t_Motion", graph.GetPhysicalTexture(motion));
        bsb.TextureUAV("u_Output", graph.GetPhysicalTexture(output));
        auto bindings = GetPassResourceCache().GetOrCreateBindingSet(bsb.Build(), state.debugLayout, nv);
        R_ASSERT2(bindings, "Motion debug binding set creation failed");
        ctx.SetComputePipeline(state.debugPipeline);
        ctx.SetComputeBindingSet(0, bindings);
        ctx.Dispatch((width + 7) / 8, (height + 7) / 8, 1);
    });
    return output;
}

} // namespace
