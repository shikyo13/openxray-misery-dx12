#pragma once
#include "TransparentPassSetup.h"

namespace xray::render::fg::passes {
struct WaterTemporalPassState {
    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::InputLayoutHandle inputLayout;
    u32 frame = 0, width = 0, height = 0;
    float time = 0;
};
struct WaterTemporalOutput {
    framegraph::VirtualResourceHandle motion, depth;
};
WaterTemporalOutput setupWaterTemporalPass(framegraph::FrameGraph& graph, fg::RenderDevice* device,
    framegraph::VirtualResourceHandle depth, framegraph::VirtualResourceHandle motion,
    const TransparentPassConfig& config, const Fmatrix& previousViewProjection, bool historyValid,
    u32 width, u32 height, WaterTemporalPassState& state);
}
