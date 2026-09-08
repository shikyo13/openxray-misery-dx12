#include "ssao_common.h"
Texture2D<float2> t_AO : register(t2);
Texture2D<float4> t_Color : register(t3);
Texture2D<float4> t_Ambient : register(t4);
RWTexture2D<float4> u_Output : register(u0);
[numthreads(8, 8, 1)]
void main(uint3 thread : SV_DispatchThreadID) {
    int2 p = int2(thread.xy);
    if (any(p >= int2(ao_screen.xy))) return;
    float4 color = t_Color.Load(int3(p, 0));
    float depth = t_Depth.Load(int3(p, 0));
    float visibility = 1;
    if (WorldDepth(depth)) {
        float linearDepth = ViewDepth(p, depth);
        float3 normal = WorldNormal(p);
        float2 location = (float2(p) - 1) * .5;
        int2 origin = int2(floor(location));
        float2 fraction = frac(location);
        float sum = 0, weights = 0;
        int2 size = (int2(ao_screen.xy) + 1) / 2;
        for (int y = 0; y <= 1; ++y) for (int x = 0; x <= 1; ++x) {
            int2 q = clamp(origin + int2(x, y), int2(0, 0), size - 1);
            float2 sampleAO = t_AO.Load(int3(q, 0));
            float2 bilinear = lerp(1 - fraction, fraction, float2(x, y));
            float weight = bilinear.x * bilinear.y *
                EdgeWeight(linearDepth, sampleAO.y, normal, WorldNormal(FullPixel(q)));
            sum += sampleAO.x * weight;
            weights += weight;
        }
        if (weights > .00001) visibility = sum / weights;
    }
    // The fourth opaque MRT holds the actual material-AO-modulated ambient term.
    // Direct sun/local lights, emissive surfaces and subsequent effects are preserved.
    color.rgb = max(0, color.rgb - t_Ambient.Load(int3(p, 0)).rgb * (1 - visibility));
    if (ao_controls.w != 0) color.rgb = visibility.xxx;
    u_Output[p] = color;
}
