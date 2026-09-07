#ifndef SHADOW_SAMPLING_H
#define SHADOW_SAMPLING_H

Texture2DArray<float> g_ShadowMapArray : register(t23);
SamplerComparisonState smp_shadowcmp : register(s4);

float SampleSunCascade(float3 worldPos, uint cascade)
{
    float4 clip = mul(shadow_matrices[cascade], float4(worldPos, 1.0));
    float3 projected = clip.xyz / clip.w;
    float2 uv = float2(projected.x * 0.5 + 0.5, 0.5 - projected.y * 0.5);
    if (any(uv <= 0.0) || any(uv >= 1.0) || projected.z <= 0.0 || projected.z >= 1.0)
        return 1.0;
    float visibility = 0.0;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x) {
            visibility += g_ShadowMapArray.SampleCmpLevelZero(smp_shadowcmp,
                float3(uv + float2(x,y) * cascade_splits.w, cascade), projected.z - 0.00001);
        }
    }
    return visibility / 9.0;
}

float SampleCSM(float3 worldPos, float3 normal)
{
    if (cascade_splits.w <= 0.0) return 1.0;
    float distance = dot(worldPos - eye_position, camera_direction.xyz);
    if (distance <= 0.0 || distance >= cascade_splits.z) return 1.0;
    uint cascade = distance < cascade_splits.x ? 0 : (distance < cascade_splits.y ? 1 : 2);
    float3 position = worldPos + normal * 0.015;
    float visibility = SampleSunCascade(position, cascade);
    float edge = cascade_splits[cascade];
    float blend = saturate((distance - edge * 0.9) / (edge * 0.1));
    if (blend > 0.0) {
        float next = cascade < 2 ? SampleSunCascade(position, cascade + 1) : 1.0;
        visibility = lerp(visibility, next, blend);
    }
    return visibility;
}
#endif
