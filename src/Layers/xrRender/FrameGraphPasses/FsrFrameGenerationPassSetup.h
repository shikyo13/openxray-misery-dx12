#pragma once
#include "Layers/xrRender/FrameGraph/FGResource.h"
#include <nvrhi/nvrhi.h>
namespace xray::render::framegraph { class FrameGraph; }
namespace xray::render::fg { class RenderDevice; }
namespace xray::render::fg::passes {
struct FsrFrameGenerationPassState {
    nvrhi::ComputePipelineHandle depthPipeline;
    nvrhi::BindingLayoutHandle depthLayout;
    u32 lastFrame = 0, width = 0, height = 0;
    float previousJitterX = 0.f, previousJitterY = 0.f;
};
bool prepareFsrFrameGeneration(fg::RenderDevice* device, u32 width, u32 height);
framegraph::VirtualResourceHandle importFsrHudlessTarget(framegraph::FrameGraph& graph);
framegraph::VirtualResourceHandle setupFsrFrameGenerationPass(framegraph::FrameGraph& graph,
    fg::RenderDevice* device, framegraph::VirtualResourceHandle backbuffer,
    framegraph::VirtualResourceHandle hudless, framegraph::VirtualResourceHandle depth,
    framegraph::VirtualResourceHandle motion, u32 width, u32 height,
    float jitterX, float jitterY, bool reset, FsrFrameGenerationPassState& state);
}
