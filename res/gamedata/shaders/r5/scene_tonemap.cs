Texture2D<float4> t_Color : register(t0);
Texture2D<float> t_Exposure : register(t1);
Texture2D<float4> t_Bloom : register(t2);
SamplerState smp_rtlinear : register(s0);
RWTexture2D<float4> u_Output : register(u0);
cbuffer SceneTonemapParams : register(b0)
{
    float4 controls; // bloom strength, bloom-only preview, unused, unused
};

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
    if (controls.x > 0.0 || controls.y > 0.5) {
        float4 bloom = t_Bloom.SampleLevel(smp_rtlinear, (id.xy + 0.5) / float2(width, height), 0);
        float3 glow = bloom.rgb * bloom.a;
        color = controls.y > 0.5 ? glow : color + glow * controls.x;
    }
    u_Output[id.xy] = float4(saturate(color), source.a);
}
