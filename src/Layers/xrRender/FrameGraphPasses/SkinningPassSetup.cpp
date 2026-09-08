// xrRender/FrameGraphPasses/SkinningPassSetup.cpp
// Consolidated skinned mesh rendering pass with World and HUD phases
// Uses GPU-driven global bone buffer for efficient skinning
#include "stdafx.h"
#include "SkinningPassSetup.h"
#include "MotionVectorPassSetup.h"
#include "ShaderConstants.h"
#include "Layers/xrRender/FrameGraph/FrameGraph.h"
#include "Layers/xrRender/FrameGraph/IPass.h"
#include "Layers/xrRender/FrameGraph/RenderPassBuilder.h"
#include "Layers/xrRender/FrameGraph/ShaderLoader.h"
#include "Layers/xrRender/Geometry/GeometryBatch.h"
#include "Layers/xrRender/Geometry/MaterialCache.h"
#include "Layers/xrRender/RenderContext/RenderContext.h"
#include "Layers/xrRender/RenderContext/RenderDevice.h"
#include "Layers/xrRender/SkeletonCustom.h"
#include "Layers/xrRender/FSkinned.h"
#include "Layers/xrRender/SkeletonX.h"
#include "Layers/xrRender/Backend/D3D12Backend.h"
#include "Layers/xrRender/Bindless/MaterialBuffer.h"
#include "Layers/xrRender/Bindless/TerrainMaterialBuffer.h"
#include "Layers/xrRender/Bindless/VariantTextureBuffer.h"
#include "Layers/xrRender/GPUCullingManager.h"
#include "Layers/xrRender/ShaderVariant/ShaderVariantRegistry.h"
#include "Layers/xrRender/ShaderVariant/VariantPSOCache.h"
#include "Layers/xrRender/FrameGraph/PassResourceCache.h"
#include "Layers/xrRender/FrameGraph/BindingSetBuilder.h"
#include "Layers/xrRender/Decals/OverlayManager.h"
#include "PassCommon.h"
#include "Layers/xrRender/ClusteredLightManager.h"
#include "xrCore/FMesh.hpp"
#include "xrEngine/IRenderable.h"

extern ENGINE_API float psHUD_FOV;

namespace xray::render::fg::passes {
using namespace bindless;

// ═══════════════════════════════════════════════════════════════════════════
//  HUD FOV ADJUSTMENT
// ═══════════════════════════════════════════════════════════════════════════
static Fmatrix ApplyHUDFOVAdjustment(const Fmatrix& worldMatrix)
{
    float fovScale = 1.0f / psHUD_FOV;
    Fmatrix viewMatrix = Device.mView;
    Fmatrix invView;
    invView.invert(viewMatrix);

    Fmatrix fovScaleMatrix;
    fovScaleMatrix.identity();
    fovScaleMatrix._11 = fovScale;
    fovScaleMatrix._22 = fovScale;
    fovScaleMatrix._33 = 1.0f;

    Fmatrix temp1, temp2, result;
    temp1.mul(viewMatrix, worldMatrix);
    temp2.mul(fovScaleMatrix, temp1);
    result.mul(invView, temp2);

    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
//  PIPELINE INITIALIZATION
// ═══════════════════════════════════════════════════════════════════════════

void InitializeSkinningResources(fg::RenderDevice* device, const nvrhi::FramebufferInfoEx& fbInfo, SkinningPassState& state)
{
    if (state.initialized)
        return;

    nvrhi::IDevice* nvDevice = device->GetNVRHIDevice();
    if (!nvDevice)
        return;

    auto* shaderLoader = GEnv.Render->GetShaderLoader();
    if (!shaderLoader)
        return;

    auto* backend = device->GetBackend();
    nvrhi::IBindingLayout* bindlessLayout = backend ? backend->GetBindlessLayout() : nullptr;
    auto& cache = framegraph::GetPassResourceCache();

    auto skinnedPsResult = shaderLoader->LoadPixelShader("bindless_skinned", "main");
    if (!skinnedPsResult.handle) {
        Msg("! [SkinningPass] Failed to load pixel shader");
        return;
    }
    state.ps = skinnedPsResult.handle;

    auto skinnedVsForReflection = shaderLoader->LoadVertexShader("bindless_skinned", "main");
    state.layout = cache.GetOrCreateBindingLayoutFromReflection("SkinningPass", *skinnedVsForReflection.reflection, *skinnedPsResult.reflection, nvDevice);

    auto hudPsResult = shaderLoader->LoadPixelShader("bindless_skinned_hud", "main");
    if (hudPsResult.handle) {
        state.hudPS = hudPsResult.handle;
        state.hudLayout = cache.GetOrCreateBindingLayoutFromReflection("SkinningPass_HUD", *skinnedVsForReflection.reflection, *hudPsResult.reflection, nvDevice);
    }
    if (!state.hudLayout)
        state.hudLayout = state.layout;

    auto buildPipelineDesc = [&](nvrhi::IShader* vs, nvrhi::IInputLayout* il, nvrhi::IShader* psOverride = nullptr) {
        nvrhi::GraphicsPipelineDesc pipeDesc;
        pipeDesc.VS = vs;
        pipeDesc.PS = psOverride ? nvrhi::ShaderHandle(psOverride) : state.ps;
        pipeDesc.inputLayout = il;
        if (bindlessLayout)
            pipeDesc.bindingLayouts = { state.layout, bindlessLayout };
        else
            pipeDesc.bindingLayouts = { state.layout };
        pipeDesc.primType = nvrhi::PrimitiveType::TriangleList;
        pipeDesc.renderState.depthStencilState.depthTestEnable = true;
        pipeDesc.renderState.depthStencilState.depthWriteEnable = true;
        pipeDesc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::GreaterOrEqual;
        pipeDesc.renderState.rasterState.frontCounterClockwise = false;
        pipeDesc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::Back;
        return pipeDesc;
    };

    auto initVariant = [&](SkinningPipelineVariant& variant, const char* shaderName, const char* cacheName,
                           const nvrhi::VertexAttributeDesc* attribs, u32 attrCount) {
        auto vsResult = shaderLoader->LoadVertexShader(shaderName, "main");
        if (!vsResult.handle)
            return;
        variant.vs = vsResult.handle;
        variant.inputLayout = nvDevice->createInputLayout(attribs, attrCount, variant.vs);
        auto pipeDesc = buildPipelineDesc(variant.vs, variant.inputLayout);
        variant.pipeline = cache.GetOrCreatePipeline(cacheName, pipeDesc, fbInfo, nvDevice);
        if (variant.pipeline)
            QueryBindingLayoutFromPipeline(variant.pipeline, state.layout);
    };

    auto initHudVariant = [&](SkinningPipelineVariant& hudVariant, const SkinningPipelineVariant& worldVariant, const char* cacheName) {
        if (!worldVariant.pipeline || !state.hudPS)
            return;
        hudVariant.vs = worldVariant.vs;
        hudVariant.inputLayout = worldVariant.inputLayout;
        auto pipeDesc = buildPipelineDesc(worldVariant.vs, worldVariant.inputLayout, state.hudPS);
        pipeDesc.bindingLayouts[0] = state.hudLayout;
        hudVariant.pipeline = cache.GetOrCreatePipeline(cacheName, pipeDesc, fbInfo, nvDevice);
    };

    auto mdiPsResult = shaderLoader->LoadPixelShader("bindless_skinned_mdi", "main");
    auto mdiVsForReflection = shaderLoader->LoadVertexShader("bindless_skinned_mdi", "main");
    if (mdiPsResult.handle && mdiVsForReflection.handle) {
        state.mdiPS = mdiPsResult.handle;
        state.mdiLayout = cache.GetOrCreateBindingLayoutFromReflection("SkinningPass_MDI",
            *mdiVsForReflection.reflection, *mdiPsResult.reflection, nvDevice);
    }

    auto initMDIVariant = [&](SkinningPipelineVariant& variant, const char* shaderName, const char* cacheName,
                              const nvrhi::VertexAttributeDesc* baseAttribs, u32 baseAttrCount) {
        if (!state.mdiPS || !state.mdiLayout)
            return;
        auto vsResult = shaderLoader->LoadVertexShader(shaderName, "main");
        if (!vsResult.handle)
            return;

        nvrhi::VertexAttributeDesc attribs[8];
        for (u32 i = 0; i < baseAttrCount; ++i)
            attribs[i] = baseAttribs[i];
        attribs[baseAttrCount] = nvrhi::VertexAttributeDesc()
            .setName("DRAWINDEX").setFormat(nvrhi::Format::R32_UINT)
            .setBufferIndex(1).setOffset(0).setElementStride(4).setIsInstanced(true);

        variant.vs = vsResult.handle;
        variant.inputLayout = nvDevice->createInputLayout(attribs, baseAttrCount + 1, variant.vs);
        auto pipeDesc = buildPipelineDesc(variant.vs, variant.inputLayout, state.mdiPS);
        pipeDesc.bindingLayouts[0] = state.mdiLayout;
        variant.pipeline = cache.GetOrCreatePipeline(cacheName, pipeDesc, fbInfo, nvDevice);
    };

    {
        constexpr u32 stride = 24;
        nvrhi::VertexAttributeDesc attribs[] = {
            nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGBA16_SNORM).setOffset(0).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("NORMAL").setFormat(nvrhi::Format::BGRA8_UNORM).setOffset(8).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("TANGENT").setFormat(nvrhi::Format::BGRA8_UNORM).setOffset(12).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("BINORMAL").setFormat(nvrhi::Format::BGRA8_UNORM).setOffset(16).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG16_SNORM).setOffset(20).setElementStride(stride),
        };
        initVariant(state.nonHQ, "bindless_skinned", "SkinningPass_nonHQ", attribs, 5);
        initMDIVariant(state.mdiNonHQ, "bindless_skinned_mdi", "SkinningPass_mdi_nonHQ", attribs, 5);
    }

