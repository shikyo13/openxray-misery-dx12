#pragma once
#include "Layers/xrRender/FrameGraph/FGResource.h"
#include "xrCDB/Frustum.h"
#include <nvrhi/nvrhi.h>

namespace xray::render { class MaterialCache; class GeometryCollector; }
namespace xray::render::framegraph { class FrameGraph; }
namespace xray::render::fg { class RenderDevice; class RenderContext; class GPUCullingManager; }
namespace xray::render::fg { class FGDetailManager; }
namespace xray::render::fg { struct IndirectDrawArgs; }
namespace xray::render::fg::decals { class OverlayManager; }

namespace xray::render::fg::passes {
struct SkinningPassState;
struct ShadowDrawRange {
    u32 first[3] = {};
    u32 count[3] = {};
};
struct ShadowMapPassState {
    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::BufferHandle constants;
    nvrhi::BufferHandle drawArgs[3];
    nvrhi::BufferHandle preparedDrawArgs[3];
    nvrhi::GraphicsPipelineHandle detailPipeline;
    nvrhi::ComputePipelineHandle detailCullPipeline;
    nvrhi::BindingLayoutHandle detailLayout, detailCullLayout;
    nvrhi::BufferHandle detailConstants, detailCullConstants, detailVisible, detailDrawArgs;
    u32 detailCapacity = 0;
    // Mode 2 compares original/optimized GPU caster counts; never used in the
    // normal path. Read only after the captured command list was submitted.
    nvrhi::BufferHandle detailValidation;
    u32 detailValidationFrame = 0, detailValidationCount = 0, detailValidationNext = 0;
};
struct SunShadowPassState : ShadowMapPassState {
    Fmatrix viewProjection[3];
    CFrustum frustum[3];
    Fvector4 splits;
    u32 resolution = 2048;
    u32 nextTrace = 0;
    bool enabled = false;
};

void InitializeShadowMapResources(RenderDevice* device, ShadowMapPassState& state);
ShadowDrawRange PrepareWorldShadowDraws(GPUCullingManager* geometry, const CFrustum& frustum,
    const xr_vector<u32>* candidates, u32 groups, u32 treeFilter, xr_vector<IndirectDrawArgs>* commands);
u32 UploadWorldShadowDraws(RenderContext* context, ShadowMapPassState& state,
    const xr_vector<IndirectDrawArgs>* commands);
u32 DrawWorldShadowMap(RenderContext* context, RenderDevice* device, GPUCullingManager* geometry,
    const Fmatrix& viewProjection, const CFrustum& frustum, nvrhi::IFramebuffer* framebuffer,
    ShadowMapPassState& state, const xr_vector<u32>* candidates = nullptr, u32 groups = 7,
    u32 treeFilter = 0, const ShadowDrawRange* prepared = nullptr,
    const xr_vector<IndirectDrawArgs>* preparedCommands = nullptr); // treeFilter: 0 all, 1 fixed, 2 animated.
bool DrawDetailShadowMap(RenderContext* context, RenderDevice* device, FGDetailManager* details,
    ShadowMapPassState& state, const Fmatrix& viewProjection, const CFrustum& frustum,
    nvrhi::IFramebuffer* framebuffer, const Fvector4* lightSphere = nullptr);

// Prepare before collecting renderables using the same volumes as shadow draws.
void PrepareSunShadowCascades(SunShadowPassState& state);

framegraph::VirtualResourceHandle setupSunShadowPass(
    framegraph::FrameGraph& graph, RenderDevice* device, GPUCullingManager* geometry,
    MaterialCache* materials, framegraph::VirtualResourceHandle uploadDependency,
    framegraph::VirtualResourceHandle detailDependency, FGDetailManager* details,
    SunShadowPassState& state, const GeometryCollector* collector, SkinningPassState& skinning,
    decals::OverlayManager* overlays);
}
