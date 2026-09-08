#ifndef XR_SSAO_COMMON
#define XR_SSAO_COMMON
Texture2D<float> t_Depth : register(t0);
Texture2D<float4> t_Normal : register(t1);
cbuffer SSAOParams : register(b0) {
    float4x4 ao_inverse_vp;
    float4x4 ao_view;
    float4 ao_screen;
    float4 ao_settings;
    float4 ao_controls;
};
int2 FullPixel(int2 halfPixel) { return min(halfPixel * 2 + 1, int2(ao_screen.xy) - 1); }
bool WorldDepth(float depth) { return depth > 0.000001 && depth < 0.9; }
float3 WorldPosition(int2 pixel, float depth) {
    float2 uv = (float2(pixel) + .5) * ao_screen.zw;
    float4 position = mul(ao_inverse_vp, float4(uv.x * 2 - 1, 1 - uv.y * 2, depth, 1));
    return position.xyz / position.w;
}
float ViewDepth(int2 pixel, float depth) {
    return mul(ao_view, float4(WorldPosition(pixel, depth), 1)).z;
}
float3 WorldNormal(int2 pixel) {
    float3 n = t_Normal.Load(int3(pixel, 0)).xyz;
    return n * rsqrt(max(dot(n, n), 0.0001));
}
float EdgeWeight(float centerDepth, float sampleDepth, float3 centerNormal, float3 sampleNormal) {
    // Tight depth and normal rejection prevents dark halos across silhouettes.
    float depthWeight = exp2(-abs(centerDepth - sampleDepth) / max(.02, ao_settings.x * .12));
    return depthWeight * pow(saturate(dot(centerNormal, sampleNormal)), 8.0);
}
#endif
