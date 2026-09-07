#define SM_6_0
#include "common.h"

cbuffer SunShadowDraw : register(b5) {
    float4x4 shadowViewProjection;
    float4 shadowOptions;
};
struct InstanceData { float4x4 world; uint materialID; uint flags; float2 padding; };
StructuredBuffer<InstanceData> g_InstanceData : register(t14);
struct Input { float3 position : POSITION; float2 uv : TEXCOORD0; uint drawIndex : DRAWINDEX; };
struct Output { float4 position : SV_Position; float2 uv : TEXCOORD0; nointerpolation uint materialID : TEXCOORD1; };
Output main(Input input) {
    InstanceData instance = g_InstanceData[input.drawIndex];
    Output output;
    output.position = mul(shadowViewProjection, mul(instance.world, float4(input.position, 1.0)));
    output.uv = input.uv;
    output.materialID = shadowOptions.x > 0.0 ? 0xFFFFFFFF : instance.materialID;
    return output;
}
