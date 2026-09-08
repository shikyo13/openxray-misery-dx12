#pragma once
#include "Layers/xrRender/FrameGraph/IPass.h"
#include <nvrhi/nvrhi.h>

namespace xray::render::fg { class RenderDevice; }
namespace xray::render::fg::passes {
struct ScreenSpaceGIPassState {
    nvrhi::ComputePipelineHandle pipelines[4];
    nvrhi::BindingLayoutHandle layouts[4];
    nvrhi::IBuffer* constants = nullptr;
};

framegraph::DefaultOutputLayout setupScreenSpaceGIPass(
    framegraph::FrameGraph& graph, RenderDevice* device,
    const framegraph::DefaultOutputLayout& inputs,
    framegraph::VirtualResourceHandle sourceColor,
    framegraph::VirtualResourceHandle sky, u32 width, u32 height,
    ScreenSpaceGIPassState& state);
}
