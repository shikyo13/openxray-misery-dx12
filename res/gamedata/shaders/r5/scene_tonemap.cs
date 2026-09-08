Texture2D<float4> t_Color : register(t0);
Texture2D<float> t_Exposure : register(t1);
RWTexture2D<float4> u_Output : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    u_Output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    float4 source = t_Color.Load(int3(id.xy, 0));
    float3 color = max(source.rgb, 0.0) * t_Exposure.Load(int3(0, 0, 0));
    // X-Ray's authored SDR curve, applied once to the scene before UI/PPE.
    // The legacy material inputs do not use a linear/sRGB texture pipeline.
    const float white = 1.7;
    color = color * (1.0 + color / (white * white)) / (1.0 + color);
    u_Output[id.xy] = float4(saturate(color), source.a);
}
