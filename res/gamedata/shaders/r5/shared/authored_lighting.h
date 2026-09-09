#ifndef XR_AUTHORED_LIGHTING
#define XR_AUTHORED_LIGHTING

Texture3D<float2> g_AuthoredMaterialLUT : register(t41);

float2 SampleAuthoredMaterial(float diffuse, float specular, float material)
{
    // Material coordinate is (THM category + weight + 0.5) / 4, as in apply_lmaterial.
    return g_AuthoredMaterialLUT.SampleLevel(smp_rtlinear, float3(diffuse, specular, material), 0);
}

float AuthoredSpecularLightWeight(float3 color)
{
    // u_diffuse2s: the original accumulator stores a scalar specular intensity.
    float value = max(0.0, (color.r + color.g + color.b) / 3.0);
    return material_settings.w * (value < 1.0 ? pow(value, 2.0 / 3.0) : value);
}

float3 AuthoredDirectLighting(float3 albedo, float3 N, float3 V, float3 L,
    float3 color, float material, float gloss)
{
    float3 H = normalize(L + V);
    float2 response = SampleAuthoredMaterial(dot(L, N), dot(H, N), material);
    return albedo * color * response.r + (AuthoredSpecularLightWeight(color) * response.g * gloss).xxx;
}

float3 AuthoredAmbientLighting(float3 albedo, float3 N, float3 V, float material,
    float hemi, float gloss, float3 skyIrradiance, out float3 reflection)
{
    float3 R = reflect(-V, N);
    float hspec = 0.5 + 0.5 * dot(R, -V);
    float2 response = SampleAuthoredMaterial(hemi, hspec, material);
    // Installed hmodel uses this remap and the same filtered weather cubemaps.
    R.y = R.y * 2.0 - 1.0;
    reflection = AuthoredEnvironmentDiffuse(R) * response.g * gloss;
    return albedo * (L_ambient.rgb + skyIrradiance * response.r) + reflection;
}

#endif
