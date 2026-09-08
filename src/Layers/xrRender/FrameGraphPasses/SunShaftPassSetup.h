#pragma once
#include "Layers/xrRender/FrameGraph/FGResource.h"
#include <nvrhi/nvrhi.h>
namespace xray::render::framegraph { class FrameGraph; }
namespace xray::render::fg { class RenderDevice; }
namespace xray::render::fg::passes {
struct SunShadowPassState;
struct SunShaftPassState {
    nvrhi::ComputePipelineHandle pipelines[4];
    nvrhi::BindingLayoutHandle layouts[4];
    nvrhi::BufferHandle constants;
};
framegraph::VirtualResourceHandle setupSunShaftPass(framegraph::FrameGraph& graph,
    RenderDevice* device, framegraph::VirtualResourceHandle color,
    framegraph::VirtualResourceHandle depth, framegraph::VirtualResourceHandle shadowMap,
    const SunShadowPassState& sun, u32 width, u32 height, SunShaftPassState& state);
}
