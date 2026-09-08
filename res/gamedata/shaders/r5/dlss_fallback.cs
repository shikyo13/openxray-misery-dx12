Texture2D<float4> t_Color : register(t0);
SamplerState smp_linear : register(s0);
RWTexture2D<float4> u_Output : register(u0);
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    uint width, height;
    u_Output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    u_Output[id.xy] = t_Color.SampleLevel(smp_linear, (float2(id.xy) + .5) / float2(width, height), 0);
}
