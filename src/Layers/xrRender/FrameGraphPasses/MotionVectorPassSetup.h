#pragma once
#include "Layers/xrRender/FrameGraph/FGTypes.h"
#include "Layers/xrRender/FrameGraph/FGResource.h"
#include <nvrhi/nvrhi.h>
#include <tuple>

namespace xray::render::fg { class RenderDevice; }
namespace xray::render::framegraph { class FrameGraph; }
namespace xray::render { class GeometryCollector; struct GeometryBatch; }
namespace xray::render::fg { class GPUCullingManager; }
namespace xray::render::fg::decals { class OverlayManager; }

namespace xray::render::fg::passes {

struct SkinningPassState;
struct ObjectMotionState {
    nvrhi::GraphicsPipelineHandle rigidPipeline;
    nvrhi::InputLayoutHandle rigidInputLayout;
    nvrhi::BindingLayoutHandle rigidLayout;
    nvrhi::GraphicsPipelineHandle skinnedPipelines[5];
    nvrhi::BindingLayoutHandle skinnedLayout;
    struct TransformHistory {
        Fmatrix current, previous;
        u32 frame = 0;
        bool seen = false, valid = false;
    };
    xr_map<std::tuple<u64, u64, bool>, TransformHistory> transforms;
};

struct MotionVectorPassState {
    nvrhi::ComputePipelineHandle pipeline;
    nvrhi::BindingLayoutHandle layout;
    nvrhi::IBuffer* cb = nullptr;
    bool initialized = false;
    ObjectMotionState objects;
    nvrhi::ComputePipelineHandle debugPipeline;
    nvrhi::BindingLayoutHandle debugLayout;
};

struct MotionVectorOutput {
    framegraph::VirtualResourceHandle motionVectors;
};

MotionVectorOutput setupMotionVectorPass(
    framegraph::FrameGraph& fg,
    fg::RenderDevice* device,
    framegraph::VirtualResourceHandle depthInput,
    const Fmatrix& invViewProj,
    const Fmatrix& prevViewProj,
    u32 width, u32 height,
    MotionVectorPassState& state,
    const GeometryCollector* geometry,
    const xr_vector<GeometryBatch>* hudBatches,
    GPUCullingManager* gpuCulling, decals::OverlayManager* overlays,
    SkinningPassState& skinning, bool historyValid);

framegraph::VirtualResourceHandle setupMotionVectorDebugPass(framegraph::FrameGraph& graph,
    fg::RenderDevice* device, framegraph::VirtualResourceHandle motion,
    u32 width, u32 height, MotionVectorPassState& state);

} // namespace
