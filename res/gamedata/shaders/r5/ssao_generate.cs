#include "ssao_common.h"
RWTexture2D<float2> u_Output : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 thread : SV_DispatchThreadID) {
    int2 size = (int2(ao_screen.xy) + 1) / 2;
    int2 p = int2(thread.xy);
    if (any(p >= size)) return;
    int2 pixel = FullPixel(p);
    float depth = t_Depth.Load(int3(pixel, 0));
    if (!WorldDepth(depth)) { u_Output[p] = float2(1, 0); return; }
    float3 position = WorldPosition(pixel, depth);
    float linearDepth = mul(ao_view, float4(position, 1)).z;
    float3 normal = WorldNormal(pixel);
    float radius = ao_settings.x;
    float radiusPixels = min(128.0, radius * ao_settings.w / max(linearDepth, .01));
    float jitter = frac(52.9829189 * frac(dot(float2(p), float2(.06711056, .00583715))));
    float occlusion = 0;
    uint directions = uint(ao_controls.x), steps = uint(ao_controls.y);
    for (uint direction = 0; direction < directions; ++direction) {
        float angle = 6.2831853 * (direction + jitter) / directions;
        float2 offset = float2(cos(angle), sin(angle));
        float horizon = 0;
        for (uint step = 1; step <= steps; ++step) {
            float distancePixels = max(1.0, radiusPixels * (step - .5 + jitter * .5) / steps);
            int2 samplePixel = int2(round(float2(pixel) + offset * distancePixels));
            if (any(samplePixel < 0) || any(samplePixel >= int2(ao_screen.xy))) continue;
            float sampleDepth = t_Depth.Load(int3(samplePixel, 0));
            if (!WorldDepth(sampleDepth)) continue;
            float3 delta = WorldPosition(samplePixel, sampleDepth) - position;
            float distanceSquared = dot(delta, delta);
            float cosine = dot(normal, delta) * rsqrt(max(distanceSquared, .00001));
            float contribution = saturate((cosine - ao_settings.z) / (1 - ao_settings.z));
            contribution *= saturate(1 - distanceSquared / (radius * radius));
            horizon = max(horizon, contribution);
        }
        occlusion += horizon;
    }
    float visibility = pow(saturate(1 - 2 * occlusion / directions), ao_settings.y);
    u_Output[p] = float2(visibility, min(linearDepth, 65000.0));
}
