Texture2D<float4> t_Input : register(t0);
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
    // Retain the authored 256-texel vertical radius, with circular spread on
    // widescreen displays and a stable apparent size when resolution changes.
    float2 stepUV = float2(screen.y / screen.x, 1.0) / 256.0;
    float4 sum;
    if (settings.z > 0.5) {
        float2 offset = stepUV * settings.w;
        // Changing filter quality must not change the energy of a flat region.
        float magnitude = weights[0].x + 2.0 * (dot(weights[0].yzw, float3(1.0)) + dot(weights[1], float4(1.0)));
        sum = (t_Input.SampleLevel(smp_rtlinear, uv + offset, 0)
            + t_Input.SampleLevel(smp_rtlinear, uv - offset, 0)
            + t_Input.SampleLevel(smp_rtlinear, uv + float2(offset.x, -offset.y), 0)
            + t_Input.SampleLevel(smp_rtlinear, uv + float2(-offset.x, offset.y), 0)) * (magnitude * 0.25);
    } else {
        float2 axis = settings.y > 0.5 ? float2(0, stepUV.y) : float2(stepUV.x, 0);
        sum = t_Input.SampleLevel(smp_rtlinear, uv, 0) * weights[0].x;
        [unroll] for (int i = 1; i < 8; ++i) {
            float2 offset = axis * (2.0 * i - 0.5);
            float weight = weights[i / 4][i % 4];
            sum += weight * (t_Input.SampleLevel(smp_rtlinear, uv - offset, 0)
                + t_Input.SampleLevel(smp_rtlinear, uv + offset, 0));
        }
    }
    u_Output[id.xy] = saturate(sum);
}
