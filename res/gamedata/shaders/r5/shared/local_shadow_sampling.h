#ifndef LOCAL_SHADOW_SAMPLING_H
#define LOCAL_SHADOW_SAMPLING_H

#include "shared/shadow_sampling.h"
Texture2DArray<float> g_LocalShadowMap : register(t27);
StructuredBuffer<float4x4> g_LocalShadowMatrices : register(t28);

uint LocalShadowCubeFace(float3 direction)
{
    float3 axis = abs(direction);
    if (axis.x >= axis.y && axis.x >= axis.z) return direction.x >= 0 ? 0 : 1;
    if (axis.y >= axis.z) return direction.y >= 0 ? 2 : 3;
    return direction.z >= 0 ? 4 : 5;
}

float SampleLocalShadow(GPULightData light, float3 worldPos, float3 normal)
{
    uint basePlusOne = (uint)light.spotParamsAndType.w;
    if (basePlusOne == 0) return 1.0;
    uint width, height, layers;
    g_LocalShadowMap.GetDimensions(width, height, layers);
    float3 fromLight = worldPos - light.positionAndInvRangeSq.xyz;
    // Scale receiver bias to a fraction of the projected texel, with a small
    // world-space floor. Root-bent foliage uses its matching deformed normal.
    float bias = min(0.06, 0.002 + length(fromLight) * 0.35 / width);
    float3 receiver = worldPos + normal * bias;
    uint face = light.spotParamsAndType.y < 0.5 ? LocalShadowCubeFace(fromLight) : 0;
    uint slice = basePlusOne - 1 + face;
    float4 clip = mul(g_LocalShadowMatrices[slice], float4(receiver, 1));
    if (clip.w <= 0) return 1.0;
    float3 projected = clip.xyz / clip.w;
    float2 uv = float2(projected.x * 0.5 + 0.5, 0.5 - projected.y * 0.5);
    if (any(uv < 0) || any(uv > 1) || projected.z <= 0 || projected.z >= 1) return 1.0;
    float visibility = 0;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            visibility += g_LocalShadowMap.SampleCmpLevelZero(smp_shadowcmp,
                float3(uv + float2(x, y) / float2(width, height), slice), projected.z - 0.00001);
        }
    }
    return visibility / 9.0;
}
#endif
