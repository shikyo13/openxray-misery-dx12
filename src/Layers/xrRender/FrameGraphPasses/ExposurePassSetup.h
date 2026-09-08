// xrRender/FrameGraphPasses/ExposurePassSetup.h
#pragma once

#include "Layers/xrRender/FrameGraph/FGTypes.h"
#include "Layers/xrRender/FrameGraph/FGResource.h"
#include <nvrhi/nvrhi.h>

// Forward declarations
namespace xray::render {
    namespace fg {
        class RenderDevice;
    }
}

namespace xray::render::framegraph {
    class FrameGraph;
}

namespace xray::render::fg::passes {

// ═══════════════════════════════════════════════════════
//  EXPOSURE PASS (Auto-Exposure / Eye Adaptation)
// ═══════════════════════════════════════════════════════
//
// Computes scene exposure for HDR rendering using histogram-based
// auto-exposure with temporal eye adaptation.
//
// PIPELINE:
// 1. Luminance Histogram - Compute shader generates 64-bin histogram
//    from HDR scene in log2 luminance space
// 2. Exposure Adaptation - Compute shader analyzes histogram,
//    skips extreme values, computes target exposure, applies
//    temporal smoothing for eye adaptation effect
//
// OUTPUT:
// - 1x1 R32_FLOAT texture containing exposure value
// - SceneTonemap applies this to the scene before antialiasing and UI.
//
// REFERENCES:
// - Krzysztof Narkowicz: "Automatic Exposure" (2016)
// - Epic Games: "Auto Exposure in UE 4.25" (2020)
// - Hillaire: "A Scalable and Production Ready Sky and Atmosphere" (2020)

struct ExposurePassState {
    nvrhi::BufferHandle histogramBuffer;
    nvrhi::TextureHandle exposureTexture;
    nvrhi::ComputePipelineHandle histogramPipeline;
    nvrhi::ComputePipelineHandle adaptPipeline;
    nvrhi::BindingLayoutHandle histogramLayout;
    nvrhi::BindingLayoutHandle adaptLayout;
    bool initialized = false;
    bool computeEnabled = false;
    bool historyValid = false;
    float currentExposure = 1.0f;
};

struct ExposureConfig {
    // Histogram parameters
    float minLogLuminance = -10.0f;  // Minimum log2 luminance (EV)
    float maxLogLuminance = 4.0f;    // Maximum log2 luminance (EV)

    // Percentile clamping (skip extreme values)
    float lowPercentile = 0.0f;
    float highPercentile = 1.0f;

    // Exponential eye adaptation rate (per second).
    float adaptSpeedUp = 3.0f;       // Speed when brightening
    float adaptSpeedDown = 1.0f;     // Speed when darkening (slower)

    // Exposure limits
    float minExposure = 1.0f / 128.0f;
    float maxExposure = 20.0f;

    // Authored X-Ray exposure controls (not a photographic EV100 calibration).
    float exposureCompensation = 0.0f;  // Manual EV adjustment
    float middleGray = 1.0f;
    float amount = 0.7f;
    float lowLuminance = 0.4f;
};

struct ExposurePassData {
    framegraph::VirtualResourceHandle sceneColor;
    framegraph::VirtualResourceHandle exposureTexture;
    framegraph::VirtualResourceHandle histogramBuffer;
    fg::RenderDevice* device;
    ExposureConfig config;
    float deltaTime;
    u32 width;
    u32 height;
    ExposurePassState* passState;
};

// Output handles from exposure pass
struct ExposureOutput {
    framegraph::VirtualResourceHandle exposureTexture;  // 1x1 R32_FLOAT
    framegraph::VirtualResourceHandle histogramBuffer;  // 64 u32 bins (for debug)
};

void InitializeExposureResources(fg::RenderDevice* device, ExposurePassState& state);

// Setup the exposure pass
// Returns handle to 1x1 exposure texture
ExposureOutput setupExposurePass(
    framegraph::FrameGraph& fg,
    fg::RenderDevice* device,
    framegraph::VirtualResourceHandle hdrSceneColor,
    const ExposureConfig& config,
    float deltaTime,
    u32 width,
    u32 height,
    ExposurePassState& state
);

ExposureConfig GetDefaultExposureConfig();

nvrhi::ITexture* GetExposureTexture(const ExposurePassState& state);

} // namespace xray::render::fg::passes
