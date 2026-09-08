#include "common.h"

cbuffer MotionVectorParams : register(b5) {
    float4x4 g_InvViewProj;
    float4x4 g_PrevViewProj;
    float2 g_ScreenSize;
    float2 g_InvScreenSize;
    float4 g_CameraPosition;
};

Texture2D<float> t_Depth : register(t0);
RWTexture2D<float2> u_MotionVectors : register(u0);

float3 ReconstructWorldPos(uint2 pixel, float depth)
{
    float2 uv = (float2(pixel) + 0.5) * g_InvScreenSize;
    float4 clip = float4(uv * 2.0 - 1.0, depth, 1.0);
    clip.y = -clip.y;
    float4 world = mul(g_InvViewProj, clip);
    return world.xyz / world.w;
}

[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    uint2 pixel = dtid.xy;
    if (pixel.x >= (uint)g_ScreenSize.x || pixel.y >= (uint)g_ScreenSize.y)
        return;

    float depth = t_Depth.Load(int3(pixel, 0));

    // HUD depth is compressed into [0.9, 1]. Its actual motion is supplied
    // by the skinned object pass, not reconstructed as a world-space surface.
    if (depth >= .9) {
        u_MotionVectors[pixel] = float2(0, 0);
        return;
    }

    float2 currUV = (float2(pixel) + 0.5) * g_InvScreenSize;
    float4 world = mul(g_InvViewProj, float4(currUV.x * 2 - 1, 1 - currUV.y * 2, depth, 1));
    // Sky follows camera rotation but not translation, including an infinite
    // reverse-Z far plane (world.w == 0).
    float4 previousPosition = depth <= 0 ? float4(world.xyz - g_CameraPosition.xyz * world.w, 0) : float4(world.xyz / world.w, 1);
    float4 prevClip = mul(g_PrevViewProj, previousPosition);
    if (prevClip.w <= 1e-6) {
        u_MotionVectors[pixel] = 0;
        return;
    }
    float2 prevNDC = prevClip.xy / prevClip.w;
    prevNDC.y = -prevNDC.y;
    float2 prevUV = prevNDC * 0.5 + 0.5;

    float2 motion = prevUV - currUV;
    u_MotionVectors[pixel] = all(isfinite(motion)) ? motion : float2(0, 0);
}