    {
        constexpr u32 stride = 36;
        nvrhi::VertexAttributeDesc attribs[] = {
            nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(0).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("NORMAL").setFormat(nvrhi::Format::BGRA8_UNORM).setOffset(16).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("TANGENT").setFormat(nvrhi::Format::BGRA8_UNORM).setOffset(20).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("BINORMAL").setFormat(nvrhi::Format::BGRA8_UNORM).setOffset(24).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(28).setElementStride(stride),
        };
        initVariant(state.hq1w, "bindless_skinned_hq", "SkinningPass_hq1w", attribs, 5);
        initMDIVariant(state.mdiHQ1w, "bindless_skinned_hq_mdi", "SkinningPass_mdi_hq1w", attribs, 5);
    }

    {
        constexpr u32 stride = 40;
        nvrhi::VertexAttributeDesc attribs[] = {
            nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(0).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("NORMAL").setFormat(nvrhi::Format::BGRA8_UNORM).setOffset(16).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("TANGENT").setFormat(nvrhi::Format::BGRA8_UNORM).setOffset(20).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("BINORMAL").setFormat(nvrhi::Format::BGRA8_UNORM).setOffset(24).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(28).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("BLENDINDICES").setFormat(nvrhi::Format::BGRA8_UNORM).setOffset(36).setElementStride(stride),
        };
        initVariant(state.hq4w, "bindless_skinned_4w", "SkinningPass_hq4w", attribs, 6);
        initMDIVariant(state.mdiHQ4w, "bindless_skinned_4w_mdi", "SkinningPass_mdi_hq4w", attribs, 6);
    }

    {
        constexpr u32 stride = 44;
        nvrhi::VertexAttributeDesc attribs[] = {
            nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(0).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("NORMAL").setFormat(nvrhi::Format::BGRA8_UNORM).setOffset(16).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("TANGENT").setFormat(nvrhi::Format::BGRA8_UNORM).setOffset(20).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("BINORMAL").setFormat(nvrhi::Format::BGRA8_UNORM).setOffset(24).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(28).setElementStride(stride),
        };
        initVariant(state.hq2w, "bindless_skinned_2w", "SkinningPass_hq2w", attribs, 5);
        initMDIVariant(state.mdiHQ2w, "bindless_skinned_2w_mdi", "SkinningPass_mdi_hq2w", attribs, 5);
    }

    {
        constexpr u32 stride = 44;
        nvrhi::VertexAttributeDesc attribs[] = {
            nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(0).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("NORMAL").setFormat(nvrhi::Format::BGRA8_UNORM).setOffset(16).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("TANGENT").setFormat(nvrhi::Format::BGRA8_UNORM).setOffset(20).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("BINORMAL").setFormat(nvrhi::Format::BGRA8_UNORM).setOffset(24).setElementStride(stride),
            nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RGBA32_FLOAT).setOffset(28).setElementStride(stride),
        };
        initVariant(state.hq3w, "bindless_skinned_3w", "SkinningPass_hq3w", attribs, 5);
        initMDIVariant(state.mdiHQ3w, "bindless_skinned_3w_mdi", "SkinningPass_mdi_hq3w", attribs, 5);
    }

    initHudVariant(state.hudNonHQ, state.nonHQ, "SkinningPass_hud_nonHQ");
    initHudVariant(state.hudHQ1w, state.hq1w, "SkinningPass_hud_hq1w");
    initHudVariant(state.hudHQ2w, state.hq2w, "SkinningPass_hud_hq2w");
    initHudVariant(state.hudHQ3w, state.hq3w, "SkinningPass_hud_hq3w");
    initHudVariant(state.hudHQ4w, state.hq4w, "SkinningPass_hud_hq4w");

    state.initialized = true;
    Msg("* [SkinningPass] Pipeline initialization complete");
}

// ═══════════════════════════════════════════════════════════════════════════
//  PIPELINE SELECTION HELPER
// ═══════════════════════════════════════════════════════════════════════════
// Render mode enum values from CSkeletonX (must match SkeletonX.h)
enum {
    RM_SKINNING_SOFT = 0,
    RM_SINGLE = 1,
    RM_SINGLE_HQ = 2,
    RM_SKINNING_1B = 3,
    RM_SKINNING_1B_HQ = 4,
    RM_SKINNING_2B = 5,
    RM_SKINNING_2B_HQ = 6,
    RM_SKINNING_3B = 7,
    RM_SKINNING_3B_HQ = 8,
    RM_SKINNING_4B = 9,
    RM_SKINNING_4B_HQ = 10
};

static nvrhi::IGraphicsPipeline* SelectSkinnedPipelineFromVariants(
    const SkinningPipelineVariant& nonHQ, const SkinningPipelineVariant& hq1w,
    const SkinningPipelineVariant& hq2w, const SkinningPipelineVariant& hq3w,
    const SkinningPipelineVariant& hq4w, u32 vertexStride, u16 renderMode)
{
    if (renderMode == RM_SKINNING_3B || renderMode == RM_SKINNING_3B_HQ)
        return hq3w.pipeline.Get();
    if (renderMode == RM_SKINNING_2B || renderMode == RM_SKINNING_2B_HQ)
        return hq2w.pipeline.Get();
    if (renderMode == RM_SKINNING_4B || renderMode == RM_SKINNING_4B_HQ)
        return hq4w.pipeline.Get();
    if (renderMode == RM_SKINNING_1B_HQ || renderMode == RM_SINGLE_HQ)
        return hq1w.pipeline.Get();
    if (renderMode == RM_SKINNING_1B || renderMode == RM_SINGLE)
        return nonHQ.pipeline.Get();

    if (vertexStride == 36)
        return hq1w.pipeline.Get();
    if (vertexStride == 40)
        return hq4w.pipeline.Get();
    if (vertexStride == 44)
        return hq2w.pipeline.Get();
    if (vertexStride == 24)
        return nonHQ.pipeline.Get();

    if (vertexStride >= 36)
        return hq1w.pipeline.Get();
    return nonHQ.pipeline.Get();
}

