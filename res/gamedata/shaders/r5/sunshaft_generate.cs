#include "sunshaft_common.h"
Texture2DArray<float> t_Shadow : register(t1);
SamplerComparisonState smp_shadowcmp : register(s0);
RWTexture2D<float2> u_Output : register(u0);

float ShaftCascade(float3 world, uint cascade) {
    float4 clip = mul(shaft_shadow_matrices[cascade], float4(world, 1));
    float3 projected = clip.xyz / clip.w;
    float2 uv = float2(projected.x * .5 + .5, .5 - projected.y * .5);
    if (any(uv <= 0) || any(uv >= 1) || projected.z <= 0 || projected.z >= 1) return 1;
    return t_Shadow.SampleCmpLevelZero(smp_shadowcmp, float3(uv, cascade), projected.z - .00001);
}
float ShaftVisibility(float3 world, float depth) {
    uint cascade = depth < shaft_splits.x ? 0 : (depth < shaft_splits.y ? 1 : 2);
    float visibility = ShaftCascade(world, cascade);
    if (cascade < 2) {
        float edge = shaft_splits[cascade];
        float blend = saturate((depth - edge * .9) / (edge * .1));
        if (blend > 0) visibility = lerp(visibility, ShaftCascade(world, cascade + 1), blend);
    }
    return visibility;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height; u_Output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    int2 pixel = ShaftFullPixel(int2(id.xy));
    float depth = t_Depth.Load(int3(pixel, 0));
    // The first-person projection reserves [0.9, 1.0] in the depth buffer.
    if (depth >= .9) { u_Output[id.xy] = float2(0, 0); return; }
    float3 ray = ShaftWorldPosition(pixel, depth) - shaft_camera.xyz;
    float viewDepth = max(dot(ray, shaft_camera_direction.xyz), .0001);
    float endDepth = min(viewDepth, shaft_splits.z);
    uint samples = uint(shaft_sampling.x);
    float3 step = ray * (endDepth / viewDepth / samples);
    float stepDepth = endDepth / samples;
    // Fixed screen-space jitter avoids animation noise while breaking slice bands.
    float jitter = frac(52.9829189 * frac(dot(float2(id.xy), float2(.06711056, .00583715))));
    float visibility = 0;
    [loop] for (uint i = 0; i < samples; ++i) {
        float distance = (i + jitter) * stepDepth;
        if (distance > .3)
            visibility += ShaftVisibility(shaft_camera.xyz + (i + jitter) * step, distance);
    }
    // Retain the legacy forward/backward ratio, normalized over solid angle.
    // The spherical mean of (0.6 + 0.4*cos(theta)) is 0.6. Adding it without
    // this normalization creates an excessive uniform veil over the sky.
    float phase = (.6 + .4 * dot(normalize(ray), shaft_sun_direction.xyz)) / (.6 * 4 * 3.14159265);
    u_Output[id.xy] = float2(visibility / samples * shaft_sun_color.w * phase, viewDepth);
}
