#pragma once
#include "Layers/xrRender/FrameGraph/FGResource.h"
#include <nvrhi/nvrhi.h>
namespace xray::render::framegraph { class FrameGraph; }
namespace xray::render::fg { class RenderDevice; }
namespace xray::render::fg::passes {
struct BloomPassState {
    nvrhi::ComputePipelineHandle pipelines[3];
    nvrhi::BindingLayoutHandle layouts[3];
    nvrhi::BufferHandle constants;
};
framegraph::VirtualResourceHandle setupBloomPass(framegraph::FrameGraph& graph,
    fg::RenderDevice* device, framegraph::VirtualResourceHandle color,
    framegraph::VirtualResourceHandle exposure, u32 width, u32 height, BloomPassState& state);
}
