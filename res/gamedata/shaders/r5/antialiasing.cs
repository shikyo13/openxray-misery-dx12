// FXAA quality path, before UI and authored camera PPE. No temporal history.
#include "fxaa.h"
#include "common_samplers.h"
Texture2D t_Color : register(t0);
RWTexture2D<float4> u_Output : register(u0);
cbuffer AAParams : register(b0) {
    float4 aa_screen;
    float4 aa_quality;
};
[numthreads(8, 8, 1)]
void main(uint3 thread : SV_DispatchThreadID) {
    if (any(thread.xy >= uint2(aa_screen.xy))) return;
    FxaaTex source;
    source.tex = t_Color;
    source.smpl = smp_rtlinear;
    float2 uv = (float2(thread.xy) + .5) * aa_screen.zw;
    float4 result = FxaaPixelShader(uv, 0, source, source, source, aa_screen.zw,
        0, 0, 0, aa_quality.x, aa_quality.y, aa_quality.z, 0, 0, 0, 0);
    result.a = t_Color.Load(int3(thread.xy, 0)).a;
    u_Output[thread.xy] = result;
}
