#include "ssgi_common.h"
RWTexture2D<float4> u_Output : register(u0);
[numthreads(8,8,1)]
void main(uint3 thread : SV_DispatchThreadID) {
    int2 p = int2(thread.xy);
    if (any(p >= int2(gi_screen.xy))) return;
    float4 color = t_Color.Load(int3(p,0));
    float depth = t_Depth.Load(int3(p,0));
    float3 bounce = 0;
    if (GIWorldDepth(depth)) {
        float3 position = GIPosition(p, depth), normal = GINormal(p);
        float linearDepth = mul(gi_view, float4(position,1)).z;
        float2 location = (float2(p) - floor(gi_controls.z * .5)) / gi_controls.z;
        int2 origin = int2(floor(location));
        float2 fraction = frac(location);
        float3 value = 0;
        float weights = 0;
        int2 size = GISize();
        for (int y = 0; y <= 1; ++y) for (int x = 0; x <= 1; ++x) {
            int2 q = clamp(origin + int2(x,y), int2(0,0), size - 1);
            float4 sampleGI = t_Indirect.Load(int3(q,0));
            float2 bilinear = lerp(1 - fraction, fraction, float2(x,y));
            float weight = bilinear.x * bilinear.y *
                GIEdgeWeight(linearDepth, sampleGI.a, normal, GINormal(GIFullPixel(q)));
            value += sampleGI.rgb * weight;
            weights += weight;
        }
        float4 material = t_BaseColor.Load(int3(p,0));
        if (weights > .00001) bounce = value / weights * saturate(material.rgb) * (1 - saturate(material.a));
        bounce *= gi_settings.y * GITransmission(GIFog(position)) * GIEdgeFade((float2(p) + .5) * gi_screen.zw);
        if (gi_controls.y >= 2) bounce = weights > .00001 ? value / weights : 0;
        if (gi_controls.y == 4) bounce = material.rgb * (1 - saturate(material.a));
        if (gi_controls.y == 5) bounce *= 16;
    }
    color.rgb = gi_controls.y != 0 ? bounce : color.rgb + bounce;
    u_Output[p] = color;
}