static nvrhi::IGraphicsPipeline* SelectSkinnedPipeline(const SkinningPassState& state, u32 vertexStride, u16 renderMode)
{
    return SelectSkinnedPipelineFromVariants(state.nonHQ, state.hq1w, state.hq2w, state.hq3w, state.hq4w, vertexStride, renderMode);
}

static nvrhi::IGraphicsPipeline* SelectHUDSkinnedPipeline(const SkinningPassState& state, u32 vertexStride, u16 renderMode)
{
    auto* hudPipe = SelectSkinnedPipelineFromVariants(state.hudNonHQ, state.hudHQ1w, state.hudHQ2w, state.hudHQ3w, state.hudHQ4w, vertexStride, renderMode);
    return hudPipe ? hudPipe : SelectSkinnedPipeline(state, vertexStride, renderMode);
}

static u32 GetSkinnedVertexFormatID(u16 renderMode, u32 vertexStride)
{
    if (renderMode == RM_SKINNING_3B || renderMode == RM_SKINNING_3B_HQ) return VF_SKINNED_HQ3W;
    if (renderMode == RM_SKINNING_2B || renderMode == RM_SKINNING_2B_HQ) return VF_SKINNED_HQ2W;
    if (renderMode == RM_SKINNING_4B || renderMode == RM_SKINNING_4B_HQ) return VF_SKINNED_HQ4W;
    if (renderMode == RM_SKINNING_1B_HQ || renderMode == RM_SINGLE_HQ) return VF_SKINNED_HQ1W;
    if (renderMode == RM_SKINNING_1B || renderMode == RM_SINGLE) return VF_SKINNED_NONHQ;
    if (vertexStride == 36) return VF_SKINNED_HQ1W;
    if (vertexStride == 40) return VF_SKINNED_HQ4W;
    if (vertexStride == 44) return VF_SKINNED_HQ2W;
    return VF_SKINNED_NONHQ;
}

static nvrhi::IInputLayout* GetSkinnedInputLayout(const SkinningPassState& state, u32 fmt)
{
    switch (fmt) {
    case VF_SKINNED_HQ1W: return state.hq1w.inputLayout.Get();
    case VF_SKINNED_HQ4W: return state.hq4w.inputLayout.Get();
    case VF_SKINNED_HQ2W: return state.hq2w.inputLayout.Get();
    case VF_SKINNED_HQ3W: return state.hq3w.inputLayout.Get();
    default: return state.nonHQ.inputLayout.Get();
    }
}

