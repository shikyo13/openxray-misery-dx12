// DetailPassSetup.h - Framegraph pass for detail objects (grass, etc.)
#pragma once

#include "Layers/xrRender/FrameGraph/FGTypes.h"
#include "Layers/xrRender/FrameGraph/FGResource.h"
#include "Layers/xrRender/FrameGraph/IPass.h"
#include <nvrhi/nvrhi.h>
#include "Layers/xrRender/FGDetailManager.h"

struct Fmatrix;

namespace xray::render::fg {
    class FGDetailManager;
}

namespace xray::render {
    namespace fg {
        class RenderDevice;
        class RenderContext;
    }
}

namespace xray::profiler {
    class GPUProfiler;
}

namespace xray::render::framegraph {
    class FrameGraph;
}

namespace xray::render::fg::passes {

void UpdateDetailWind(FGDetailManager* wind);
FGDetailManager::DetailFrameConstants BuildDetailFrameConstants(FGDetailManager* dm, const Fmatrix& viewProjection);

struct DetailPassState {
    bool detailDataUploaded = false;
    float lastBladeWidth = 0.0f;
};

struct DetailMotionPassState {
    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::BindingLayoutHandle layout;
    Fvector4 previousWind{};
    float previousDisplacement = 0;
    u32 previousFrame = 0;
};

framegraph::VirtualResourceHandle setupDetailMotionPass(framegraph::FrameGraph& graph,
    fg::RenderDevice* device, fg::FGDetailManager* detailManager,
    framegraph::VirtualResourceHandle depth, framegraph::VirtualResourceHandle motion,
    const Fmatrix& previousViewProjection, bool historyValid, DetailMotionPassState& state);

struct DetailPassData {
    framegraph::VirtualResourceHandle inputColor;
    framegraph::VirtualResourceHandle depth;
    framegraph::VirtualResourceHandle outputColor;
    framegraph::VirtualResourceHandle outputNormal;
    framegraph::VirtualResourceHandle baseColor;
    framegraph::VirtualResourceHandle ambient;
    fg::RenderDevice* device;
    fg::FGDetailManager* detailManager;
    framegraph::DefaultOutputLayout outputs;
    u32 width;
    u32 height;
    xray::profiler::GPUProfiler* gpuProfiler;
};
// Lambda-based detail pass setup with GPU culling support
// Renders detail objects (grass, vegetation) using:
// - GPU compute culling (frustum + Hi-Z occlusion)
// - Single unified draw call via DrawIndexedInstancedIndirect
// - Interactive grass system (wind + entity interactions)
// - Virtual texturing for interaction atlas
// Renders AFTER forward color pass (details on top of world geometry)
framegraph::DefaultOutputLayout setupDetailPass(
    framegraph::FrameGraph& fg,
    fg::RenderDevice* device,
    fg::FGDetailManager* detailManager,
    const framegraph::DefaultOutputLayout& forwardInputs,
    u32 width,
    u32 height,
    xray::profiler::GPUProfiler* gpuProfiler = nullptr
);

} // namespace xray::render::fg::passes
