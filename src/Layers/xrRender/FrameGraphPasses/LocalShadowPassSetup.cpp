#include "stdafx.h"
#include "LocalShadowPassSetup.h"
#include "SkinningPassSetup.h"
#include "PassCommon.h"
#include "Layers/xrRender/Decals/OverlayManager.h"
#include "Layers/xrRender/ClusteredLightManager.h"
#include "Layers/xrRender/light.h"
#include "Layers/xrRender/FrameGraph/FrameGraph.h"
#include "Layers/xrRender/FrameGraph/RenderPassBuilder.h"
#include "Layers/xrRender/FrameGraph/PassResourceCache.h"
#include "Layers/xrRender/Geometry/MaterialCache.h"
#include "Layers/xrRender/Geometry/GeometryBatch.h"
#include "Layers/xrRender/RenderContext/RenderDevice.h"
#include "Layers/xrRender/RenderContext/RenderContext.h"
#include "Layers/xrRender/Bindless/MaterialBuffer.h"
#include "Layers/xrRender/GPUCullingManager.h"
#include "Layers/xrRender/xrRender_console.h"
#include <chrono>

namespace xray::render::fg::passes {
namespace {
// Conservative intersection: reject only when one frustum lies entirely
// outside a plane of the other. False positives cost work, never missing shadows.
bool ShadowFaceReachesCamera(const Fmatrix& vp, const CFrustum& face,
    const Fvector (&cameraCorners)[8], const CFrustum& camera)
{
    for (u32 p = 0; p < face.p_count; ++p) {
        bool outside = true;
        for (const auto& corner : cameraCorners)
            if (face.planes[p].classify(corner) <= .05f) { outside = false; break; }
        if (outside) return false;
    }
    Fmatrix inverse; inverse.invert_44(vp);
    Fvector corners[8]; u32 index = 0;
    for (float z : {0.f, 1.f}) for (float y : {-1.f, 1.f}) for (float x : {-1.f, 1.f})
        inverse.transform(corners[index++], Fvector{x,y,z});
    for (u32 p = 0; p < camera.p_count; ++p) {
        bool outside = true;
        for (const auto& corner : corners)
            if (camera.planes[p].classify(corner) <= .05f) { outside = false; break; }
        if (outside) return false;
    }
    return true;
}

void PrepareLocalShadows(LocalShadowPassState& state)
{
    auto& lights = ClusteredLightManager::Instance();
    const auto& sources = lights.GetLightSources();
    state.matrices.clear(); state.frusta.clear(); state.owners.clear(); state.lightFaces.clear(); state.visibleFaces.clear();
    CFrustum camera; camera.CreateFromMatrix(Device.mFullTransform, FRUSTUM_P_ALL);
    Fmatrix inverseCamera; inverseCamera.invert_44(Device.mFullTransform);
    Fvector cameraCorners[8]; u32 corner = 0;
    for (float z : {0.f, 1.f}) for (float y : {-1.f, 1.f}) for (float x : {-1.f, 1.f})
        inverseCamera.transform(cameraCorners[corner++], Fvector{x,y,z});
    state.pointLights = state.spotLights = state.omittedLights = 0;
    state.resolution = ps_r_local_shadow_resolution;
    xr_vector<u32> candidates;
    for (u32 i = 0; i < sources.size(); ++i) {
        lights.SetShadowBase(i, 0);
        const auto* light = sources[i];
        if (ps_r_local_shadows && !strstr(Core.Params, "-noshadows") &&
            light->flags.bShadow && light->range > .1f &&
            (light->flags.type == IRender_Light::POINT || light->flags.type == IRender_Light::SPOT ||
                light->flags.type == IRender_Light::OMNIPART))
            candidates.push_back(i);
    }
    // A bounded pool avoids runaway depth-map allocation in scenes with many
    // lights. Prefer the largest angular influence; expose and trace the budget.
    std::stable_sort(candidates.begin(), candidates.end(), [&](u32 a, u32 b) {
        const auto score = [&](u32 i) {
            return sources[i]->range / (1.f + sources[i]->position.distance_to(Device.vCameraPosition));
        };
        return score(a) > score(b);
    });
    const Fvector directions[] = {{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}};
    const Fvector ups[] = {{0,1,0}, {0,1,0}, {0,0,-1}, {0,0,1}, {0,1,0}, {0,1,0}};
    for (u32 index : candidates) {
        const auto* light = sources[index];
        const bool point = light->flags.type == IRender_Light::POINT;
        const u32 faces = point ? 6 : 1;
        if (state.matrices.size() + faces > u32(ps_r_local_shadow_faces)) {
            ++state.omittedLights;
            continue;
        }
        lights.SetShadowBase(index, u32(state.matrices.size()) + 1);
        if (point) ++state.pointLights; else ++state.spotLights;
        for (u32 face = 0; face < faces; ++face) {
            Fvector direction = point ? directions[face] : light->direction;
            direction.normalize_safe();
            Fvector up = point ? ups[face] : Fvector{0,1,0};
            if (!point) {
                if (light->right.square_magnitude() > EPS) {
                    up.crossproduct(direction, light->right);
                    up.normalize_safe();
                } else if (_abs(direction.y) > .99f) up.set(0,0,1);
            }
            Fmatrix view, projection, vp;
            view.build_camera_dir(light->position, direction, up);
            const float nearPlane = clampr(light->virtual_size, .01f, light->range * .1f);
            const float farPlane = light->range + EPS_S;
            projection.build_projection(point ? PI_DIV_2 : light->cone + deg2rad(3.5f),
                1.f, nearPlane, farPlane);
            // The engine's camera projection uses reversed Z. These maps share
            // the sun-shadow LESS_EQUAL pipeline, clear depth 1 and comparison
            // sampler, so use ordinary depth: near -> 0, far -> 1.
            projection._33 = farPlane / (farPlane - nearPlane);
            projection._43 = -nearPlane * projection._33;
            vp.mul(projection, view);
            state.matrices.push_back(vp);
            CFrustum frustum; frustum.CreateFromMatrix(vp, FRUSTUM_P_ALL);
            state.frusta.push_back(frustum);
            state.owners.push_back(index);
            state.lightFaces.push_back(face);
            state.visibleFaces.push_back(ShadowFaceReachesCamera(vp, frustum, cameraCorners, camera));
        }
    }
}
}

framegraph::VirtualResourceHandle setupLocalShadowPass(
    framegraph::FrameGraph& graph, RenderDevice* device, GPUCullingManager* geometry,
    MaterialCache* materials, framegraph::VirtualResourceHandle uploadDependency,
    framegraph::VirtualResourceHandle detailDependency, FGDetailManager* details,
    LocalShadowPassState& state, const GeometryCollector* collector, SkinningPassState& skinning,
    decals::OverlayManager* overlays)
{
    // CPU metadata is ready before ClusterLightAssign uploads its light buffer.
    PrepareLocalShadows(state);
    InitializeShadowMapResources(device, state.drawing);
    struct PassData {
        framegraph::VirtualResourceHandle texture;
        RenderDevice* device;
        GPUCullingManager* geometry;
        MaterialCache* materials;
        LocalShadowPassState* state;
        const GeometryCollector* collector;
        SkinningPassState* skinning;
        decals::OverlayManager* overlays;
        FGDetailManager* details;
    };
    auto& pass = graph.addCallbackPass<PassData>("Local shadow maps",
        [&](framegraph::FrameGraph& builder, framegraph::PassHandle handle, PassData& data) {
            framegraph::RenderPassBuilder pb(builder, handle);
            if (uploadDependency.is_valid()) pb.read(uploadDependency, framegraph::ResourceState::IndirectArgument);
            if (detailDependency.is_valid()) pb.read(detailDependency, framegraph::ResourceState::ShaderResource);
            framegraph::ResourceDesc desc;
            desc.type = framegraph::ResourceDesc::Type::Texture2DArray;
            desc.width = desc.height = state.matrices.empty() ? 1 : state.resolution;
            // Bucket capacity to avoid reallocating for every entering light.
            desc.arraySize = 1;
            while (desc.arraySize < state.matrices.size()) desc.arraySize *= 2;
            desc.arraySize = _min(desc.arraySize, MAX_LOCAL_SHADOW_FACES);
            desc.format = nvrhi::Format::D32;
            desc.isDepthStencil = true; desc.isTransient = false; desc.debugName = "rt_LocalShadow";
            data.texture = pb.createTexture("rt_LocalShadow", desc);
            data.device = device; data.geometry = geometry; data.materials = materials; data.state = &state;
            data.collector = collector; data.skinning = &skinning; data.overlays = overlays; data.details = details;
            if (!state.matrices.empty()) builder.SetPassParallelRecording(handle,
                [&data](RenderContext& context, const framegraph::FrameGraph&) {
                    if (!data.geometry || !data.geometry->IsMegaDataUploaded()) return;
                    data.materials->FinalizePendingMaterials(&context);
                    bindless::MaterialBuffer::Instance().Upload(&context);
                    GetOrCreateDrawIndexBuffer("SunShadow", context.GetCommandList()->getDevice());
                    if (data.overlays) data.overlays->UploadSplats(context.GetCommandList());
                    if (!data.skinning->initialized) return;
                    const auto& state = *data.state;
                    for (u32 face = 0; face < state.matrices.size(); ++face) {
                        if (!state.visibleFaces[face]) continue;
                        const auto* light = ClusteredLightManager::Instance().GetLightSources()[state.owners[face]];
                        const Fvector4 sphere{light->position.x, light->position.y, light->position.z, light->range};
                        PrepareSkinnedShadowBones(&context, data.geometry, data.collector, state.frusta[face], &sphere);
                    }
                });
        },
        [](const PassData& data, const framegraph::FrameGraph& graph, RenderContext* context) {
            auto& state = *data.state;
            const bool trace = strstr(Core.Params, "-shadow_trace") && Device.dwTimeGlobal >= state.nextTrace;
            using Clock = std::chrono::steady_clock;
            auto stamp = Clock::now();
            double cpu[6] = {};
            const auto lap = [&](u32 part) {
                if (!trace) return;
                const auto now = Clock::now();
                cpu[part] += std::chrono::duration<double, std::milli>(now - stamp).count();
                stamp = now;
            };
            auto* command = context->GetCommandList();
            auto* texture = graph.GetPhysicalTexture(data.texture);
            auto* geometry = data.geometry;
            const bool canRender = !state.matrices.empty() && geometry && geometry->IsMegaDataUploaded();
            if (!canRender) {
                command->clearDepthStencilTexture(texture, nvrhi::AllSubresources, true, 1.f, false, 0);
            } else {
                // Every rendered face receives a complete cached depth copy.
                // Clear only skipped faces; unused capacity has no light index.
                for (u32 face = 0; face < state.matrices.size();) {
                    if (state.visibleFaces[face]) { ++face; continue; }
                    const u32 first = face++;
                    while (face < state.matrices.size() && !state.visibleFaces[face]) ++face;
                    command->clearDepthStencilTexture(texture,
                        nvrhi::TextureSubresourceSet(0, 1, first, face - first), true, 1.f, false, 0);
                }
            }
            if (!state.matrices.empty())
                command->writeBuffer(ClusteredLightManager::Instance().GetShadowMatricesBuffer(),
                    state.matrices.data(), state.matrices.size() * sizeof(Fmatrix));
            u32 worldCount = 0, skinnedCount = 0, detailCount = 0, renderedFaces = 0, staticUpdates = 0;
            u32 treeCount = 0;
            if (canRender) {
                if (!context->IsParallelRecording()) {
                    data.materials->FinalizePendingMaterials(context);
                    bindless::MaterialBuffer::Instance().Upload(context);
                }
                if (state.outputTexture != texture) {
                    state.outputFramebuffers.clear();
                    state.outputTexture = texture;
                }
                state.outputFramebuffers.resize(texture->getDesc().arraySize);
                lap(0);
                xr_vector<u32> candidates[3];
                xr_vector<u32> treeCandidates[3], animatedTrees;
                const auto& staticObjects = geometry->GetStaticObjectData();
                for (u32 i = 0; i < staticObjects.size(); ++i)
                    if (staticObjects[i].flags & GPU_INSTANCE_TREE_WIND) animatedTrees.push_back(i);
                xr_vector<const GeometryBatch*> skinnedBatches, skinnedCandidates;
                if (data.collector) {
                    for (const auto& batch : data.collector->GetBatches())
                        if (batch.isSkinned && batch.vertexBuffer && batch.indexBuffer)
                            skinnedBatches.push_back(&batch);
                }
                const auto collect = [&](u32 group, const xr_vector<GPUObjectData>& objects, const light* source) {
                    candidates[group].clear();
                    for (u32 i = 0; i < objects.size(); ++i) {
                        const float radius = source->range + objects[i].radius;
                        if (objects[i].position.distance_to_sqr(source->position) <= radius * radius)
                            candidates[group].push_back(i);
                    }
                };
                u32 lastOwner = u32(-1);
                bool staticCandidatesReady = false;
                for (u32 face = 0; face < state.matrices.size(); ++face) {
                    if (!state.visibleFaces[face]) continue;
                    ++renderedFaces;
                    const auto* source = ClusteredLightManager::Instance().GetLightSources()[state.owners[face]];
                    if (state.owners[face] != lastOwner) {
                        lastOwner = state.owners[face];
                        staticCandidatesReady = false;
                        collect(2, geometry->GetDynamicObjectData(), source);
                        treeCandidates[0].clear();
                        for (u32 i : animatedTrees) {
                            const float radius = source->range + staticObjects[i].radius;
                            if (staticObjects[i].position.distance_to_sqr(source->position) <= radius * radius)
                                treeCandidates[0].push_back(i);
                        }
                        // A cubemap's corners extend beyond the spherical light
                        // range. Keep only characters that can shadow this light,
                        // then apply the existing face frustum test when drawing.
                        skinnedCandidates.clear();
                        for (const auto* batch : skinnedBatches) {
                            const float radius = source->range + batch->worldBoundsRadius;
                            if (batch->worldBoundsCenter.distance_to_sqr(source->position) <= radius * radius)
                                skinnedCandidates.push_back(batch);
                        }
                    }
                    lap(1);
                    nvrhi::FramebufferDesc fb;
                    fb.setDepthAttachment(nvrhi::FramebufferAttachment().setTexture(texture).setArraySlice(face));
                    auto& framebuffer = state.outputFramebuffers[face];
                    if (!framebuffer) framebuffer = command->getDevice()->createFramebuffer(fb);
                    R_ASSERT2(framebuffer, "Local shadow framebuffer creation failed");
                    auto& saved = state.staticCache[source];
                    const u32 lightFace = state.lightFaces[face];
                    const u32 faceCount = source->flags.type == IRender_Light::POINT ? 6 : 1;
                    const bool reset = !saved.texture || saved.texture->getDesc().width != state.resolution ||
                        saved.texture->getDesc().arraySize != faceCount ||
                        saved.staticInstances != geometry->GetStaticInstanceBuffer() ||
                        saved.terrainInstances != geometry->GetTerrainInstanceBuffer();
                    if (reset) {
                        nvrhi::TextureDesc desc;
                        desc.width = desc.height = state.resolution; desc.arraySize = faceCount;
                        desc.dimension = nvrhi::TextureDimension::Texture2DArray;
                        desc.format = nvrhi::Format::D32; desc.isRenderTarget = true;
                        desc.initialState = nvrhi::ResourceStates::DepthWrite; desc.keepInitialState = true;
                        desc.debugName = "LocalStaticShadow";
                        saved.texture = command->getDevice()->createTexture(desc);
                        R_ASSERT2(saved.texture, "Static local shadow cache allocation failed");
                        saved.staticInstances = geometry->GetStaticInstanceBuffer();
                        saved.terrainInstances = geometry->GetTerrainInstanceBuffer();
                        for (bool& valid : saved.valid) valid = false;
                        for (auto& framebuffer : saved.framebuffers) framebuffer = nullptr;
                    }
                    const u64 revision = bindless::MaterialBuffer::Instance().GetShadowRevision();
                    if (saved.materialRevision != revision) {
                        for (bool& valid : saved.valid) valid = false;
                        saved.materialRevision = revision;
                    }
                    saved.lastUsedFrame = Device.dwFrame;
                    if (!ps_r_local_shadow_cache || !saved.valid[lightFace] || memcmp(&saved.matrices[lightFace], &state.matrices[face], sizeof(Fmatrix))) {
                        // Warm cached faces need no static-world candidate scan.
                        if (!staticCandidatesReady) {
                            collect(0, geometry->GetStaticObjectData(), source);
                            collect(1, geometry->GetTerrainObjectData(), source);
                            staticCandidatesReady = true;
                        }
                        ++staticUpdates;
                        command->clearDepthStencilTexture(saved.texture,
                            nvrhi::TextureSubresourceSet(0, 1, lightFace, 1), true, 1.f, false, 0);
                        nvrhi::FramebufferDesc staticFB;
                        staticFB.setDepthAttachment(nvrhi::FramebufferAttachment().setTexture(saved.texture).setArraySlice(lightFace));
                        auto& target = saved.framebuffers[lightFace];
                        if (!target) target = command->getDevice()->createFramebuffer(staticFB);
                        R_ASSERT2(target, "Static local shadow framebuffer creation failed");
                        worldCount += DrawWorldShadowMap(context, data.device, geometry,
                            state.matrices[face], state.frusta[face], target, state.drawing, candidates, 3, 1);
                        saved.matrices[lightFace] = state.matrices[face]; saved.valid[lightFace] = true;
                    }
                    command->copyTexture(texture, nvrhi::TextureSlice().setArraySlice(face),
                        saved.texture, nvrhi::TextureSlice().setArraySlice(lightFace));
                    lap(2);
                    worldCount += DrawWorldShadowMap(context, data.device, geometry,
                        state.matrices[face], state.frusta[face], framebuffer, state.drawing, candidates, 4);
                    // Wind moves authored tree vertices even though their instance transforms are static.
                    // Draw them over the fixed-geometry cache each frame.
                    treeCount += DrawWorldShadowMap(context, data.device, geometry,
                        state.matrices[face], state.frusta[face], framebuffer, state.drawing, treeCandidates, 1, 2);
                    lap(3);
                    skinnedCount += DrawSkinnedSunShadows(context, data.device, geometry, data.collector,
                        data.overlays, state.matrices[face], state.frusta[face], framebuffer, *data.skinning, &skinnedCandidates);
                    lap(4);
                    const Fvector4 lightSphere{source->position.x, source->position.y, source->position.z, source->range};
                    detailCount += DrawDetailShadowMap(context, data.device, data.details, state.drawing,
                        state.matrices[face], state.frusta[face], framebuffer, &lightSphere) ? 1 : 0;
                    lap(5);
                }
            }
            // Keep only recently used lights; the command list owns in-flight
            // resource references. Stationary maps survive camera movement.
            for (auto it = state.staticCache.begin(); it != state.staticCache.end();) {
                if (Device.dwFrame - it->second.lastUsedFrame > 8) it = state.staticCache.erase(it);
                else ++it;
            }
            if (trace) {
                state.nextTrace = Device.dwTimeGlobal + 1000;
                Msg("* [LocalShadow] frame=%u size=%u points=%u spots=%u faces=%u rendered=%u static_updates=%u budget=%d omitted=%u world=%u skinned=%u detail=%u trees=%u",
                    Device.dwFrame, state.resolution, state.pointLights, state.spotLights, u32(state.matrices.size()),
                    renderedFaces, staticUpdates, ps_r_local_shadow_faces, state.omittedLights, worldCount, skinnedCount, detailCount, treeCount);
                Msg("* [LocalShadowCPU] frame=%u time=%u init_ms=%.3f candidates_ms=%.3f static_copy_ms=%.3f dynamic_ms=%.3f skinned_ms=%.3f detail_ms=%.3f",
                    Device.dwFrame, Device.dwTimeGlobal, cpu[0], cpu[1], cpu[2], cpu[3], cpu[4], cpu[5]);
            }
        });
    return pass.texture;
}
}
