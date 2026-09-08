#include "sunshaft_common.h"
Texture2D<float2> t_Shafts : register(t2);
RWTexture2D<float2> u_Output : register(u0);
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height; u_Output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    int2 pixel = int2(id.xy);
    float2 center = t_Shafts.Load(int3(pixel, 0));
    float sum = center.x, weights = 1;
    int2 axis = shaft_sampling.z > .5 ? int2(0, 1) : int2(1, 0);
    [unroll] for (int i = -2; i <= 2; ++i) {
        if (i == 0) continue;
        int2 q = clamp(pixel + i * axis, int2(0, 0), int2(width, height) - 1);
        float2 sampleValue = t_Shafts.Load(int3(q, 0));
        float weight = (abs(i) == 1 ? .75 : .3) * ShaftEdgeWeight(center.y, sampleValue.y);
        sum += sampleValue.x * weight; weights += weight;
    }
    u_Output[id.xy] = float2(sum / weights, center.y);
}
