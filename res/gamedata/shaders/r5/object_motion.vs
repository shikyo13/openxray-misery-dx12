#define SM_6_0
#include "common.h"
#include "object_motion_common.h"
cbuffer SkinnedMaterialCB : register(b4) {
    uint g_SkinnedMaterialID;
    uint g_SkeletonBoneOffset;
    uint g_SplatOffset;
    uint g_SplatCount;
};
struct Input {
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
};
struct Output {
    float4 position : SV_Position;
    float3 worldPos : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float3 normal : TEXCOORD2;
    float3 tangent : TEXCOORD3;
    float3 bitangent : TEXCOORD4;
    nointerpolation uint materialID : TEXCOORD5;
    float4 previousClip : TEXCOORD7;
    float4 currentClip : TEXCOORD8;
};
Output main(Input input) {
    Output o = (Output)0;
    float4 world = mul(m_W, float4(input.position, 1));
    o.position = mul(m_VP, world);
    o.currentClip = o.position;
    o.previousClip = mul(motion_previous_view_projection,
        mul(motion_previous_world, float4(input.position, 1)));
    if (motion_controls.x < .5) o.previousClip = o.currentClip;
    o.uv = input.uv;
    o.materialID = g_SkinnedMaterialID;
    return o;
}
