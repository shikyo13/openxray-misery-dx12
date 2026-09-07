// xrRender/FrameGraphPasses/TonemapPassSetup.h
#pragma once

#include "Layers/xrRender/FrameGraph/FGTypes.h"
#include "Layers/xrRender/FrameGraph/FGResource.h"
#include "xrCore/PostProcess/PPInfo.hpp"
#include <nvrhi/nvrhi.h>

namespace xray::render::framegraph {
    class FrameGraph;
}

namespace xray::render::fg {
    class RenderDevice;
}

namespace xray::render::fg::passes {

struct ExposurePassState;

// Matches PostProcessParams in tonemap.ps.
struct alignas(16) PostProcessConstants {
    Fvector4 colorBase;
    Fvector4 colorGray;
    Fvector4 colorAdd;
    Fvector4 dualityBlur;
    Fvector4 noiseUV;
    Fvector4 controls;
};
static_assert(sizeof(PostProcessConstants) == 96);

struct TonemapPassState {
    nvrhi::TextureHandle fallbackExposureTexture;
    nvrhi::BufferHandle postProcessBuffer;
    nvrhi::TextureHandle noiseTexture;
    nvrhi::TextureHandle colorMaps[2];
    shared_str colorMapNames[2];
    float noiseTime = 0.f;
    u32 noiseRandom = 0x9e3779b9;
    u32 noiseShiftX = 0;
    u32 noiseShiftY = 0;
    u32 nextTraceTime = 0;
    nvrhi::GraphicsPipelineHandle pipeline;
    nvrhi::BindingLayoutHandle bindingLayout;
    bool initialized = false;
};

struct TonemapPassData {
    framegraph::VirtualResourceHandle hdrInput;
    framegraph::VirtualResourceHandle exposureInput;
    framegraph::VirtualResourceHandle ldrOutput;
    bool hasExposure;
    u32 width;
    u32 height;
    TonemapPassState* passState;
    const ExposurePassState* exposurePassState;
    PostProcessConstants postProcess;
    nvrhi::TextureHandle colorMaps[2];
};

// Lambda-based tonemap pass setup
// Transfers scene color and applies authored camera PPE parameters. Exposure/ACES
// remain inactive until the lighting pipeline supports their intended input range.
// If outputTarget is valid, writes directly to it (e.g., imported backbuffer)
// If outputTarget is invalid, creates internal rt_Final texture
framegraph::VirtualResourceHandle setupTonemapPass(
    framegraph::FrameGraph& fg,
    fg::RenderDevice* device,
    framegraph::VirtualResourceHandle hdrInput,
    framegraph::VirtualResourceHandle exposureTexture,
    framegraph::VirtualResourceHandle outputTarget,
    u32 width,
    u32 height,
    TonemapPassState& state,
    const ExposurePassState* exposureState = nullptr,
    const SPPInfo& postProcess = SPPInfo{}
);

void InitializeTonemapPass(nvrhi::IDevice* device, TonemapPassState& state);
void ShutdownTonemapPass(TonemapPassState& state);

} // namespace xray::render::fg::passes