static const SkinningPipelineVariant* SelectMDISkinnedVariant(const SkinningPassState& state, u32 fmt)
{
    switch (fmt) {
    case VF_SKINNED_NONHQ: return &state.mdiNonHQ;
    case VF_SKINNED_HQ1W: return &state.mdiHQ1w;
    case VF_SKINNED_HQ2W: return &state.mdiHQ2w;
    case VF_SKINNED_HQ3W: return &state.mdiHQ3w;
    case VF_SKINNED_HQ4W: return &state.mdiHQ4w;
    default: return nullptr;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  SKELETON BONE OFFSET HELPER
// ═══════════════════════════════════════════════════════════════════════════
// Extracts parent skeleton from batch and uploads bones to global buffer.
// Returns offset into GPUCullingManager's global bone buffer.
static u32 GetSkeletonBoneOffset(
    nvrhi::ICommandList* cmdList,
    GPUCullingManager& gpuCullMgr,
    const GeometryBatch& batch)
{
    CKinematics* parent = nullptr;
    u32 visualType = batch.visual ? batch.visual->getType() : 0;

    if (visualType == MT_SKELETON_GEOMDEF_ST) {
        parent = static_cast<CSkeletonX_ST*>(batch.visual)->GetParent();
    } else if (visualType == MT_SKELETON_GEOMDEF_PM) {
        parent = static_cast<CSkeletonX_PM*>(batch.visual)->GetParent();
    }

    if (!parent)
        return 0;

    return gpuCullMgr.GetOrUploadSkeleton(cmdList, parent);
}

static decals::OverlayManager::SplatRange GetSplatRange(const GeometryBatch& batch, decals::OverlayManager* overlayMgr)
{
    if (!overlayMgr)
        return { 0, 0 };

    CKinematics* parent = nullptr;
    u32 visualType = batch.visual ? batch.visual->getType() : 0;

    if (visualType == MT_SKELETON_GEOMDEF_ST)
        parent = static_cast<CSkeletonX_ST*>(batch.visual)->GetParent();
    else if (visualType == MT_SKELETON_GEOMDEF_PM)
        parent = static_cast<CSkeletonX_PM*>(batch.visual)->GetParent();

    if (!parent)
        return { 0, 0 };

    return overlayMgr->GetSplatRange(parent);
}

// ═══════════════════════════════════════════════════════════════════════════

struct SkinnedPhaseContext {
    nvrhi::IBindingSet* bindingSet = nullptr;
    nvrhi::IBuffer* dynTransformsCB = nullptr;
    nvrhi::IBuffer* materialIdCB = nullptr;
    nvrhi::IFramebuffer* framebuffer = nullptr;
    nvrhi::IDescriptorTable* bindlessTable = nullptr;
    nvrhi::IBindingLayout* bindlessLayout = nullptr;
    nvrhi::Viewport viewport;
    nvrhi::Rect scissor;
    bool isHUD = false;
};

static SkinnedPhaseContext BuildSkinnedPhaseContext(
    const SkinningPassState& state,
    nvrhi::IDevice* nvDevice,
    nvrhi::IFramebuffer* framebuffer,
    nvrhi::IBuffer* dynTransformsCB,
    nvrhi::IBuffer* staticGlobalsCB,
    nvrhi::IBuffer* materialIdCB,
    nvrhi::IBuffer* globalBoneBuffer,
    nvrhi::IDescriptorTable* bindlessTable,
    nvrhi::IBindingLayout* bindlessLayout,
    nvrhi::IBuffer* splatBuffer,
    const nvrhi::Viewport& viewport,
    const nvrhi::Rect& scissor,
    bool isHUD)
{
    using namespace fg;
    using namespace fg::bindless;

    SkinnedPhaseContext ctx;
    ctx.dynTransformsCB = dynTransformsCB;
    ctx.materialIdCB = materialIdCB;
    ctx.framebuffer = framebuffer;
    ctx.bindlessTable = bindlessTable;
    ctx.bindlessLayout = bindlessLayout;
    ctx.viewport = viewport;
    ctx.scissor = scissor;
    ctx.isHUD = isHUD;

    if (!globalBoneBuffer)
        return ctx;

    auto& matBuffer = MaterialBuffer::Instance();
    auto& cache = framegraph::GetPassResourceCache();

    auto* shaderLoader = GEnv.Render->GetShaderLoader();
    auto* vsReflection = shaderLoader->GetCachedReflection("bindless_skinned", ".vs");
    const char* psShaderName = (isHUD && state.hudPS) ? "bindless_skinned_hud" : "bindless_skinned";
    auto* psReflection = shaderLoader->GetCachedReflection(psShaderName, ".ps");
    auto activeLayout = isHUD ? state.hudLayout : state.layout;

    framegraph::BindingSetBuilder bsb(*vsReflection, *psReflection, nvDevice, isHUD ? "Skinning.HUD" : "Skinning");
    bsb.ConstantBuffer("dynamic_transforms", dynTransformsCB);
    bsb.ConstantBuffer("static_globals", staticGlobalsCB);
    bsb.BufferSRV("g_BoneMatrices", globalBoneBuffer);
    bsb.ConstantBuffer("SkinnedMaterialCB", materialIdCB);
    bsb.BufferSRV("g_Materials", matBuffer.GetBuffer());
    bsb.BufferSRV("g_PaintSplats", splatBuffer);
    bsb.BufferSRV("g_LightData", ClusteredLightManager::Instance().GetLightDataBuffer());
    bsb.BufferSRV("g_ClusterGrid", ClusteredLightManager::Instance().GetClusterGridBuffer());
    bsb.BufferSRV("g_LightIndexList", ClusteredLightManager::Instance().GetLightIndexListBuffer());

    ctx.bindingSet = cache.GetOrCreateBindingSet(bsb.Build(), activeLayout, nvDevice);
    return ctx;
}

u32 DrawSkinnedSunShadows(RenderContext* context, RenderDevice* device, GPUCullingManager* gpuCulling,
    const GeometryCollector* geometry, decals::OverlayManager* overlays, const Fmatrix& viewProjection,
    const CFrustum& frustum, nvrhi::IFramebuffer* framebuffer, SkinningPassState& state,
    const xr_vector<const GeometryBatch*>* candidates)
{
    if (!geometry || !gpuCulling || !state.initialized || !gpuCulling->GetGlobalBoneBuffer()) return 0;
    if (candidates && candidates->empty()) return 0;
    auto* command = context->GetCommandList();
    auto* nvDevice = device->GetNVRHIDevice();
    auto* backend = device->GetBackend();
    auto* loader = GEnv.Render->GetShaderLoader();
    auto& cache = framegraph::GetPassResourceCache();
    auto ps = loader->LoadPixelShader("sun_shadow_skinned");
    const auto* vsReflection = loader->GetCachedReflection("bindless_skinned", ".vs");
    R_ASSERT2(ps.handle && vsReflection, "Skinned sun shadow shaders could not be loaded");
    if (!state.sunShadowLayout) {
        state.sunShadowLayout = cache.GetOrCreateBindingLayoutFromReflection("SunShadowSkinned",
            *vsReflection, *ps.reflection, nvDevice);
        const SkinningPipelineVariant* variants[] = {&state.nonHQ, &state.hq1w, &state.hq2w, &state.hq3w, &state.hq4w};
        for (u32 i = 0; i < 5; ++i) {
            nvrhi::GraphicsPipelineDesc desc;
            desc.VS = variants[i]->vs; desc.PS = ps.handle; desc.inputLayout = variants[i]->inputLayout;
            desc.bindingLayouts = { state.sunShadowLayout, backend->GetBindlessLayout() };
            desc.primType = nvrhi::PrimitiveType::TriangleList;
            desc.renderState.depthStencilState.depthTestEnable = true;
            desc.renderState.depthStencilState.depthWriteEnable = true;
            desc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;
            desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
            desc.renderState.rasterState.depthBias = 64;
            desc.renderState.rasterState.slopeScaledDepthBias = 1.5f;
            nvrhi::FramebufferInfoEx info; info.depthFormat = nvrhi::Format::D32;
            string32 name; xr_sprintf(name, "SunShadowSkinned%u", i);
            state.sunShadowPipelines[i] = cache.GetOrCreatePipeline(name, desc, info, nvDevice);
            R_ASSERT2(state.sunShadowPipelines[i], "Skinned sun shadow pipeline creation failed");
        }
    }
    auto transforms = cache.GetOrCreateVolatileCB("SunShadowSkinned", "Transforms", sizeof(DynamicTransforms), device, 8192);
    auto material = cache.GetOrCreateVolatileCB("SunShadowSkinned", "Material", sizeof(SkinnedMaterialCB), device, 8192);
    auto globals = cache.GetOrCreateVolatileCB("SunShadowSkinned", "Globals", sizeof(StaticGlobals), device, 4096);
    auto constants = BuildStaticGlobals();
    constants.m_VP = viewProjection;
    command->writeBuffer(globals, &constants, sizeof(constants));
    if (overlays) overlays->UploadSplats(command);
    framegraph::BindingSetBuilder bsb(*vsReflection, *ps.reflection, nvDevice, "SunShadowSkinned");
    bsb.ConstantBuffer("dynamic_transforms", transforms);
    bsb.ConstantBuffer("static_globals", globals);
    bsb.ConstantBuffer("SkinnedMaterialCB", material);
    bsb.BufferSRV("g_BoneMatrices", gpuCulling->GetGlobalBoneBuffer());
    bsb.BufferSRV("g_Materials", MaterialBuffer::Instance().GetBuffer());
    bsb.BufferSRV("g_PaintSplats", overlays ? overlays->GetSplatBuffer() : nullptr);
    auto bindings = cache.GetOrCreateBindingSet(bsb.Build(), state.sunShadowLayout, nvDevice);
    R_ASSERT2(bindings, "Skinned sun shadow binding set creation failed");
    u32 count = 0;
    const auto& batches = geometry->GetBatches();
    const u32 candidateCount = candidates ? u32(candidates->size()) : u32(batches.size());
    for (u32 i = 0; i < candidateCount; ++i) {
        const auto& batch = candidates ? *(*candidates)[i] : batches[i];
        if (!batch.isSkinned || !batch.vertexBuffer || !batch.indexBuffer) continue;
        if (!frustum.testSphere_dirty(batch.worldBoundsCenter, batch.worldBoundsRadius)) continue;
        u32 index = 0;
        switch (GetSkinnedVertexFormatID(batch.skinningRenderMode, batch.vertexStride)) {
        case VF_SKINNED_HQ1W: index = 1; break;
        case VF_SKINNED_HQ2W: index = 2; break;
        case VF_SKINNED_HQ3W: index = 3; break;
        case VF_SKINNED_HQ4W: index = 4; break;
        }
        DynamicTransforms world = {};
        FillDynamicTransforms(world, batch.worldMatrix);
        command->writeBuffer(transforms, &world, sizeof(world));
        const auto splats = GetSplatRange(batch, overlays);
        SkinnedMaterialCB materialData = {};
        materialData.materialID = batch.bindlessMaterialID;
        materialData.skeletonBoneOffset = GetSkeletonBoneOffset(command, *gpuCulling, batch);
        materialData.splatOffset = splats.offset; materialData.splatCount = splats.count;
        command->writeBuffer(material, &materialData, sizeof(materialData));
        nvrhi::GraphicsState draw;
        draw.pipeline = state.sunShadowPipelines[index]; draw.framebuffer = framebuffer;
        draw.bindings = { bindings, backend->GetBindlessDescriptorTable() };
        draw.vertexBuffers = { { batch.vertexBuffer, 0, 0 } };
        draw.indexBuffer = { batch.indexBuffer, nvrhi::Format::R16_UINT, 0 };
        const auto& size = framebuffer->getFramebufferInfo();
        draw.viewport.addViewportAndScissorRect(nvrhi::Viewport(0.f, float(size.width), 0.f, float(size.height), 0.f, 1.f));
        command->setGraphicsState(draw);
        command->drawIndexed(nvrhi::DrawArguments().setVertexCount(batch.indexCount)
            .setStartIndexLocation(batch.startIndex).setStartVertexLocation(batch.baseVertex));
        ++count;
    }
    return count;
}

void DrawObjectMotion(RenderContext* context, RenderDevice* device, GPUCullingManager* gpuCulling,
    const GeometryCollector* geometry, const xr_vector<GeometryBatch>* hudBatches,
    decals::OverlayManager* overlays, nvrhi::IFramebuffer* framebuffer,
    const Fmatrix& previousViewProjection, bool historyValid,
    SkinningPassState& skinning, ObjectMotionState& state)
{
    if (!gpuCulling || !framebuffer) return;
    if ((!geometry || geometry->GetBatches().empty()) && (!hudBatches || hudBatches->empty())) return;
    auto* command = context->GetCommandList();
    auto* nv = device->GetNVRHIDevice();
    auto* backend = device->GetBackend();
    auto* loader = GEnv.Render->GetShaderLoader();
    auto& cache = framegraph::GetPassResourceCache();
    auto ps = loader->LoadPixelShader("object_motion");
    R_ASSERT2(ps.handle && ps.reflection, "Object motion pixel shader compilation failed");
    nvrhi::FramebufferInfoEx info;
    info.colorFormats.push_back(nvrhi::Format::RG16_FLOAT);
    info.depthFormat = nvrhi::Format::D32;
    auto pipeline = [&](nvrhi::IShader* vs, nvrhi::IInputLayout* input,
                        nvrhi::IBindingLayout* layout, const char* name) {
        nvrhi::GraphicsPipelineDesc desc;
        desc.VS = vs; desc.PS = ps.handle; desc.inputLayout = input;
        desc.bindingLayouts = {layout, backend->GetBindlessLayout()};
        desc.primType = nvrhi::PrimitiveType::TriangleList;
        desc.renderState.depthStencilState.depthTestEnable = true;
        desc.renderState.depthStencilState.depthWriteEnable = false;
        desc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::Equal;
        desc.renderState.rasterState.frontCounterClockwise = false;
        desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::Back;
        auto result = cache.GetOrCreatePipeline(name, desc, info, nv);
        R_ASSERT2(result, "Object motion pipeline creation failed");
        return result;
    };
    if (!state.rigidPipeline) {
        auto vs = loader->LoadVertexShader("object_motion");
        R_ASSERT2(vs.handle && vs.reflection, "Rigid motion vertex shader compilation failed");
        state.rigidLayout = cache.GetOrCreateBindingLayoutFromReflection("ObjectMotionRigid", *vs.reflection, *ps.reflection, nv);
        nvrhi::VertexAttributeDesc attributes[] = {
            nvrhi::VertexAttributeDesc().setName("POSITION").setFormat(nvrhi::Format::RGB32_FLOAT).setOffset(0).setElementStride(48),
            nvrhi::VertexAttributeDesc().setName("TEXCOORD").setFormat(nvrhi::Format::RG32_FLOAT).setOffset(24).setElementStride(48)
        };
        state.rigidInputLayout = nv->createInputLayout(attributes, 2, vs.handle);
        state.rigidPipeline = pipeline(vs.handle, state.rigidInputLayout, state.rigidLayout, "ObjectMotionRigid");
    }
    const char* shaderNames[] = {"bindless_skinned_motion", "bindless_skinned_hq_motion",
        "bindless_skinned_2w_motion", "bindless_skinned_3w_motion", "bindless_skinned_4w_motion"};
    if (skinning.initialized && !state.skinnedPipelines[0]) {
        const SkinningPipelineVariant* variants[] = {&skinning.nonHQ, &skinning.hq1w,
            &skinning.hq2w, &skinning.hq3w, &skinning.hq4w};
        for (u32 i = 0; i < 5; ++i) {
            auto vs = loader->LoadVertexShader(shaderNames[i]);
            R_ASSERT2(vs.handle && vs.reflection, "Skinned motion vertex shader compilation failed");
            if (!state.skinnedLayout)
                state.skinnedLayout = cache.GetOrCreateBindingLayoutFromReflection("ObjectMotionSkinned", *vs.reflection, *ps.reflection, nv);
            state.skinnedPipelines[i] = pipeline(vs.handle, variants[i]->inputLayout, state.skinnedLayout, shaderNames[i]);
        }
    }
    struct MotionConstants { Fmatrix previousViewProjection, previousWorld; Fvector4 controls; };
    static_assert(sizeof(MotionConstants) == 144);
    auto motionCB = cache.GetOrCreateVolatileCB("ObjectMotion", "Motion", sizeof(MotionConstants), device, 8192);
    auto worldCB = cache.GetOrCreateVolatileCB("ObjectMotion", "World", sizeof(DynamicTransforms), device, 8192);
    auto materialCB = cache.GetOrCreateVolatileCB("ObjectMotion", "Material", sizeof(SkinnedMaterialCB), device, 8192);
    auto globalsCB = cache.GetOrCreateVolatileCB("ObjectMotion", "Globals", sizeof(StaticGlobals), device);
    auto globals = BuildStaticGlobals(2.f, framebuffer->getFramebufferInfo().width, framebuffer->getFramebufferInfo().height);
    command->writeBuffer(globalsCB, &globals, sizeof(globals));
    if (overlays) overlays->UploadSplats(command);
    auto bindingSet = [&](bool skinned) {
        const auto* vsReflection = loader->GetCachedReflection(skinned ? shaderNames[0] : "object_motion", ".vs");
        framegraph::BindingSetBuilder bsb(*vsReflection, *ps.reflection, nv, skinned ? "ObjectMotionSkinned" : "ObjectMotionRigid");
        bsb.ConstantBuffer("ObjectMotionParams", motionCB);
        bsb.ConstantBuffer("dynamic_transforms", worldCB);
        bsb.ConstantBuffer("static_globals", globalsCB);
        bsb.ConstantBuffer("SkinnedMaterialCB", materialCB);
        bsb.BufferSRV("g_Materials", MaterialBuffer::Instance().GetBuffer());
        if (skinned) {
            bsb.BufferSRV("g_BoneMatrices", gpuCulling->GetGlobalBoneBuffer());
            bsb.BufferSRV("g_PreviousBoneMatrices", gpuCulling->GetPreviousBoneBuffer());
            bsb.BufferSRV("g_PaintSplats", overlays ? overlays->GetSplatBuffer() : nullptr);
        }
        auto result = cache.GetOrCreateBindingSet(bsb.Build(), skinned ? state.skinnedLayout : state.rigidLayout, nv);
        R_ASSERT2(result, "Object motion binding set creation failed");
        return result;
    };
    auto rigidBindings = bindingSet(false);
    auto skinnedBindings = state.skinnedPipelines[0] ? bindingSet(true) : nvrhi::BindingSetHandle();
    for (auto it = state.transforms.begin(); it != state.transforms.end();) {
        if (it->second.frame != Device.dwFrame - 1) it = state.transforms.erase(it);
        else ++it;
    }
    CFrustum frustum;
    frustum.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_LRTB | FRUSTUM_P_FAR);
    const auto& size = framebuffer->getFramebufferInfo();
    u32 counts[3] = {}, continuous = 0;
    auto drawBatch = [&](const GeometryBatch& batch, bool hud) {
        // Match the visible skinning pass: it writes depth for every skinned
        // mesh, including MISERY model shaders carrying legacy transparency flags.
        if (!batch.visual || batch.isStatic || batch.isTerrain ||
            (!batch.isSkinned && batch.IsStrictB2F()) || batch.bindlessMaterialID == UINT32_MAX) return;
        if (!hud && !frustum.testSphere_dirty(batch.worldBoundsCenter, batch.worldBoundsRadius)) return;
        if (batch.isSkinned) {
            if (!skinnedBindings || !batch.vertexBuffer || !batch.indexBuffer) return;
        } else if (hud || !batch.megaBufferAlloc.valid) return;
        auto world = hud ? ApplyHUDFOVAdjustment(batch.worldMatrix) : batch.worldMatrix;
        auto* root = batch.renderable ? batch.renderable->GetRenderData().visual : nullptr;
        const u64 owner = root ? static_cast<dxRender_Visual*>(root)->motionIdentity : 0;
        auto& history = state.transforms[{batch.visual->motionIdentity, owner, hud}];
        if (!history.seen || history.frame != Device.dwFrame) {
            history.valid = historyValid && history.seen && history.frame == Device.dwFrame - 1;
            history.previous = history.valid ? history.current : world;
            history.current = world; history.frame = Device.dwFrame; history.seen = true;
        }
        MotionConstants motion{previousViewProjection, history.previous, {}};
        motion.controls.set(history.valid ? 1.f : 0.f, ps_r_motion_debug == 3 ? 1.f : 0.f, 0, 0);
        command->writeBuffer(motionCB, &motion, sizeof(motion));
        DynamicTransforms transforms{};
        FillDynamicTransforms(transforms, world);
        command->writeBuffer(worldCB, &transforms, sizeof(transforms));
        SkinnedMaterialCB material{};
        material.materialID = batch.bindlessMaterialID;
        nvrhi::GraphicsState draw;
        if (batch.isSkinned) {
            u32 index = 0;
            switch (GetSkinnedVertexFormatID(batch.skinningRenderMode, batch.vertexStride)) {
            case VF_SKINNED_HQ1W: index = 1; break;
            case VF_SKINNED_HQ2W: index = 2; break;
            case VF_SKINNED_HQ3W: index = 3; break;
            case VF_SKINNED_HQ4W: index = 4; break;
            }
            material.skeletonBoneOffset = GetSkeletonBoneOffset(command, *gpuCulling, batch);
            const auto splats = GetSplatRange(batch, overlays);
            material.splatOffset = splats.offset; material.splatCount = splats.count;
            draw.pipeline = state.skinnedPipelines[index];
            draw.bindings = {skinnedBindings, backend->GetBindlessDescriptorTable()};
            draw.vertexBuffers = {{batch.vertexBuffer, 0, 0}};
            draw.indexBuffer = {batch.indexBuffer, nvrhi::Format::R16_UINT, 0};
        } else {
            draw.pipeline = state.rigidPipeline;
            draw.bindings = {rigidBindings, backend->GetBindlessDescriptorTable()};
            draw.vertexBuffers = {{gpuCulling->GetMegaVertexBuffer(), 0, 0}};
            draw.indexBuffer = {gpuCulling->GetMegaIndexBuffer(), nvrhi::Format::R32_UINT, 0};
        }
        command->writeBuffer(materialCB, &material, sizeof(material));
        draw.framebuffer = framebuffer;
        draw.viewport.addViewportAndScissorRect(nvrhi::Viewport(0.f, float(size.width), 0.f, float(size.height), hud ? .9f : 0.f, 1.f));
        command->setGraphicsState(draw);
        command->drawIndexed(nvrhi::DrawArguments().setVertexCount(batch.indexCount)
            .setStartIndexLocation(batch.isSkinned ? batch.startIndex : batch.megaBufferAlloc.indexOffset)
            .setStartVertexLocation(batch.isSkinned ? batch.baseVertex : batch.megaBufferAlloc.vertexOffset));
        ++counts[hud ? 2 : (batch.isSkinned ? 1 : 0)];
        continuous += history.valid ? 1 : 0;
    };
    if (geometry) for (const auto& batch : geometry->GetBatches()) drawBatch(batch, false);
    if (hudBatches) for (const auto& batch : *hudBatches) drawBatch(batch, true);
    if (strstr(Core.Params, "-graphics_trace") && Device.dwFrame % 120 == 0)
        Msg("* [ObjectMotion] frame=%u rigid=%u skinned=%u hud=%u continuous=%u camera_history=%u",
            Device.dwFrame, counts[0], counts[1], counts[2], continuous, historyValid ? 1u : 0u);
}

static void DrawSkinnedBatch(
    const SkinningPassState& state,
    nvrhi::ICommandList* cmdList,
    nvrhi::IDevice* nvDevice,
    const SkinnedPhaseContext& ctx,
    const GeometryBatch& batch,
    const Fmatrix& worldMatrix,
    u32 skeletonBoneOffset,
    decals::OverlayManager::SplatRange splatRange = {0, 0},
    nvrhi::IBuffer* indirectArgs = nullptr,
    u32 indirectOffset = 0)
{
    using namespace fg;
    using namespace fg::bindless;

    if (!batch.vertexBuffer || !batch.indexBuffer || !ctx.bindingSet)
        return;

    DynamicTransforms dynTransData = {};
    FillDynamicTransforms(dynTransData, worldMatrix);
    cmdList->writeBuffer(ctx.dynTransformsCB, &dynTransData, sizeof(dynTransData));

    SkinnedMaterialCB matIdData = {};
    matIdData.materialID = batch.bindlessMaterialID;
    matIdData.skeletonBoneOffset = skeletonBoneOffset;
    matIdData.splatOffset = splatRange.offset;
    matIdData.splatCount = splatRange.count;
    cmdList->writeBuffer(ctx.materialIdCB, &matIdData, sizeof(matIdData));

    u32 variantIdx = MaterialBuffer::Instance().GetShaderVariant(batch.bindlessMaterialID);
    const ShaderVariantDesc* variant = nullptr;
    u32 passCount = 1;
    if (variantIdx > 0) {
        variant = ShaderVariantRegistry::Instance().GetVariantByIndex(variantIdx);
        if (variant)
            passCount = variant->GetPassCount();
    }

    for (u32 p = 0; p < passCount; p++) {
        nvrhi::IGraphicsPipeline* pipeline;
        if (variant) {
            u32 fmt = GetSkinnedVertexFormatID(batch.skinningRenderMode, batch.vertexStride);
            pipeline = VariantPSOCache::Instance().GetOrCreatePSO(
                nvDevice, ctx.framebuffer, variantIdx, *variant, p, fmt,
                GetSkinnedInputLayout(state, fmt), state.layout, ctx.bindlessLayout);
        } else {
            pipeline = ctx.isHUD
                ? SelectHUDSkinnedPipeline(state, batch.vertexStride, batch.skinningRenderMode)
                : SelectSkinnedPipeline(state, batch.vertexStride, batch.skinningRenderMode);
        }
        if (!pipeline)
            continue;

        nvrhi::GraphicsState gfxState;
        gfxState.pipeline = pipeline;
        gfxState.framebuffer = ctx.framebuffer;
        gfxState.bindings = { ctx.bindingSet };
        if (ctx.bindlessTable)
            gfxState.addBindingSet(ctx.bindlessTable);
        gfxState.vertexBuffers = { {batch.vertexBuffer, 0, 0} };
        gfxState.indexBuffer = { batch.indexBuffer, nvrhi::Format::R16_UINT, 0 };
        gfxState.viewport.addViewport(ctx.viewport);
        gfxState.viewport.addScissorRect(ctx.scissor);
        gfxState.indirectParams = indirectArgs;

        cmdList->setGraphicsState(gfxState);
        if (indirectArgs) {
            cmdList->drawIndexedIndirect(indirectOffset, 1);
        } else {
            cmdList->drawIndexed(
                nvrhi::DrawArguments()
                    .setVertexCount(batch.indexCount)
                    .setStartIndexLocation(batch.startIndex)
                    .setStartVertexLocation(batch.baseVertex));
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  SKINNING PASS SETUP
// ═══════════════════════════════════════════════════════════════════════════

framegraph::DefaultOutputLayout setupSkinningPass(
    framegraph::FrameGraph& fg,
    fg::RenderDevice* device,
    const framegraph::DefaultOutputLayout& inputs,
    const GeometryCollector* geometry,
    const xr_vector<GeometryBatch>* hudBatches,
    MaterialCache* materialCache,
    u32 width,
    u32 height,
    fg::GPUCullingManager* gpuCulling,
    framegraph::VirtualResourceHandle skinnedDrawArgs,
    SkinningPassState* state,
    decals::OverlayManager* overlayMgr)
{
    using namespace framegraph;

    if (state) {
        nvrhi::FramebufferInfoEx fbInfo;
        fbInfo.colorFormats.push_back(nvrhi::Format::RGBA16_FLOAT);
        fbInfo.colorFormats.push_back(nvrhi::Format::RGBA16_FLOAT);
        fbInfo.colorFormats.push_back(nvrhi::Format::RGBA8_UNORM);
        fbInfo.colorFormats.push_back(nvrhi::Format::RGBA16_FLOAT);
        fbInfo.depthFormat = nvrhi::Format::D32;
        InitializeSkinningResources(device, fbInfo, *state);
    }

    auto& passData = fg.addCallbackPass<SkinningPassData>(
        "Skinning Pass",

        // ═══════════════════════════════════════════════════════
        //  SETUP LAMBDA
        // ═══════════════════════════════════════════════════════
        [&, width, height, gpuCulling, skinnedDrawArgs, state, overlayMgr](FrameGraph& builder, PassHandle passHandle, SkinningPassData& data) {
            RenderPassBuilder passBuilder(builder, passHandle);
            ReadWorldShadowMaps(builder, passHandle);
            ReadSkyBackground(builder, passHandle);

            data.width = width;
            data.height = height;
            data.device = device;
            data.geometry = geometry;
            data.hudBatches = hudBatches;
            data.materialCache = materialCache;
            data.gpuCulling = gpuCulling;
            data.passState = state;
            data.overlayMgr = overlayMgr;

            if (skinnedDrawArgs.is_valid())
                data.skinnedDrawArgs = passBuilder.read(skinnedDrawArgs, ResourceState::IndirectArgument);

            data.color = passBuilder.readWrite(inputs.albedo, ResourceState::RenderTarget);
            data.normal = passBuilder.readWrite(inputs.normal, ResourceState::RenderTarget);
            if (inputs.baseColor.is_valid())
                data.baseColor = passBuilder.readWrite(inputs.baseColor, ResourceState::RenderTarget);
            data.ambient = passBuilder.readWrite(inputs.ambient, ResourceState::RenderTarget);
            data.depth = passBuilder.readWrite(inputs.depth, ResourceState::DepthStencilWrite);

            data.outputs.albedo = data.color;
            data.outputs.normal = data.normal;
            data.outputs.baseColor = data.baseColor;
            data.outputs.ambient = data.ambient;
            data.outputs.depth = data.depth;
        },

        // ═══════════════════════════════════════════════════════
        //  EXECUTE LAMBDA
        // ═══════════════════════════════════════════════════════
        [](const SkinningPassData& data, const FrameGraph& fg, fg::RenderContext* ctx) {
            using namespace fg;

            // Check if any skinned batches to render
            bool hasWorldSkinned = data.geometry && !data.geometry->GetBatches().empty();
            bool hasHUDSkinned = data.hudBatches && !data.hudBatches->empty();

            // Count actual skinned batches in world geometry
            u32 worldSkinnedCount = 0;
            if (hasWorldSkinned) {
                for (const auto& batch : data.geometry->GetBatches()) {
                    if (batch.isSkinned) worldSkinnedCount++;
                }
            }

            if (worldSkinnedCount == 0 && !hasHUDSkinned) {
                // Msg("! [SkinningPass] No skinned batches to render");
                return;
            }

            if (!data.passState)
                return;

            auto* colorRT = fg.GetPhysicalTexture(data.color);
            auto* normalRT = fg.GetPhysicalTexture(data.normal);
            auto* baseColorRT = data.baseColor.is_valid() ? fg.GetPhysicalTexture(data.baseColor) : nullptr;
            auto* ambientRT = fg.GetPhysicalTexture(data.ambient);
            auto* depthRT = fg.GetPhysicalTexture(data.depth);
            if (!colorRT || !depthRT)
                return;

            nvrhi::IDevice* nvDevice = data.device->GetNVRHIDevice();
            nvrhi::ICommandList* cmdList = ctx->GetCommandList();
            if (!nvDevice || !cmdList)
                return;

            nvrhi::FramebufferDesc fbDesc;
            fbDesc.addColorAttachment(colorRT);
            if (normalRT)
                fbDesc.addColorAttachment(normalRT);
            if (baseColorRT)
                fbDesc.addColorAttachment(baseColorRT);
            fbDesc.addColorAttachment(ambientRT);
            fbDesc.setDepthAttachment(depthRT);
            auto framebuffer = framegraph::GetPassResourceCache().GetOrCreateFramebuffer("SkinningPass", fbDesc, nvDevice);
            if (!framebuffer)
                return;

            if (!data.passState->initialized)
                return;

            const auto& rtDesc = colorRT->getDesc();

            GPUCullingManager* gpuCullMgr = data.gpuCulling;

            if (!gpuCullMgr || !gpuCullMgr->GetGlobalBoneBuffer()) {
                Msg("! [SkinningPass] GPUCullingManager bone buffer not available!");
                return;
            }

            // Get global bone buffer for shader binding
            nvrhi::IBuffer* globalBoneBuffer = gpuCullMgr->GetGlobalBoneBuffer();

            auto& cache = framegraph::GetPassResourceCache();
            auto dynTransformsCB = cache.GetOrCreateVolatileCB("SkinningPass", "DynTransforms", sizeof(DynamicTransforms), data.device, 1024 * 8);
            auto staticGlobalsCB = cache.GetOrCreateVolatileCB("Frame", "StaticGlobals", sizeof(StaticGlobals), data.device);
            auto shaderParamsCB = cache.GetOrCreateVolatileCB("SkinningPass", "ShaderParams", sizeof(ShaderParams), data.device, 512);
            auto materialIdCB = cache.GetOrCreateVolatileCB("SkinningPass", "MaterialId", sizeof(SkinnedMaterialCB), data.device, 1024 * 8);

            ShaderParams shaderParams = {};
            shaderParams.m_AlphaRef = 0.5f;
            shaderParams.dt_params.set(1.0f, 0.0f, 1.0f, 50.0f);
            cmdList->writeBuffer(shaderParamsCB, &shaderParams, sizeof(shaderParams));

            // Get terrain material buffer (t9) - required by bindless_common.h
            auto& terrainMatBuffer = TerrainMaterialBuffer::Instance();
            if (!terrainMatBuffer.GetBuffer()) {
                Msg("! [SkinningPass] TerrainMaterialBuffer is NULL - cannot render skinned meshes");
                return;
            }

            // Finalize any pending materials (register textures to bindless descriptor heap)
            if (data.materialCache) {
                data.materialCache->FinalizePendingMaterials(ctx);
            }

            // Upload material buffer to GPU
            auto& matBuffer = MaterialBuffer::Instance();
            matBuffer.Upload(ctx);

            if (data.overlayMgr)
                data.overlayMgr->UploadSplats(cmdList);
            nvrhi::IBuffer* splatBuffer = data.overlayMgr ? data.overlayMgr->GetSplatBuffer() : nullptr;

            auto* backend = data.device->GetBackend();
            nvrhi::IDescriptorTable* bindlessTable = backend ? backend->GetBindlessDescriptorTable() : nullptr;
            nvrhi::IBindingLayout* bindlessLayout = backend ? backend->GetBindlessLayout() : nullptr;

            // Scissor rect (same for both phases)
            nvrhi::Rect scissor(rtDesc.width, rtDesc.height);

            // ═══════════════════════════════════════════════════════
            //  PHASE 1: WORLD SKINNED MESHES (depth [0.0, 1.0])
            // ═══════════════════════════════════════════════════════
            if (worldSkinnedCount > 0) {
                nvrhi::Viewport worldViewport(
                    0.0f, static_cast<float>(rtDesc.width),
                    0.0f, static_cast<float>(rtDesc.height),
                    0.0f, 1.0f
                );

                SkinnedPhaseContext worldCtx = BuildSkinnedPhaseContext(
                    *data.passState, nvDevice, framebuffer,
                    dynTransformsCB, staticGlobalsCB, materialIdCB,
                    globalBoneBuffer, bindlessTable, bindlessLayout, splatBuffer,
                    worldViewport, scissor, false);

                const bool cullActive = data.skinnedDrawArgs.is_valid()
                    && gpuCullMgr->IsSkinnedCullingEnabled()
                    && gpuCullMgr->GetSkinnedObjectCount() == worldSkinnedCount;
                const bool mdiActive = cullActive && gpuCullMgr->IsSkinnedMDIEnabled();

                if (mdiActive) {
                    auto* shaderLoader = GEnv.Render->GetShaderLoader();
                    auto* mdiVsRefl = shaderLoader->GetCachedReflection("bindless_skinned_mdi", ".vs");
                    auto* mdiPsRefl = shaderLoader->GetCachedReflection("bindless_skinned_mdi", ".ps");
                    nvrhi::IBuffer* drawIndexBuffer = GetOrCreateDrawIndexBuffer("SkinningPass", nvDevice);
                    auto& pools = gpuCullMgr->GetSkinnedPools();
                    auto& matBuffer = MaterialBuffer::Instance();

                    for (u32 f = SkinnedGeometryPools::FIRST_FORMAT; f < SkinnedGeometryPools::FORMAT_COUNT; ++f) {
                        const auto& bucket = gpuCullMgr->GetSkinnedBucket(f);
                        if (bucket.count == 0)
                            continue;

                        const SkinningPipelineVariant* variant = SelectMDISkinnedVariant(*data.passState, f);
                        nvrhi::IBuffer* poolVB = pools.GetVertexBuffer(f);
                        nvrhi::IBuffer* poolIB = pools.GetIndexBuffer(f);
                        if (!variant || !variant->pipeline || !poolVB || !poolIB
                            || !mdiVsRefl || !mdiPsRefl || !drawIndexBuffer || !data.passState->mdiLayout) {
                            Msg("! [SkinningPass] MDI bucket %u unavailable, %u batches dropped", f, bucket.count);
                            continue;
                        }

                        framegraph::BindingSetBuilder bsb(*mdiVsRefl, *mdiPsRefl, nvDevice, "Skinning.MDI");
                        bsb.ConstantBuffer("static_globals", staticGlobalsCB);
                        bsb.BufferSRV("g_BoneMatrices", globalBoneBuffer);
                        bsb.BufferSRV("g_Materials", matBuffer.GetBuffer());
                        bsb.BufferSRV("g_PaintSplats", splatBuffer);
                        bsb.BufferSRV("g_SkinnedRecords", bucket.recordsBuffer);
                        bsb.BufferSRV("g_SkinnedCompactIndices", bucket.compactBatchIndicesBuffer);
                        bsb.BufferSRV("g_SkinnedCompactMaterialIDs", bucket.compactMaterialIDBuffer);
                        bsb.BufferSRV("g_LightData", ClusteredLightManager::Instance().GetLightDataBuffer());
                        bsb.BufferSRV("g_ClusterGrid", ClusteredLightManager::Instance().GetClusterGridBuffer());
                        bsb.BufferSRV("g_LightIndexList", ClusteredLightManager::Instance().GetLightIndexListBuffer());

                        auto& cache = framegraph::GetPassResourceCache();
                        nvrhi::BindingSetHandle mdiBindingSet = cache.GetOrCreateBindingSet(bsb.Build(), data.passState->mdiLayout, nvDevice);
                        if (!mdiBindingSet)
                            continue;

                        nvrhi::GraphicsState gfxState;
                        gfxState.pipeline = variant->pipeline;
                        gfxState.framebuffer = framebuffer;
                        gfxState.bindings = { mdiBindingSet };
                        if (bindlessTable)
                            gfxState.addBindingSet(bindlessTable);
                        gfxState.vertexBuffers = { {poolVB, 0, 0}, {drawIndexBuffer, 1, 0} };
                        gfxState.indexBuffer = { poolIB, nvrhi::Format::R16_UINT, 0 };
                        gfxState.viewport.addViewport(worldViewport);
                        gfxState.viewport.addScissorRect(scissor);
                        gfxState.indirectParams = bucket.compactDrawArgsBuffer;
                        gfxState.indirectCountBuffer = bucket.compactCountBuffer;

                        cmdList->setGraphicsState(gfxState);
                        DrawIndexedIndirectCountOrFallback(cmdList, 0, 0, bucket.count);
                    }
                }

                nvrhi::IBuffer* residualArgs = cullActive ? gpuCullMgr->GetSkinnedDrawArgsBuffer() : nullptr;
                u32 residualIdx = 0;
                for (const auto& batch : data.geometry->GetBatches()) {
                    if (!batch.isSkinned)
                        continue;
                    if (mdiActive) {
                        const u32 variantIdx = MaterialBuffer::Instance().GetShaderVariant(batch.bindlessMaterialID);
                        const bool pooled = variantIdx == 0
                            && batch.skinnedPoolFormat >= SkinnedGeometryPools::FIRST_FORMAT
                            && batch.skinnedPoolFormat < SkinnedGeometryPools::FORMAT_COUNT;
                        if (pooled)
                            continue;
                    }

                    u32 boneOffset = GetSkeletonBoneOffset(cmdList, *gpuCullMgr, batch);
                    auto sr = GetSplatRange(batch, data.overlayMgr);

                    DrawSkinnedBatch(*data.passState, cmdList, nvDevice, worldCtx,
                        batch, batch.worldMatrix, boneOffset, sr,
                        residualArgs, residualIdx * (u32)sizeof(IndirectDrawArgs));
                    ++residualIdx;
                }
            }

            // ═══════════════════════════════════════════════════════
            //  PHASE 2: HUD SKINNED MESHES (depth [0.9, 1.0])
            // ═══════════════════════════════════════════════════════
            if (hasHUDSkinned) {
                nvrhi::Viewport hudViewport(
                    0.0f, static_cast<float>(rtDesc.width),
                    0.0f, static_cast<float>(rtDesc.height),
                    0.9f, 1.0f
                );

                SkinnedPhaseContext hudCtx = BuildSkinnedPhaseContext(
                    *data.passState, nvDevice, framebuffer,
                    dynTransformsCB, staticGlobalsCB, materialIdCB,
                    globalBoneBuffer, bindlessTable, bindlessLayout, splatBuffer,
                    hudViewport, scissor, true);

                for (const auto& batch : *data.hudBatches) {
                    Fmatrix adjustedWorldMatrix = ApplyHUDFOVAdjustment(batch.worldMatrix);
                    u32 boneOffset = GetSkeletonBoneOffset(cmdList, *gpuCullMgr, batch);

                    DrawSkinnedBatch(*data.passState, cmdList, nvDevice, hudCtx,
                        batch, adjustedWorldMatrix, boneOffset, {0, 0});
                }
            }
        }
    );

    // Return outputs
    DefaultOutputLayout outputs;
    outputs.albedo = passData.color;
    outputs.normal = passData.normal;
    outputs.baseColor = passData.baseColor;
    outputs.ambient = passData.ambient;
    outputs.depth = passData.depth;
    return outputs;
}

} // namespace xray::render::fg::passes
