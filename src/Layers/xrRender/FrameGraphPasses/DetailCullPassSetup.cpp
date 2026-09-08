#include "stdafx.h"
#include "DetailCullPassSetup.h"
#include "Layers/xrRender/FGDetailManager.h"
#include "Layers/xrRender/FrameGraph/FrameGraph.h"
#include "Layers/xrRender/FrameGraph/RenderPassBuilder.h"
#include "Layers/xrRender/RenderContext/RenderDevice.h"
#include "Layers/xrRender/RenderContext/RenderContext.h"
#include "Layers/xrRender/Profiler/GPUProfiler.h"

extern ENGINE_API float ps_r3_grass_blade_width;

namespace xray::render::fg::passes
{
using namespace framegraph;

struct DetailCullPassData {
    VirtualResourceHandle hiZPyramid;
    fg::RenderDevice* device;
    fg::FGDetailManager* detailManager;
    u32 hiZWidth;
    u32 hiZHeight;
    u32 hiZMipLevels;
    Fmatrix prevViewProj;
    bool hasPrevViewProj;
    xray::profiler::GPUProfiler* gpuProfiler;
    DetailPassState* detailState;
};

VirtualResourceHandle setupDetailCullPass(
    FrameGraph& fg,
    fg::RenderDevice* device,
    fg::FGDetailManager* detailManager,
    VirtualResourceHandle hiZPyramid,
    u32 hiZWidth,
    u32 hiZHeight,
    u32 hiZMipLevels,
    const Fmatrix* prevViewProj,
    xray::profiler::GPUProfiler* gpuProfiler,
    DetailPassState* detailState)
{
    if (!detailManager || !detailManager->perlin4dTexture) return {};
    ResourceDesc noiseDesc;
    noiseDesc.type = ResourceDesc::Type::Texture3D;
    noiseDesc.width = noiseDesc.height = noiseDesc.depth = FGDetailManager::PERLIN4D_TEXTURE_SIZE;
    noiseDesc.format = nvrhi::Format::RGBA16_FLOAT;
    noiseDesc.isUAV = true;
    noiseDesc.isTransient = false;
    auto noise = fg.ImportTexture("DetailWindNoise", detailManager->perlin4dTexture, noiseDesc);

    // Loading, a camera cut or resize can invalidate the previous depth pyramid.
    // Bind a cleared reverse-Z far plane for that frame: culling still runs,
    // but unavailable history cannot hide grass or create a null texture binding.
    if (!hiZPyramid.is_valid()) {
        ResourceDesc emptyDesc;
        emptyDesc.type = ResourceDesc::Type::Texture2D;
        emptyDesc.debugName = "Detail.EmptyHiZ";
        emptyDesc.width = emptyDesc.height = 1;
        emptyDesc.format = nvrhi::Format::R32_FLOAT;
        emptyDesc.isUAV = true;
        hiZPyramid = fg.CreateTexture("Detail.EmptyHiZ", emptyDesc);
        hiZWidth = hiZHeight = hiZMipLevels = 1;
        auto clear = fg.AddPass("Detail.EmptyHiZ");
        fg.PassWrite(clear, hiZPyramid, ResourceState::UnorderedAccess);
        fg.SetPassCallback(clear, [hiZPyramid](fg::RenderContext& context, const FrameGraph& graph) {
            context.GetCommandList()->clearTextureFloat(graph.GetPhysicalTexture(hiZPyramid),
                nvrhi::AllSubresources, nvrhi::Color(0.f));
        });
    }

    Fmatrix capturedPrevViewProj;
    bool hasPrevViewProj = (prevViewProj != nullptr);
    if (hasPrevViewProj)
        capturedPrevViewProj = *prevViewProj;
    else
        capturedPrevViewProj.identity();

    fg.addCallbackPass<DetailCullPassData>(
        "DetailCull",
        [&, hiZPyramid, hiZWidth, hiZHeight, hiZMipLevels, capturedPrevViewProj, hasPrevViewProj, gpuProfiler, detailState](
            FrameGraph& builder, PassHandle passHandle, DetailCullPassData& data) {
            RenderPassBuilder passBuilder(builder, passHandle);
            // Instance generation and shared wind must finish before shadow and color draws.
            passBuilder.write(noise, ResourceState::UnorderedAccess);
            passBuilder.sideEffects();

            data.device = device;
            data.detailManager = detailManager;
            data.hiZWidth = hiZWidth;
            data.hiZHeight = hiZHeight;
            data.hiZMipLevels = hiZMipLevels;
            data.prevViewProj = capturedPrevViewProj;
            data.hasPrevViewProj = hasPrevViewProj;
            data.gpuProfiler = gpuProfiler;
            data.detailState = detailState;

            data.hiZPyramid = passBuilder.read(hiZPyramid, ResourceState::ShaderResource);
        },
        [](const DetailCullPassData& data, const FrameGraph& fg, fg::RenderContext* ctx)
        {
            ZoneScoped;
            ZoneName("DetailCullPass", 14);

            if (!data.detailManager)
                return;

            if (!psDeviceFlags.is(rsDrawDetails))
                return;

            bool detailPipelineValid = (data.detailManager->instanceGenPipeline && data.detailManager->slotDataBuffer);
            if (!detailPipelineValid)
                return;

            nvrhi::ICommandList* cmdList = ctx->GetCommandList();
            if (!cmdList)
                return;

            if (data.detailState && !data.detailState->detailDataUploaded)
            {
                data.detailManager->UploadBufferData(cmdList);
                data.detailState->detailDataUploaded = true;
            }

            if (data.detailState && data.detailState->lastBladeWidth != ps_r3_grass_blade_width)
            {
                data.detailManager->RegenerateBladeGeometry(cmdList);
                data.detailState->lastBladeWidth = ps_r3_grass_blade_width;
            }

            nvrhi::ITexture* hiZTexture = fg.GetPhysicalTexture(data.hiZPyramid);
            const float fadeDistance = g_pGamePersistent->Environment().CurrentEnv.far_plane;
            Fmatrix effectivePrevViewProj = data.hasPrevViewProj ? data.prevViewProj : Device.mFullTransform;

            data.detailManager->DispatchCulling(
                cmdList,
                data.device->GetNVRHIDevice(),
                hiZTexture,
                effectivePrevViewProj,
                fadeDistance,
                data.hiZWidth,
                data.hiZHeight,
                data.hiZMipLevels,
                data.gpuProfiler
            );

            UpdateDetailWind(data.detailManager);
            data.detailManager->DispatchPerlin4DCompute(cmdList, data.device->GetNVRHIDevice(), Device.fTimeGlobal);
            data.detailManager->ScheduleStatsReadback(cmdList, data.device->GetNVRHIDevice());
        }
    );
    return noise;
}

} // namespace xray::render::fg::passes
