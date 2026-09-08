#include "stdafx.h"
#include "BloomPassSetup.h"
#include "Layers/xrRender/FrameGraph/BindingSetBuilder.h"
#include "Layers/xrRender/FrameGraph/PassResourceCache.h"
#include "Layers/xrRender/FrameGraph/RenderPassBuilder.h"
#include "Layers/xrRender/FrameGraph/ShaderLoader.h"
#include "Layers/xrRender/RenderContext/RenderContext.h"
#include "Layers/xrRender/RenderContext/RenderDevice.h"
#include "Layers/xrRender/xrRender_console.h"

namespace xray::render::fg::passes {
using namespace framegraph;
namespace {
const char* bloomShaders[] = {"bloom_extract", "bloom_blur", "bloom_blur"};
const char* bloomPassNames[] = {"Bloom.Extract", "Bloom.BlurX", "Bloom.BlurY"};
struct BloomConstants {
    Fvector4 screen; // source width, height, inverse quarter-resolution width, height
    Fvector4 settings; // threshold, blur axis, fast filter, bilinear kernel radius
    Fvector4 weights[2]; // center, then seven symmetric pairs
};
static_assert(sizeof(BloomConstants) == 64);
}

VirtualResourceHandle setupBloomPass(FrameGraph& graph, fg::RenderDevice* device,
    VirtualResourceHandle color, VirtualResourceHandle exposure, u32 width, u32 height,
    BloomPassState& state)
{
    if (ps_r_bloom_strength <= 0.f && !ps_r_bloom_debug) return {};
    auto& cache = GetPassResourceCache();
    auto* nv = device->GetNVRHIDevice();
    for (u32 i = 0; i < 3; ++i) {
        if (state.pipelines[i]) continue;
        auto cs = GEnv.Render->GetShaderLoader()->LoadComputeShader(bloomShaders[i]);
        R_ASSERT2(cs.handle && cs.reflection, "Bloom shader compilation failed");
        state.layouts[i] = cache.GetOrCreateBindingLayoutFromReflection(bloomPassNames[i], *cs.reflection, nv);
        nvrhi::ComputePipelineDesc pipeline;
        pipeline.CS = cs.handle;
        pipeline.bindingLayouts = {state.layouts[i]};
        state.pipelines[i] = cache.GetOrCreateComputePipeline(bloomPassNames[i], pipeline, nv);
        R_ASSERT2(state.pipelines[i], "Bloom pipeline creation failed");
    }
    state.constants = cache.GetOrCreateVolatileCB("Bloom", "BloomParams", sizeof(BloomConstants), device);
    const u32 bloomWidth = (width + 3) / 4;
    const u32 bloomHeight = (height + 3) / 4;
    BloomConstants constants{};
    constants.screen.set(float(width), float(height), 1.f / bloomWidth, 1.f / bloomHeight);
    constants.settings.set(ps_r2_ls_bloom_threshold, 0.f,
        ps_r2_ls_flags.test(R2FLAG_FASTBLOOM) ? 1.f : 0.f, ps_r2_ls_bloom_kernel_b);

    // X-Ray's broad + narrow Gaussian lobes, each with the authored magnitude.
    float weights[8]{};
    for (float sigma : {ps_r2_ls_bloom_kernel_g, ps_r2_ls_bloom_kernel_g / 3.f}) {
        float lobe[8], magnitude = 0.f;
        for (u32 i = 0; i < 8; ++i) {
            lobe[i] = std::exp(-float(i * i) / (2.f * sigma * sigma));
            magnitude += lobe[i] * (i ? 2.f : 1.f);
        }
        for (u32 i = 0; i < 8; ++i)
            weights[i] += ps_r2_ls_bloom_kernel_scale * lobe[i] / magnitude;
    }
    constants.weights[0].set(weights[0], weights[1], weights[2], weights[3]);
    constants.weights[1].set(weights[4], weights[5], weights[6], weights[7]);
    VirtualResourceHandle previous = color;
    for (u32 i = 0; i < 3; ++i) {
        ResourceDesc desc;
        desc.debugName = bloomPassNames[i];
        desc.width = bloomWidth;
        desc.height = bloomHeight;
        desc.format = nvrhi::Format::RGBA16_FLOAT;
        desc.isUAV = true;
        desc.isTransient = true;
        auto target = graph.CreateTexture(bloomPassNames[i], desc);
        struct PassData {
            VirtualResourceHandle input, exposure, output;
            BloomPassState* state;
            fg::RenderDevice* device;
            BloomConstants constants;
            u32 index, width, height;
        };
        auto& data = graph.addCallbackPass<PassData>(bloomPassNames[i],
            [&](FrameGraph& builder, PassHandle pass, PassData& data) {
                RenderPassBuilder pb(builder, pass);
                data.input = pb.read(previous);
                if (i == 0) data.exposure = pb.read(exposure);
                data.output = pb.write(target, ResourceState::UnorderedAccess);
                data.state = &state;
                data.device = device;
                data.constants = constants;
                data.constants.settings.y = i == 2 ? 1.f : 0.f;
                data.index = i;
                data.width = bloomWidth;
                data.height = bloomHeight;
            },
            [](const PassData& data, const FrameGraph& graph, fg::RenderContext* ctx) {
                auto* nv = data.device->GetNVRHIDevice();
                auto* reflection = GEnv.Render->GetShaderLoader()->GetCachedReflection(bloomShaders[data.index], ".cs");
                BindingSetBuilder bindings(*reflection, nv, bloomPassNames[data.index]);
                bindings.ConstantBuffer("BloomParams", data.state->constants)
                    .Texture("t_Input", graph.GetPhysicalTexture(data.input))
                    .TextureUAV("u_Output", graph.GetPhysicalTexture(data.output));
                if (data.exposure.is_valid())
                    bindings.Texture("t_Exposure", graph.GetPhysicalTexture(data.exposure));
                auto set = GetPassResourceCache().GetOrCreateBindingSet(bindings.Build(), data.state->layouts[data.index], nv);
                R_ASSERT2(set, "Bloom binding set creation failed");
                ctx->GetCommandList()->writeBuffer(data.state->constants, &data.constants, sizeof(data.constants));
                ctx->SetComputePipeline(data.state->pipelines[data.index]);
                ctx->SetComputeBindingSet(0, set);
                ctx->Dispatch((data.width + 7) / 8, (data.height + 7) / 8, 1);
            });
        previous = data.output;
    }
    return previous;
}
}
