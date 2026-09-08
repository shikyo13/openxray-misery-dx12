#ifndef XR_SUNSHAFT_COMMON
#define XR_SUNSHAFT_COMMON
Texture2D<float> t_Depth : register(t0);
cbuffer SunShaftParams : register(b0) {
    float4x4 shaft_inverse_vp;
    float4x4 shaft_shadow_matrices[3];
    float4 shaft_splits;
    float4 shaft_camera;
    float4 shaft_camera_direction;
    float4 shaft_sun_color; // RGB radiance, authored shaft intensity
    float4 shaft_sun_direction; // direction toward the sun
    float4 shaft_screen; // full width/height, reciprocal width/height
    float4 shaft_sampling; // sample count, reduction factor, blur axis, unused
};
int2 ShaftFullPixel(int2 pixel) {
    return min(pixel * int(shaft_sampling.y) + int(shaft_sampling.y / 2), int2(shaft_screen.xy) - 1);
}
float3 ShaftWorldPosition(int2 pixel, float depth) {
    float2 uv = (float2(pixel) + .5) * shaft_screen.zw;
    float4 world = mul(shaft_inverse_vp, float4(uv.x * 2 - 1, 1 - uv.y * 2, depth, 1));
    return world.xyz / world.w;
}
float ShaftViewDepth(int2 pixel, float depth) {
    return dot(ShaftWorldPosition(pixel, depth) - shaft_camera.xyz, shaft_camera_direction.xyz);
}
float ShaftEdgeWeight(float a, float b) {
    return exp2(-abs(a - b) / max(.2, min(a, b) * .02));
}
#endif
