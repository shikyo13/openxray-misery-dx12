#define SM_6_0
#include "common.h"
#include "object_motion_common.h"
#include "shared/tree_wind.h"
struct InstanceData { float4x4 world; uint materialID; uint flags; float2 hemi; };
StructuredBuffer<InstanceData> g_InstanceData : register(t14);
struct Input {
    float3 position : POSITION;
    float2 uv : TEXCOORD0;
    float2 rigidity : TEXCOORD1;
    uint drawIndex : DRAWINDEX;
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
    InstanceData instance = g_InstanceData[input.drawIndex];
    float4 world = mul(instance.world, float4(input.position, 1));
    float4 previousWorld = TreeWindPosition(world, instance.world[1][3], input.rigidity.x,
        motion_previous_tree_wave, motion_previous_tree_wind);
    world = TreeWindPosition(world, instance.world[1][3], input.rigidity.x, tree_wave, tree_wind);
    Output o = (Output)0;
    o.position = mul(m_VP, world);
    o.currentClip = o.position;
    o.previousClip = motion_controls.x > .5 ? mul(motion_previous_view_projection, previousWorld) : o.currentClip;
    o.uv = input.uv;
    o.materialID = instance.materialID;
    return o;
}
