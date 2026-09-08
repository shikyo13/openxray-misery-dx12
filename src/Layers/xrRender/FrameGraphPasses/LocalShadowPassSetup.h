#pragma once
#include "SunShadowPassSetup.h"

namespace xray::render::fg { class light; }

namespace xray::render::fg::passes {
struct LocalStaticShadowCache {
    nvrhi::TextureHandle texture;
    nvrhi::FramebufferHandle framebuffers[6];
    nvrhi::BufferHandle staticInstances, terrainInstances;
    Fmatrix matrices[6];
    bool valid[6] = {};
    u32 lastUsedFrame = 0;
    u64 materialRevision = 0;
};
struct LocalShadowPassState {
    ShadowMapPassState drawing;
    nvrhi::TextureHandle outputTexture;
    xr_vector<nvrhi::FramebufferHandle> outputFramebuffers;
    xr_vector<Fmatrix> matrices;
    xr_vector<CFrustum> frusta;
    xr_map<const light*, LocalStaticShadowCache> staticCache;
    xr_vector<u32> owners;
    xr_vector<u32> lightFaces;
    xr_vector<bool> visibleFaces;
    u32 resolution = 512;
    u32 pointLights = 0, spotLights = 0, omittedLights = 0;
    u32 nextTrace = 0;
};

framegraph::VirtualResourceHandle setupLocalShadowPass(
    framegraph::FrameGraph& graph, RenderDevice* device, GPUCullingManager* geometry,
    MaterialCache* materials, framegraph::VirtualResourceHandle uploadDependency,
    framegraph::VirtualResourceHandle detailDependency, FGDetailManager* details,
    LocalShadowPassState& state, const GeometryCollector* collector, SkinningPassState& skinning,
    decals::OverlayManager* overlays);
}
