#pragma once
#include "Layers/xrRender/FrameGraph/FGResource.h"
#include <nvrhi/nvrhi.h>
#include <memory>

namespace xray::render::framegraph { class FrameGraph; }
namespace xray::render::fg { class RenderDevice; class RenderContext; }
namespace xray::render::fg::passes {
class DlssPassState {
public:
    DlssPassState();
    ~DlssPassState();
    bool Prepare(nvrhi::IDevice* device, u32 width, u32 height);
    bool Evaluate(nvrhi::ICommandList* cmd, nvrhi::ITexture* color, nvrhi::ITexture* depth,
        nvrhi::ITexture* motion, nvrhi::ITexture* output, float jitterX, float jitterY, bool reset);
    nvrhi::ComputePipelineHandle depthPipeline;
    nvrhi::BindingLayoutHandle depthLayout;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
framegraph::VirtualResourceHandle setupDlssPass(framegraph::FrameGraph& graph,
    fg::RenderDevice* device, framegraph::VirtualResourceHandle color,
    framegraph::VirtualResourceHandle depth, framegraph::VirtualResourceHandle motion,
    u32 width, u32 height, float jitterX, float jitterY, bool reset, DlssPassState& state);
}
