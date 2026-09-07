#include "stdafx.h"
#include "SunShadowPassSetup.h"
#include "SkinningPassSetup.h"
#include "PassCommon.h"
#include "Layers/xrRender/FrameGraph/FrameGraph.h"
#include "Layers/xrRender/FrameGraph/RenderPassBuilder.h"
#include "Layers/xrRender/FrameGraph/BindingSetBuilder.h"
#include "Layers/xrRender/FrameGraph/PassResourceCache.h"
#include "Layers/xrRender/FrameGraph/ShaderLoader.h"
#include "Layers/xrRender/Geometry/MaterialCache.h"
#include "Layers/xrRender/RenderContext/RenderDevice.h"
#include "Layers/xrRender/RenderContext/RenderContext.h"
#include "Layers/xrRender/Bindless/MaterialBuffer.h"
#include "Layers/xrRender/GPUCullingManager.h"
#include "Layers/xrRender/xrRender_console.h"
#include "xrEngine/IGame_Persistent.h"

namespace xray::render::fg::passes {
namespace {
struct alignas(16) SunShadowDrawConstants {
    Fmatrix viewProjection;
    Fvector4 options; // x: terrain uses an opaque material table
};
static_assert(sizeof(SunShadowDrawConstants) == 80);

void PrepareCascades(SunShadowPassState& state)
{
    const auto& environment = g_pGamePersistent->Environment().CurrentEnv;
    state.enabled = ps_r_sun_shadows && ps_r2_ls_flags.test(R2FLAG_SUN) && !strstr(Core.Params, "-noshadows") &&
        environment.sun_color.square_magnitude() > EPS_S;
    state.resolution = ps_r2_smapsize;
    const float farDistance = clampr(ps_r2_sun_far, 30.f, 500.f);
    state.splits.set(_min(25.f, farDistance * .25f), _min(75.f, farDistance * .6f), farDistance,
        state.enabled ? 1.f / state.resolution : 0.f);
    const float distances[] = { state.splits.x, state.splits.y, state.splits.z };
    Fvector direction = environment.sun_dir;
    direction.normalize_safe();
    Fvector up; up.set(0.f, 1.f, 0.f);
    if (_abs(direction.y) > .99f) up.set(0.f, 0.f, 1.f);
    Fvector origin; origin.set(0.f, 0.f, 0.f);
    Fmatrix rotation;
    rotation.build_camera_dir(origin, direction, up);

    float nearDistance = .05f;
    for (u32 cascade = 0; cascade < 3; ++cascade) {
        const float end = distances[cascade];
        Fvector center = Device.vCameraPosition;
        center.mad(Device.vCameraDirection, (nearDistance + end) * .5f);
        float radius = 0.f;
        for (float distance : { nearDistance, end }) {
            for (float x : { -1.f, 1.f }) for (float y : { -1.f, 1.f }) {
                Fvector corner = Device.vCameraPosition;
                corner.mad(Device.vCameraDirection, distance);
                corner.mad(Device.vCameraRight, x * distance / Device.mProject._11);
                corner.mad(Device.vCameraTop, y * distance / Device.mProject._22);
                radius = _max(radius, corner.distance_to(center));
            }
        }
        // A rotation-invariant enclosing sphere and texel snapping keep the map
        // stable as the camera moves. Include casters toward the sun off screen.
        radius = std::ceil(radius * 16.f) / 16.f + 1.f;
        const float texelWorld = 2.f * radius / state.resolution;
        Fvector lightCenter; rotation.transform_tiny(lightCenter, center);
        Fmatrix view = rotation;
        view._41 = -std::floor(lightCenter.x / texelWorld + .5f) * texelWorld;
        view._42 = -std::floor(lightCenter.y / texelWorld + .5f) * texelWorld;
        const float minimumZ = lightCenter.z - radius - 250.f;
        const float maximumZ = lightCenter.z + radius;
        Fmatrix projection; projection.identity();
        projection._11 = 1.f / radius;
        projection._22 = 1.f / radius;
        projection._33 = 1.f / (maximumZ - minimumZ);
        projection._43 = -minimumZ / (maximumZ - minimumZ);
        state.viewProjection[cascade].mul(projection, view);
        state.frustum[cascade].CreateFromMatrix(state.viewProjection[cascade], FRUSTUM_P_ALL);
        nearDistance = end * .9f; // overlap for filtered cascade transitions
    }
}

void InitializeShadowPipeline(RenderDevice* device, SunShadowPassState& state)
{
    if (state.pipeline) return;
    auto* nvDevice = device->GetNVRHIDevice();
    auto* loader = GEnv.Render->GetShaderLoader();
    auto vs = loader->LoadVertexShader("sun_shadow");
    auto ps = loader->LoadPixelShader("sun_shadow");
    R_ASSERT2(vs.handle && ps.handle, "Sun shadow shaders could not be loaded");
    auto& cache = framegraph::GetPassResourceCache();
    state.layout = cache.GetOrCreateBindingLayoutFromReflection("SunShadow", *vs.reflection, *ps.reflection, nvDevice);
    u32 attributeCount;
    auto* attributes = GetUnifiedVertexAttributes(attributeCount);
    auto inputLayout = nvDevice->createInputLayout(attributes, attributeCount, vs.handle);
    nvrhi::GraphicsPipelineDesc desc;
    desc.VS = vs.handle; desc.PS = ps.handle; desc.inputLayout = inputLayout;
    desc.bindingLayouts = { state.layout, device->GetBackend()->GetBindlessLayout() };
    desc.primType = nvrhi::PrimitiveType::TriangleList;
    desc.renderState.depthStencilState.depthTestEnable = true;
    desc.renderState.depthStencilState.depthWriteEnable = true;
    desc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;
    desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
    desc.renderState.rasterState.depthBias = 64;
    desc.renderState.rasterState.slopeScaledDepthBias = 1.5f;
    nvrhi::FramebufferInfoEx fbInfo; fbInfo.depthFormat = nvrhi::Format::D32;
    state.pipeline = cache.GetOrCreatePipeline("SunShadow", desc, fbInfo, nvDevice);
    R_ASSERT2(state.pipeline, "Sun shadow pipeline creation failed");
    nvrhi::BufferDesc cb;
    cb.byteSize = sizeof(SunShadowDrawConstants); cb.isConstantBuffer = true;
    cb.isVolatile = true; cb.maxVersions = 32; cb.debugName = "SunShadowDraw";
    state.constants = nvDevice->createBuffer(cb);
    R_ASSERT(state.constants);
}
}

framegraph::VirtualResourceHandle setupSunShadowPass(
    framegraph::FrameGraph& graph, RenderDevice* device, GPUCullingManager* geometry,
    MaterialCache* materials, framegraph::VirtualResourceHandle uploadDependency,
    SunShadowPassState& state, const GeometryCollector* collector, SkinningPassState& skinning,
    decals::OverlayManager* overlays)
{
    PrepareCascades(state);
    InitializeShadowPipeline(device, state);
    struct PassData {
        framegraph::VirtualResourceHandle texture;
        RenderDevice* device;
        GPUCullingManager* geometry;
        MaterialCache* materials;
        SunShadowPassState* state;
        const GeometryCollector* collector;
        SkinningPassState* skinning;
        decals::OverlayManager* overlays;
    };
    auto& pass = graph.addCallbackPass<PassData>("Sun shadow maps",
        [&](framegraph::FrameGraph& builder, framegraph::PassHandle handle, PassData& data) {
            framegraph::RenderPassBuilder pb(builder, handle);
            if (uploadDependency.is_valid()) pb.read(uploadDependency, framegraph::ResourceState::IndirectArgument);
            framegraph::ResourceDesc desc;
            desc.type = framegraph::ResourceDesc::Type::Texture2DArray;
            desc.width = desc.height = state.resolution;
            desc.arraySize = 3; desc.format = nvrhi::Format::D32;
            desc.isDepthStencil = true; desc.isTransient = false; desc.debugName = "rt_SunShadow";
            data.texture = pb.createTexture("rt_SunShadow", desc);
            data.device = device; data.geometry = geometry; data.materials = materials; data.state = &state;
            data.collector = collector; data.skinning = &skinning; data.overlays = overlays;
        },
        [](const PassData& data, const framegraph::FrameGraph& graph, RenderContext* context) {
            auto& state = *data.state;
            auto* command = context->GetCommandList();
            auto* texture = graph.GetPhysicalTexture(data.texture);
            command->clearDepthStencilTexture(texture, nvrhi::AllSubresources, true, 1.f, false, 0);
            auto* geometry = data.geometry;
            if (!state.enabled || !geometry || !geometry->IsMegaDataUploaded()) return;
            data.materials->FinalizePendingMaterials(context);
            auto& materialBuffer = bindless::MaterialBuffer::Instance();
            materialBuffer.Upload(context);
            auto* nvDevice = command->getDevice();
            auto& cache = framegraph::GetPassResourceCache();
            auto* loader = GEnv.Render->GetShaderLoader();
            const auto* vs = loader->GetCachedReflection("sun_shadow", ".vs");
            const auto* ps = loader->GetCachedReflection("sun_shadow", ".ps");
            auto indexBuffer = GetOrCreateDrawIndexBuffer("SunShadow", nvDevice);
            u32 counts[3] = {};
            u32 skinnedCounts[3] = {};
            for (u32 cascade = 0; cascade < 3; ++cascade) {
                nvrhi::FramebufferDesc fb;
                fb.setDepthAttachment(nvrhi::FramebufferAttachment().setTexture(texture).setArraySlice(cascade));
                string32 cacheName; xr_sprintf(cacheName, "SunShadow%u", cascade);
                auto framebuffer = cache.GetOrCreateFramebuffer(cacheName, fb, nvDevice);
                auto drawSet = [&](u32 group, const xr_vector<IndirectDrawArgs>& original,
                    const xr_vector<GPUObjectData>& objects, nvrhi::IBuffer* instances, bool terrain) {
                    if (original.empty() || !instances) return;
                    R_ASSERT(original.size() == objects.size());
                    xr_vector<IndirectDrawArgs> visible;
                    visible.reserve(original.size());
                    for (u32 i = 0; i < original.size(); ++i) {
                        if (!state.frustum[cascade].testSphere_dirty(objects[i].position, objects[i].radius)) continue;
                        auto args = original[i];
                        args.instanceCount = 1; args.startInstanceLocation = i;
                        visible.push_back(args);
                    }
                    if (visible.empty()) return;
                    const u64 bytes = visible.size() * sizeof(IndirectDrawArgs);
                    auto& buffer = state.drawArgs[cascade][group];
                    if (!buffer || buffer->getDesc().byteSize < bytes) {
                        nvrhi::BufferDesc desc;
                        desc.byteSize = original.size() * sizeof(IndirectDrawArgs);
                        desc.isDrawIndirectArgs = true; desc.debugName = "SunShadowDrawArgs";
                        desc.initialState = nvrhi::ResourceStates::IndirectArgument; desc.keepInitialState = true;
                        buffer = nvDevice->createBuffer(desc);
                    }
                    command->writeBuffer(buffer, visible.data(), bytes);
                    SunShadowDrawConstants constants;
                    constants.viewProjection = state.viewProjection[cascade];
                    constants.options.set(terrain ? 1.f : 0.f, 0.f, 0.f, 0.f);
                    command->writeBuffer(state.constants, &constants, sizeof(constants));
                    framegraph::BindingSetBuilder bsb(*vs, *ps, nvDevice, "SunShadow");
                    bsb.ConstantBuffer("SunShadowDraw", state.constants);
                    bsb.BufferSRV("g_InstanceData", instances);
                    bsb.BufferSRV("g_Materials", materialBuffer.GetBuffer());
                    auto bindings = cache.GetOrCreateBindingSet(bsb.Build(), state.layout, nvDevice);
                    R_ASSERT2(bindings, "Sun shadow binding set creation failed");
                    nvrhi::GraphicsState draw;
                    draw.pipeline = state.pipeline; draw.framebuffer = framebuffer;
                    draw.bindings = { bindings, data.device->GetBackend()->GetBindlessDescriptorTable() };
                    draw.vertexBuffers = { {geometry->GetMegaVertexBuffer(), 0, 0}, {indexBuffer, 1, 0} };
                    draw.indexBuffer = { geometry->GetMegaIndexBuffer(), nvrhi::Format::R32_UINT, 0 };
                    draw.indirectParams = buffer;
                    draw.viewport.addViewportAndScissorRect(nvrhi::Viewport(0.f, float(state.resolution), 0.f, float(state.resolution), 0.f, 1.f));
                    command->setGraphicsState(draw);
                    command->drawIndexedIndirect(0, u32(visible.size()));
                    counts[cascade] += u32(visible.size());
                };
                drawSet(0, geometry->GetStaticDrawArgsData(), geometry->GetStaticObjectData(), geometry->GetStaticInstanceBuffer(), false);
                drawSet(1, geometry->GetTerrainDrawArgsData(), geometry->GetTerrainObjectData(), geometry->GetTerrainInstanceBuffer(), true);
                drawSet(2, geometry->GetDynamicDrawArgsData(), geometry->GetDynamicObjectData(), geometry->GetDynamicInstanceBuffer(), false);
                skinnedCounts[cascade] = DrawSkinnedSunShadows(context, data.device, geometry, data.collector,
                    data.overlays, state.viewProjection[cascade], state.frustum[cascade], framebuffer, *data.skinning);
            }
            if (strstr(Core.Params, "-shadow_trace") && Device.dwTimeGlobal >= state.nextTrace) {
                state.nextTrace = Device.dwTimeGlobal + 1000;
                Msg("* [SunShadow] frame=%u size=%u ranges=%.1f,%.1f,%.1f casters=%u,%u,%u skinned=%u,%u,%u",
                    Device.dwFrame, state.resolution, state.splits.x, state.splits.y, state.splits.z, counts[0], counts[1], counts[2],
                    skinnedCounts[0], skinnedCounts[1], skinnedCounts[2]);
                const auto globals = BuildStaticGlobals();
                Msg("* [SunShadow] weather=%s sun=%.4f,%.4f,%.4f direction=%.4f,%.4f,%.4f ambient=%.4f,%.4f,%.4f hemi=%.4f,%.4f,%.4f/%.4f",
                    g_pGamePersistent->Environment().GetWeather().c_str(),
                    globals.L_sun_color.x, globals.L_sun_color.y, globals.L_sun_color.z,
                    globals.L_sun_dir_w.x, globals.L_sun_dir_w.y, globals.L_sun_dir_w.z,
                    globals.L_ambient.x, globals.L_ambient.y, globals.L_ambient.z,
                    globals.L_hemi_color.x, globals.L_hemi_color.y, globals.L_hemi_color.z, globals.L_hemi_color.w);
            }
        });
    return pass.texture;
}
}
