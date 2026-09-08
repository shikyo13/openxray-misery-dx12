#pragma once
#include "Layers/xrRender/FrameGraph/FGResource.h"
#include "xrCDB/Frustum.h"
#include <nvrhi/nvrhi.h>

namespace xray::render { class MaterialCache; class GeometryCollector; }
namespace xray::render::framegraph { class FrameGraph; }
namespace xray::render::fg { class RenderDevice; class RenderContext; class GPUCullingManager; }
namespace xray::render::fg { class FGDetailManager; }
namespace xray::render::fg::decals { class OverlayManager; }

namespace xray::render::fg::passes {
struct SkinningPassState;
struct ShadowMapPassState {
    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::BufferHandle constants;
    nvrhi::BufferHandle drawArgs[3];
    nvrhi::GraphicsPipelineHandle detailPipeline;
    nvrhi::ComputePipelineHandle detailCullPipeline;
    nvrhi::BindingLayoutHandle detailLayout, detailCullLayout;
    nvrhi::BufferHandle detailConstants, detailCullConstants, detailVisible, detailDrawArgs;
    u32 detailCapacity = 0;
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
u32 DrawWorldShadowMap(RenderContext* context, RenderDevice* device, GPUCullingManager* geometry,
    const Fmatrix& viewProjection, const CFrustum& frustum, nvrhi::IFramebuffer* framebuffer,
    ShadowMapPassState& state, const xr_vector<u32>* candidates = nullptr, u32 groups = 7);
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
