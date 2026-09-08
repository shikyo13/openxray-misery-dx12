cbuffer DetailMotionParams : register(b4)
{
    float4x4 g_previousDetailVP;
    float4 g_previousDetailWind;
    float4 g_detailMotionControls; // previous displacement, coverage debug, atlas index, unused
};
struct DetailMotionOutput
{
    float4 hpos : SV_Position;
    float2 uv : TEXCOORD0;
    float4 previousClip : TEXCOORD1;
    float4 currentClip : TEXCOORD2;
};
