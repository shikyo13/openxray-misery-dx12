#include "sunshaft_common.h"
Texture2D<float2> t_Shafts : register(t2);
Texture2D<float4> t_Color : register(t3);
RWTexture2D<float4> u_Output : register(u0);
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    int2 pixel = int2(id.xy);
    if (any(pixel >= int2(shaft_screen.xy))) return;
    float4 color = t_Color.Load(int3(pixel, 0));
    float depth = t_Depth.Load(int3(pixel, 0));
    if (depth < .9) {
        float viewDepth = ShaftViewDepth(pixel, depth);
        float2 location = (float2(pixel) - floor(shaft_sampling.y / 2)) / shaft_sampling.y;
        int2 origin = int2(floor(location));
        float2 fraction = frac(location);
        uint width, height; t_Shafts.GetDimensions(width, height);
        float sum = 0, weights = 0;
        [unroll] for (int y = 0; y <= 1; ++y) [unroll] for (int x = 0; x <= 1; ++x) {
            int2 q = clamp(origin + int2(x, y), int2(0, 0), int2(width, height) - 1);
            float2 sampleValue = t_Shafts.Load(int3(q, 0));
            float2 bilinear = lerp(1 - fraction, fraction, float2(x, y));
            float weight = bilinear.x * bilinear.y * ShaftEdgeWeight(viewDepth, sampleValue.y);
            sum += sampleValue.x * weight; weights += weight;
        }
        if (weights > .00001) color.rgb += shaft_sun_color.rgb * (sum / weights);
    }
    u_Output[pixel] = color;
}
