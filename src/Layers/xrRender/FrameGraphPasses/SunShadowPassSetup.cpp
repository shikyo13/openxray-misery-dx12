#include "stdafx.h"
#include "SunShadowPassSetup.h"
#include "SkinningPassSetup.h"
#include "DetailPassSetup.h"
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

namespace xray::render::fg { extern int ps_r__detail_gpu; }

namespace xray::render::fg::passes {
namespace {
struct alignas(16) SunShadowDrawConstants {
    Fmatrix viewProjection;
    Fvector4 options; // x: terrain uses an opaque material table
};
static_assert(sizeof(SunShadowDrawConstants) == 80);

struct alignas(16) DetailShadowCullConstants {
    Fvector4 planes[6];
    u32 slotCount, capacity;
    float maximumRadius;
    u32 instanceCapacity;
    Fvector4 rootRadii[16];
    Fvector4 cameraRange;
};
static_assert(sizeof(DetailShadowCullConstants) == 384);

bool DrawDetailSunShadows(RenderContext* context, RenderDevice* device, FGDetailManager* dm,
    SunShadowPassState& state, u32 cascade, nvrhi::IFramebuffer* framebuffer)
{
    // This path renders MISERY's authored detail meshes. Procedural blades use
    // different geometry and must not cast the silhouettes of those meshes.
    if (!ps_r_detail_shadows || ps_r__detail_gpu || !psDeviceFlags.is(rsDrawDetails) || !dm ||
        !dm->instanceGenPipeline || !dm->slotAABBBuffer || !dm->generatedInstancesBuffer ||
        !dm->buildDetailsTexture || !dm->pulledIndexBuffer || !dm->maxPulledIndexCount || !dm->slot_count)
        return false;
    auto* nv = device->GetNVRHIDevice();
    auto* command = context->GetCommandList();
    auto* loader = GEnv.Render->GetShaderLoader();
    auto& cache = framegraph::GetPassResourceCache();
    if (!state.detailPipeline) {
        auto vs = loader->LoadVertexShader("detail_shadow");
        auto ps = loader->LoadPixelShader("detail_shadow");
        auto cs = loader->LoadComputeShader("detail_shadow_cull");
        R_ASSERT2(vs.handle && ps.handle && cs.handle, "Detail sun shadow shader compilation failed");
        state.detailLayout = cache.GetOrCreateBindingLayoutFromReflection("DetailShadow", *vs.reflection, *ps.reflection, nv);
        state.detailCullLayout = cache.GetOrCreateBindingLayoutFromReflection("DetailShadowCull", *cs.reflection, nv);
        nvrhi::GraphicsPipelineDesc desc;
        desc.VS = vs.handle; desc.PS = ps.handle;
        desc.bindingLayouts = {state.detailLayout};
        desc.primType = nvrhi::PrimitiveType::TriangleList;
        desc.renderState.depthStencilState.depthTestEnable = true;
        desc.renderState.depthStencilState.depthWriteEnable = true;
        desc.renderState.depthStencilState.depthFunc = nvrhi::ComparisonFunc::LessOrEqual;
        desc.renderState.rasterState.cullMode = nvrhi::RasterCullMode::None;
        desc.renderState.rasterState.depthBias = 64;
        desc.renderState.rasterState.slopeScaledDepthBias = 1.5f;
        nvrhi::FramebufferInfoEx fb; fb.depthFormat = nvrhi::Format::D32;
        state.detailPipeline = cache.GetOrCreatePipeline("DetailShadow", desc, fb, nv);
        nvrhi::ComputePipelineDesc compute;
        compute.CS = cs.handle; compute.bindingLayouts = {state.detailCullLayout};
        state.detailCullPipeline = cache.GetOrCreateComputePipeline("DetailShadowCull", compute, nv);
        R_ASSERT2(state.detailPipeline && state.detailCullPipeline, "Detail sun shadow pipeline creation failed");
        state.detailConstants = cache.GetOrCreateVolatileCB("DetailShadow", "DetailGlobals", sizeof(FGDetailManager::DetailFrameConstants), device);
        state.detailCullConstants = cache.GetOrCreateVolatileCB("DetailShadow", "DetailShadowCull", sizeof(DetailShadowCullConstants), device);
        nvrhi::BufferDesc args;
        args.byteSize = 5 * sizeof(u32); args.isDrawIndirectArgs = true;
        args.canHaveUAVs = true; args.canHaveRawViews = true;
        args.initialState = nvrhi::ResourceStates::IndirectArgument; args.keepInitialState = true;
        args.debugName = "DetailShadowDrawArgs";
        state.detailDrawArgs = nv->createBuffer(args);
        R_ASSERT(state.detailDrawArgs);
    }
    // Detail preparation grows this to the generation capacity before every
    // regeneration, then shrinks it using the actual complete GPU instance count.
    if (state.detailCapacity != dm->visibleBufferCapacity) {
        nvrhi::BufferDesc visible;
        visible.byteSize = u64(dm->visibleBufferCapacity) * sizeof(u32);
        visible.structStride = sizeof(u32); visible.canHaveUAVs = true;
        visible.initialState = nvrhi::ResourceStates::ShaderResource; visible.keepInitialState = true;
        visible.debugName = "DetailShadowVisible";
        state.detailVisible = nv->createBuffer(visible);
        R_ASSERT2(state.detailVisible, "Detail sun shadow visibility allocation failed");
        state.detailCapacity = dm->visibleBufferCapacity;
    }
    DetailShadowCullConstants constants{};
    R_ASSERT(state.frustum[cascade].p_count == 6);
    for (u32 p = 0; p < 6; ++p) {
        const auto& plane = state.frustum[cascade].planes[p];
        constants.planes[p].set(plane.n.x, plane.n.y, plane.n.z, plane.d);
    }
    constants.slotCount = dm->slot_count; constants.capacity = state.detailCapacity;
    constants.instanceCapacity = dm->generatedInstancesCapacity;
    constants.cameraRange.set(Device.vCameraPosition.x, Device.vCameraPosition.y,
        Device.vCameraPosition.z, ps_r_detail_shadow_distance);
    CopyMemory(constants.rootRadii, dm->modelRootRadii, sizeof(constants.rootRadii));
    for (float radius : dm->modelRootRadii) constants.maximumRadius = _max(constants.maximumRadius, radius * 4.f);
    const u32 args[] = {dm->maxPulledIndexCount, 0, 0, 0, 0};
    command->writeBuffer(state.detailDrawArgs, args, sizeof(args));
    command->writeBuffer(state.detailCullConstants, &constants, sizeof(constants));
    const auto* cs = loader->GetCachedReflection("detail_shadow_cull", ".cs");
    framegraph::BindingSetBuilder cb(*cs, nv, "DetailShadowCull");
    cb.ConstantBuffer("DetailShadowCull", state.detailCullConstants)
        .BufferSRV("t_Slots", dm->slotAABBBuffer).BufferSRV("t_Instances", dm->generatedInstancesBuffer)
        .BufferUAV("u_Visible", state.detailVisible).BufferUAV("u_DrawArgs", state.detailDrawArgs);
    auto cullBindings = cache.GetOrCreateBindingSet(cb.Build(), state.detailCullLayout, nv);
    R_ASSERT2(cullBindings, "Detail sun shadow culling bindings failed");
    nvrhi::ComputeState compute;
    compute.pipeline = state.detailCullPipeline; compute.bindings = {cullBindings};
    command->setComputeState(compute);
    command->dispatch((dm->slot_count + 255) / 256, 1, 1);

    const auto drawConstants = BuildDetailFrameConstants(dm, state.viewProjection[cascade]);
    command->writeBuffer(state.detailConstants, &drawConstants, sizeof(drawConstants));
    const auto* vs = loader->GetCachedReflection("detail_shadow", ".vs");
    const auto* ps = loader->GetCachedReflection("detail_shadow", ".ps");
    framegraph::BindingSetBuilder db(*vs, *ps, nv, "DetailShadow");
    db.ConstantBuffer("DetailGlobals", state.detailConstants)
        .BufferSRV("visible_indices", state.detailVisible).BufferSRV("detail_models", dm->detailModelsBuffer)
        .BufferSRV("pulled_vertices", dm->pulledVertexBuffer).BufferSRV("all_instances", dm->generatedInstancesBuffer)
        .Texture("g_Perlin4D", dm->perlin4dTexture)
        .Texture("t_DetailAtlas", dm->buildDetailsTexture);
    auto drawBindings = cache.GetOrCreateBindingSet(db.Build(), state.detailLayout, nv);
    R_ASSERT2(drawBindings, "Detail sun shadow draw bindings failed");
    nvrhi::GraphicsState draw;
    draw.pipeline = state.detailPipeline; draw.framebuffer = framebuffer; draw.bindings = {drawBindings};
    draw.indexBuffer = {dm->pulledIndexBuffer, nvrhi::Format::R16_UINT, 0};
    draw.indirectParams = state.detailDrawArgs;
    draw.viewport.addViewportAndScissorRect(nvrhi::Viewport(float(state.resolution), float(state.resolution)));
    command->setGraphicsState(draw);
    command->drawIndexedIndirect(0);
    return true;
}

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

void PrepareSunShadowCascades(SunShadowPassState& state)
{
    PrepareCascades(state);
}

framegraph::VirtualResourceHandle setupSunShadowPass(
    framegraph::FrameGraph& graph, RenderDevice* device, GPUCullingManager* geometry,
    MaterialCache* materials, framegraph::VirtualResourceHandle uploadDependency,
    framegraph::VirtualResourceHandle detailDependency, FGDetailManager* details,
    SunShadowPassState& state, const GeometryCollector* collector, SkinningPassState& skinning,
    decals::OverlayManager* overlays)
{
    // Shader hot-reload can clear the blackboard after spatial collection.
    // Recreate the same camera/light volumes if that happened this frame.
    PrepareSunShadowCascades(state);
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
        FGDetailManager* details;
    };
    auto& pass = graph.addCallbackPass<PassData>("Sun shadow maps",
        [&](framegraph::FrameGraph& builder, framegraph::PassHandle handle, PassData& data) {
            framegraph::RenderPassBuilder pb(builder, handle);
            if (uploadDependency.is_valid()) pb.read(uploadDependency, framegraph::ResourceState::IndirectArgument);
            if (detailDependency.is_valid()) pb.read(detailDependency, framegraph::ResourceState::ShaderResource);
            framegraph::ResourceDesc desc;
            desc.type = framegraph::ResourceDesc::Type::Texture2DArray;
            desc.width = desc.height = state.resolution;
            desc.arraySize = 3; desc.format = nvrhi::Format::D32;
            desc.isDepthStencil = true; desc.isTransient = false; desc.debugName = "rt_SunShadow";
            data.texture = pb.createTexture("rt_SunShadow", desc);
            data.device = device; data.geometry = geometry; data.materials = materials; data.state = &state;
            data.collector = collector; data.skinning = &skinning; data.overlays = overlays;
            data.details = details;
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
            bool detailDraws[3] = {};
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
                detailDraws[cascade] = DrawDetailSunShadows(context, data.device, data.details, state, cascade, framebuffer);
            }
            if (strstr(Core.Params, "-shadow_trace") && Device.dwTimeGlobal >= state.nextTrace) {
                state.nextTrace = Device.dwTimeGlobal + 1000;
                Msg("* [SunShadow] frame=%u size=%u ranges=%.1f,%.1f,%.1f casters=%u,%u,%u skinned=%u,%u,%u",
                    Device.dwFrame, state.resolution, state.splits.x, state.splits.y, state.splits.z, counts[0], counts[1], counts[2],
                    skinnedCounts[0], skinnedCounts[1], skinnedCounts[2]);
                Msg("* [DetailShadow] frame=%u draws=%u,%u,%u capacity=%u mode=%d distance=%.1f",
                    Device.dwFrame, detailDraws[0], detailDraws[1], detailDraws[2], state.detailCapacity, ps_r__detail_gpu, ps_r_detail_shadow_distance);
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
