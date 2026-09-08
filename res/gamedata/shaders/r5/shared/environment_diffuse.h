#ifndef XR_ENVIRONMENT_DIFFUSE
#define XR_ENVIRONMENT_DIFFUSE

TextureCube<float4> g_EnvironmentDiffuse0 : register(t39);
TextureCube<float4> g_EnvironmentDiffuse1 : register(t40);

float3 AuthoredEnvironmentDiffuse(float3 worldNormal)
{
    // MISERY hmodel samples its filtered weather maps in world-normal space.
    // Keep its weather interpolation and contrast in the authored color space.
    float3 a = g_EnvironmentDiffuse0.SampleLevel(smp_rtlinear, worldNormal, 0).rgb;
    float3 b = g_EnvironmentDiffuse1.SampleLevel(smp_rtlinear, worldNormal, 0).rgb;
    float3 color = environment_color.rgb * lerp(a, b, environment_color.w);
    return color * color;
}

#endif
