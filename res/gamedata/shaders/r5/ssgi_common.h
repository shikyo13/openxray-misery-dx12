#ifndef XR_SSGI_COMMON
#define XR_SSGI_COMMON
Texture2D<float> t_Depth : register(t0);
Texture2D<float4> t_Normal : register(t1);
Texture2D<float4> t_Color : register(t2);
Texture2D<float4> t_Ambient : register(t3);
Texture2D<float4> t_BaseColor : register(t4);
Texture2D<float4> t_Sky : register(t5);
Texture2D<float4> t_Indirect : register(t6);
cbuffer SSGIParams : register(b0) {
    float4x4 gi_inverse_vp, gi_view, gi_vp;
    float4 gi_screen, gi_camera, gi_fog, gi_fog_color, gi_settings, gi_controls;
};
int2 GISize() { return (int2(gi_screen.xy) + int(gi_controls.z) - 1) / int(gi_controls.z); }
int2 GIFullPixel(int2 p) { return min(p * int(gi_controls.z) + int(gi_controls.z) / 2, int2(gi_screen.xy) - 1); }
bool GIWorldDepth(float d) { return d > .000001 && d < .9; }
float3 GIPosition(int2 p, float d) {
    float2 uv = (float2(p) + .5) * gi_screen.zw;
    float4 world = mul(gi_inverse_vp, float4(uv.x * 2 - 1, 1 - uv.y * 2, d, 1));
    return world.xyz / world.w;
}
float3 GINormal(int2 p) {
    float3 n = t_Normal.Load(int3(p, 0)).xyz;
    return n * rsqrt(max(dot(n,n), .0001));
}
float GIFog(float3 position) {
    return saturate((length(position - gi_camera.xyz) - gi_fog.x) * gi_fog.y);
}
float GITransmission(float fog) { return (1 - fog) * (1 - fog * fog); }
float GIEdgeWeight(float z, float sampleZ, float3 n, float3 sampleN) {
    return exp2(-abs(z - sampleZ) / max(.025, gi_settings.x * .06)) *
        pow(saturate(dot(n, sampleN)), 16.0);
}
bool GIProject(float3 world, out float2 uv) {
    float4 clip = mul(gi_vp, float4(world, 1));
    uv = clip.xy / max(clip.w, .00001) * float2(.5, -.5) + .5;
    return clip.w > .01 && all(uv > 0) && all(uv < 1);
}
float GIEdgeFade(float2 uv) {
    float2 edge = min(uv, 1 - uv);
    return saturate(min(edge.x, edge.y) * 12.5);
}
#endif
