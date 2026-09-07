// xrRender/FrameGraphPasses/TonemapPassSetup.cpp
#include "stdafx.h"
#include "TonemapPassSetup.h"
#include "ExposurePassSetup.h"
#include "Layers/xrRender/FrameGraph/FrameGraph.h"
#include "Layers/xrRender/FrameGraph/PassResourceCache.h"
#include "Layers/xrRender/FrameGraph/BindingSetBuilder.h"
#include "Layers/xrRender/FrameGraph/RenderPassBuilder.h"
#include "Layers/xrRender/FrameGraph/ShaderCache.h"
#include "Layers/xrRender/RenderContext/RenderContext.h"
#include "Layers/xrRender/FrameGraph/ShaderLoader.h"
#include "Layers/xrRender/RenderContext/RenderDevice.h"
#include "Layers/xrRender/ResourceManager/FGResourceManager.h"

namespace xray::render::fg {
    extern xray::render::FrameGraphRenderer RImplementation;
}

namespace xray::render::fg::passes {

namespace {
PostProcessConstants PreparePostProcess(const SPPInfo& ppi, TonemapPassState& state,
    fg::RenderDevice* device, u32 width, u32 height)
{
    auto* textures = device->GetFGResourceManager()->GetTextureManager();
    if (!state.noiseTexture) {
        state.noiseTexture = textures->GetNVRHITexture(textures->LoadTexture("fx" DELIMITER "fx_noise2"));
        R_ASSERT2(state.noiseTexture, "Post-process noise texture could not be loaded");
    }

    const u32 base = ppi.color_base;
    const u32 gray = ppi.color_gray;
    const bool colorChanged = _abs(int(color_get_R(base)) - 127) > 2 ||
        _abs(int(color_get_G(base)) - 127) > 2 || _abs(int(color_get_B(base)) - 127) > 2;
    const bool addChanged = _abs(int(ppi.color_add.r * 255.f)) > 2 ||
        _abs(int(ppi.color_add.g * 255.f)) > 2 || _abs(int(ppi.color_add.b * 255.f)) > 2;
    const bool colorMap = ppi.cm_influence > .001f;
    const bool enabled = ppi.blur > .001f || ppi.gray > .001f || ppi.noise.intensity > .001f ||
        _abs(ppi.duality.h) > .001f || _abs(ppi.duality.v) > .001f || colorChanged || addChanged || colorMap;

    if (colorMap) {
        const shared_str names[] = { ppi.cm_tex1, ppi.cm_tex2.size() ? ppi.cm_tex2 : ppi.cm_tex1 };
        for (u32 i = 0; i < 2; ++i) {
            if (state.colorMapNames[i] != names[i] || !state.colorMaps[i]) {
                R_ASSERT2(names[i].size(), "Active post-process color map has no texture name");
                state.colorMaps[i] = textures->GetNVRHITexture(textures->LoadTexture(names[i].c_str()));
                R_ASSERT2(state.colorMaps[i], "Post-process color map could not be loaded");
                state.colorMapNames[i] = names[i];
            }
        }
    }

    PostProcessConstants result{};
    // Preserve the original packed color and inverse-alpha blend equations.
    result.colorBase.set(color_get_R(base) / 255.f, color_get_G(base) / 255.f, color_get_B(base) / 255.f,
        clampr(iFloor((1.f - ppi.noise.intensity) * 255.f), 0, 255) / 255.f);
    result.colorGray.set(color_get_R(gray) / 255.f, color_get_G(gray) / 255.f, color_get_B(gray) / 255.f,
        clampr(iFloor((1.f - ppi.gray) * 255.f), 0, 255) / 255.f);
    result.colorAdd.set(ppi.color_add.r, ppi.color_add.g, ppi.color_add.b, 0.f);
    // The fullscreen triangle already samples pixel centers; blur adds the authored offset.
    result.dualityBlur.set(.5f * ppi.blur / width, .5f * ppi.blur / height,
        .5f * _abs(ppi.duality.h), .5f * _abs(ppi.duality.v));
    result.controls.set(enabled ? 1.f : 0.f, colorMap ? ppi.cm_influence : 0.f, ppi.cm_interpolate, 0.f);

    if (enabled && ppi.noise.intensity > 0.f) {
        const auto& desc = state.noiseTexture->getDesc();
        const u32 tw = u32(_max(1, iCeil(float(desc.width) * ppi.noise.grain + EPS_S)));
        const u32 th = u32(_max(1, iCeil(float(desc.height) * ppi.noise.grain + EPS_S)));
        state.noiseTime -= Device.fTimeDelta;
        if (state.noiseTime < 0.f) {
            // Rendering noise must not consume the gameplay RNG sequence.
            auto random = [&state]() {
                state.noiseRandom ^= state.noiseRandom << 13;
                state.noiseRandom ^= state.noiseRandom >> 17;
                state.noiseRandom ^= state.noiseRandom << 5;
                return state.noiseRandom;
            };
            state.noiseShiftX = random() % tw;
            state.noiseShiftY = random() % th;
            const float period = 1.f / (_abs(ppi.noise.fps) + EPS_S);
            state.noiseTime += (std::floor(-state.noiseTime / period) + 1.f) * period;
        }
        result.noiseUV.set((state.noiseShiftX + .5f) / tw, (state.noiseShiftY + .5f) / th,
            float(width / tw) + 1.f, float(height / th) + 1.f);
    }

    if (strstr(Core.Params, "-pp_trace") && Device.dwTimeGlobal >= state.nextTraceTime) {
        state.nextTraceTime = Device.dwTimeGlobal + 1000;
        Msg("* [PostProcess] frame=%u enabled=%d blur=%.4f gray=%.4f dual=%.4f,%.4f noise=%.4f,%.4f,%.4f base=%.4f,%.4f,%.4f add=%.4f,%.4f,%.4f cm=%.4f,%.4f maps='%s','%s'",
            Device.dwFrame, int(enabled), ppi.blur, ppi.gray, ppi.duality.h, ppi.duality.v,
            ppi.noise.intensity, ppi.noise.grain, ppi.noise.fps, ppi.color_base.r, ppi.color_base.g, ppi.color_base.b,
            ppi.color_add.r, ppi.color_add.g, ppi.color_add.b, ppi.cm_influence, ppi.cm_interpolate,
            ppi.cm_tex1.size() ? ppi.cm_tex1.c_str() : "", ppi.cm_tex2.size() ? ppi.cm_tex2.c_str() : "");
    }
    return result;
}
}

void InitializeTonemapPass(nvrhi::IDevice* device, TonemapPassState& state) {
    if (state.initialized || !device) return;

    nvrhi::BufferDesc cbDesc;
    cbDesc.byteSize = sizeof(PostProcessConstants);
    cbDesc.isConstantBuffer = true;
    cbDesc.isVolatile = true;
    cbDesc.maxVersions = 16;
    cbDesc.debugName = "PostProcessParams";
    state.postProcessBuffer = device->createBuffer(cbDesc);
    R_ASSERT2(state.postProcessBuffer, "Post-process constant buffer creation failed");

    nvrhi::TextureDesc texDesc;
    texDesc.debugName = "FallbackExposure";
    texDesc.width = 1;
    texDesc.height = 1;
    texDesc.format = nvrhi::Format::R32_FLOAT;
    texDesc.initialState = nvrhi::ResourceStates::ShaderResource;
    texDesc.keepInitialState = true;

    state.fallbackExposureTexture = device->createTexture(texDesc);

    nvrhi::CommandListHandle cmdList = device->createCommandList();
    cmdList->open();
    float defaultExposure = 1.0f;
    cmdList->writeTexture(state.fallbackExposureTexture, 0, 0, &defaultExposure, sizeof(float));
    cmdList->close();
    device->executeCommandList(cmdList);

    if (GEnv.Render->GetShaderLoader()) {
        auto vsResult = GEnv.Render->GetShaderLoader()->LoadVertexShader("tonemap");
        auto psResult = GEnv.Render->GetShaderLoader()->LoadPixelShader("tonemap");
        if (vsResult.handle && psResult.handle) {
            auto& cache = framegraph::GetPassResourceCache();

            state.bindingLayout = cache.GetOrCreateBindingLayoutFromReflection(
                "TonemapPass", *vsResult.reflection, *psResult.reflection, device);

            if (state.bindingLayout) {
                nvrhi::GraphicsPipelineDesc pipeDesc;
                pipeDesc.setVertexShader(vsResult.handle);
                pipeDesc.setPixelShader(psResult.handle);
                pipeDesc.addBindingLayout(state.bindingLayout);
                pipeDesc.setPrimType(nvrhi::PrimitiveType::TriangleList);
                pipeDesc.renderState.blendState.targets[0].setBlendEnable(false);
                pipeDesc.renderState.depthStencilState.setDepthTestEnable(false);
                pipeDesc.renderState.depthStencilState.setDepthWriteEnable(false);
                pipeDesc.renderState.rasterState.setCullMode(nvrhi::RasterCullMode::None);

                nvrhi::FramebufferInfoEx fbInfo;
                nvrhi::Format fbFmt = nvrhi::Format::RGBA8_UNORM;
                if (GEnv.Backend && GEnv.Backend->GetBackBuffer())
                    fbFmt = GEnv.Backend->GetBackBuffer()->getDesc().format;
                fbInfo.addColorFormat(fbFmt);

                state.pipeline = cache.GetOrCreatePipeline("TonemapPass", pipeDesc, fbInfo, device);
            }
        }
    }

    state.initialized = true;
}

void ShutdownTonemapPass(TonemapPassState& state) {
    state.postProcessBuffer = nullptr;
    state.noiseTexture = nullptr;
    for (u32 i = 0; i < 2; ++i) {
        state.colorMaps[i] = nullptr;
        state.colorMapNames[i] = nullptr;
    }
    state.noiseTime = 0.f;
    state.nextTraceTime = 0;
    state.fallbackExposureTexture = nullptr;
    state.pipeline = nullptr;
    state.bindingLayout = nullptr;
    state.initialized = false;
}

framegraph::VirtualResourceHandle setupTonemapPass(
    framegraph::FrameGraph& fg,
    fg::RenderDevice* device,
    framegraph::VirtualResourceHandle hdrInput,
    framegraph::VirtualResourceHandle exposureTexture,
    framegraph::VirtualResourceHandle outputTarget,
    u32 width,
    u32 height,
    TonemapPassState& tonemapState,
    const ExposurePassState* exposureState,
    const SPPInfo& postProcess)
{
    using namespace framegraph;

    if (device && device->GetNVRHIDevice())
        InitializeTonemapPass(device->GetNVRHIDevice(), tonemapState);

    const auto postProcessConstants = PreparePostProcess(postProcess, tonemapState, device, width, height);
    bool hasExposure = exposureTexture.is_valid();
    bool hasOutputTarget = outputTarget.is_valid();

    auto& passData = fg.addCallbackPass<TonemapPassData>(
        "Tonemap",

        [hdrInput, exposureTexture, outputTarget, hasExposure, hasOutputTarget, width, height, &tonemapState, exposureState, postProcessConstants](FrameGraph& builder, PassHandle passHandle, TonemapPassData& data) {
            RenderPassBuilder passBuilder(builder, passHandle);

            data.width = width;
            data.height = height;
            data.hasExposure = hasExposure;
            data.passState = &tonemapState;
            data.exposurePassState = exposureState;
            data.postProcess = postProcessConstants;
            for (u32 i = 0; i < 2; ++i)
                data.colorMaps[i] = tonemapState.colorMaps[i] ? tonemapState.colorMaps[i] : tonemapState.noiseTexture;

            data.hdrInput = passBuilder.read(hdrInput, ResourceState::ShaderResource);

            if (hasExposure) {
                data.exposureInput = passBuilder.read(exposureTexture, ResourceState::ShaderResource);
            }

            if (hasOutputTarget) {
                data.ldrOutput = passBuilder.write(outputTarget, ResourceState::RenderTarget);
            } else {
                framegraph::ResourceDesc ldrDesc;
                ldrDesc.type = framegraph::ResourceDesc::Type::Texture2D;
                ldrDesc.width = width;
                ldrDesc.height = height;
                ldrDesc.format = nvrhi::Format::RGBA8_UNORM;
                ldrDesc.isRenderTarget = true;
                ldrDesc.isTransient = false;
                ldrDesc.debugName = "rt_Final";

                data.ldrOutput = passBuilder.createTexture("rt_Final", ldrDesc);
            }
        },

        [](const TonemapPassData& data,
           const FrameGraph& fg,
           fg::RenderContext* ctx) {

            auto* ps = data.passState;
            nvrhi::ICommandList* cmdList = ctx->GetCommandList();
            nvrhi::IDevice* device = cmdList->getDevice();

            if (!ps->pipeline || !ps->bindingLayout)
                return;

            auto* hdrTexture = fg.GetPhysicalTexture(data.hdrInput);
            auto* ldrTexture = fg.GetPhysicalTexture(data.ldrOutput);
            if (!hdrTexture || !ldrTexture)
                return;

            auto& cache = framegraph::GetPassResourceCache();

            auto* vsRefl = GEnv.Render->GetShaderLoader()->GetCachedReflection("tonemap", ".vs");
            auto* psRefl = GEnv.Render->GetShaderLoader()->GetCachedReflection("tonemap", ".ps");
            if (!vsRefl || !psRefl)
                return;

            framegraph::BindingSetBuilder bsb(*vsRefl, *psRefl, device, "Tonemap");
            bsb.Texture("t_hdr", hdrTexture);
            bsb.Texture("t_pp_noise", ps->noiseTexture);
            bsb.Texture("t_pp_grad0", data.colorMaps[0]);
            bsb.Texture("t_pp_grad1", data.colorMaps[1]);
            bsb.ConstantBuffer("PostProcessParams", ps->postProcessBuffer);
            auto bindingSet = cache.GetOrCreateBindingSet(bsb.Build(), ps->bindingLayout, device);
            if (!bindingSet)
                return;

            nvrhi::FramebufferDesc fbDesc;
            fbDesc.addColorAttachment(ldrTexture);
            auto framebuffer = cache.GetOrCreateFramebuffer("TonemapPass", fbDesc, device);

            nvrhi::Viewport viewport;
            viewport.minX = 0;
            viewport.minY = 0;
            viewport.maxX = static_cast<float>(data.width);
            viewport.maxY = static_cast<float>(data.height);
            viewport.minZ = 0.0f;
            viewport.maxZ = 1.0f;

            nvrhi::GraphicsState state;
            state.pipeline = ps->pipeline;
            state.framebuffer = framebuffer;
            state.viewport.addViewportAndScissorRect(viewport);
            state.addBindingSet(bindingSet);

            cmdList->writeBuffer(ps->postProcessBuffer, &data.postProcess, sizeof(data.postProcess));
            cmdList->setGraphicsState(state);
            cmdList->draw(nvrhi::DrawArguments().setVertexCount(3));
        }
    );

    return passData.ldrOutput;
}

} // namespace xray::render::fg::passes
