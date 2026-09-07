#pragma once
#include "Layers/xrRender/FrameGraph/FGResource.h"
#include "xrCDB/Frustum.h"
#include <nvrhi/nvrhi.h>

namespace xray::render { class MaterialCache; class GeometryCollector; }
namespace xray::render::framegraph { class FrameGraph; }
namespace xray::render::fg { class RenderDevice; class GPUCullingManager; }
namespace xray::render::fg::decals { class OverlayManager; }

namespace xray::render::fg::passes {
struct SkinningPassState;
struct SunShadowPassState {
    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::BufferHandle constants;
    nvrhi::BufferHandle drawArgs[3][3];
    Fmatrix viewProjection[3];
    CFrustum frustum[3];
    Fvector4 splits;
    u32 resolution = 2048;
    u32 nextTrace = 0;
    bool enabled = false;
};

framegraph::VirtualResourceHandle setupSunShadowPass(
    framegraph::FrameGraph& graph, RenderDevice* device, GPUCullingManager* geometry,
    MaterialCache* materials, framegraph::VirtualResourceHandle uploadDependency,
    SunShadowPassState& state, const GeometryCollector* collector, SkinningPassState& skinning,
    decals::OverlayManager* overlays);
}
