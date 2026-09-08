#include "ssgi_common.h"
RWTexture2D<float4> u_Output : register(u0);
[numthreads(8,8,1)]
void main(uint3 thread : SV_DispatchThreadID) {
    int2 p = int2(thread.xy), size = GISize();
    if (any(p >= size)) return;
    float4 center = t_Indirect.Load(int3(p, 0));
    if (center.a == 0) { u_Output[p] = center; return; }
    float3 normal = GINormal(GIFullPixel(p));
    float3 value = center.rgb;
    float weights = 1;
    int2 axis = gi_controls.x == 0 ? int2(1,0) : int2(0,1);
    for (int i = -3; i <= 3; ++i) {
        if (i == 0) continue;
        int2 q = clamp(p + axis * i, int2(0,0), size - 1);
        float4 sampleGI = t_Indirect.Load(int3(q,0));
        float weight = exp2(-float(i * i) * .25) *
            GIEdgeWeight(center.a, sampleGI.a, normal, GINormal(GIFullPixel(q)));
        value += sampleGI.rgb * weight;
        weights += weight;
    }
    u_Output[p] = float4(value / weights, center.a);
}
