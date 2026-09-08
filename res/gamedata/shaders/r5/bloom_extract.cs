Texture2D<float4> t_Input : register(t0);
Texture2D<float> t_Exposure : register(t1);
SamplerState smp_rtlinear : register(s0);
RWTexture2D<float4> u_Output : register(u0);
cbuffer BloomParams : register(b0)
{
    float4 screen;
    float4 settings;
    float4 weights[2];
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    u_Output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    float2 uv = (id.xy + 0.5) * screen.zw;
    float2 offset = screen.zw * 0.25;
    float exposure = t_Exposure.Load(int3(0, 0, 0));
    // Reconstruct the legacy high-range target (def_hdr = 9), then bloom_build.
    // Four bilinear taps cover the quarter-resolution pixel's source footprint.
    float3 sum = 0.0;
    [unroll] for (int y = -1; y <= 1; y += 2)
        [unroll] for (int x = -1; x <= 1; x += 2)
            sum += saturate(t_Input.SampleLevel(smp_rtlinear, uv + float2(x, y) * offset, 0).rgb * exposure / 9.0);
    float3 average = sum * 0.5;
    // Preserve the old UNORM range without introducing 8-bit quantization.
    u_Output[id.xy] = saturate(float4(average, dot(average, float3(1.0)) - settings.x));
}
