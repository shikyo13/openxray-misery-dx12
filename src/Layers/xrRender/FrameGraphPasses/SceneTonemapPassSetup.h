#pragma once
#include "Layers/xrRender/FrameGraph/FGResource.h"
#include <nvrhi/nvrhi.h>
namespace xray::render::framegraph { class FrameGraph; }
namespace xray::render::fg { class RenderDevice; }
namespace xray::render::fg::passes {
struct SceneTonemapPassState {
    nvrhi::ComputePipelineHandle pipeline;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::BufferHandle constants;
};
framegraph::VirtualResourceHandle setupSceneTonemapPass(framegraph::FrameGraph& graph,
    fg::RenderDevice* device, framegraph::VirtualResourceHandle color,
    framegraph::VirtualResourceHandle exposure, framegraph::VirtualResourceHandle bloom, u32 width, u32 height,
    SceneTonemapPassState& state);
}
