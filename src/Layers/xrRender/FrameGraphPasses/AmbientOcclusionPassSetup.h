#pragma once
#include "Layers/xrRender/FrameGraph/IPass.h"
#include <nvrhi/nvrhi.h>

namespace xray::render::fg { class RenderDevice; }
namespace xray::render::fg::passes {
struct AmbientOcclusionPassState {
    nvrhi::ComputePipelineHandle pipelines[4];
    nvrhi::BindingLayoutHandle layouts[4];
    nvrhi::IBuffer* constants = nullptr;
};

framegraph::DefaultOutputLayout setupAmbientOcclusionPass(
    framegraph::FrameGraph& graph, fg::RenderDevice* device,
    const framegraph::DefaultOutputLayout& inputs, u32 width, u32 height,
    AmbientOcclusionPassState& state);
}
