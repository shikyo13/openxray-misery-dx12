#include "ssao_common.h"
Texture2D<float2> t_AO : register(t2);
RWTexture2D<float2> u_Output : register(u0);
[numthreads(8, 8, 1)]
void main(uint3 thread : SV_DispatchThreadID) {
    int2 size = (int2(ao_screen.xy) + 1) / 2;
    int2 p = int2(thread.xy);
    if (any(p >= size)) return;
    float2 center = t_AO.Load(int3(p, 0));
    if (center.y == 0) { u_Output[p] = center; return; }
    float3 normal = WorldNormal(FullPixel(p));
    float value = center.x, weights = 1;
    int2 axis = ao_controls.z == 0 ? int2(1, 0) : int2(0, 1);
    for (int i = -2; i <= 2; ++i) {
        if (i == 0) continue;
        int2 q = clamp(p + axis * i, int2(0, 0), size - 1);
        float2 sampleAO = t_AO.Load(int3(q, 0));
        float weight = exp2(-float(i * i) * .5) *
            EdgeWeight(center.y, sampleAO.y, normal, WorldNormal(FullPixel(q)));
        value += sampleAO.x * weight;
        weights += weight;
    }
    u_Output[p] = float2(value / weights, center.y);
}
