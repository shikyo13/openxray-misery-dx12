#ifndef XR_WATER_SURFACE
#define XR_WATER_SURFACE
#include "shared/waterconfig.h"

Texture2D<float> g_WaterDepth : register(t29);
Texture2D<float4> g_WaterScene : register(t30);
TextureCube g_WaterSky0 : register(t31);
TextureCube g_WaterSky1 : register(t32);
cbuffer WaterParams : register(b7) {
    float4x4 water_inverse_vp;
    float4 water_options; // Soft water, cos/sin sky rotation, weather interpolation weight.
};

float2 WaterNormalUV(float2 base, float2 position, float amplitude) {
    // Legacy watermove_tc reads timers.z = time / 10. Native globals use
    // that component for sin(time), so take the continuous phase from x.
    float angle = timers.x * .1 + dot(position, float2(.2111, .2333) * amplitude);
    return base + amplitude * float2(sin(angle), cos(angle));
}

float WaterBackgroundDepth(float2 uv) {
    float d = g_WaterDepth.SampleLevel(smp_nofilter, uv, 0);
    // The renderer reserves [0.9, 1] for the HUD. Never refract the weapon.
    if (d >= .9) return -1;
    if (d <= .000001) return 10000;
    float4 p = mul(water_inverse_vp, float4(uv.x * 2 - 1, 1 - uv.y * 2, d, 1));
    return mul(m_V, float4(p.xyz / p.w, 1)).z;
}

PS_OUTPUT ShadeWater(PS_INPUT input, MaterialData mat) {
    PS_OUTPUT o = (PS_OUTPUT)0;
    float2 uv = input.position.xy * screen_res.zw;
    float3 N = normalize(input.normal);
    if (mat.normalIndex != INVALID_TEXTURE_INDEX) {
        Texture2D normalMap = GetBindlessTexture(mat.normalIndex);
        float2 uv0 = WaterNormalUV(input.texcoord * W_DISTORT_BASE_TILE_0, input.worldPos.xz, W_DISTORT_AMP_0);
        float2 uv1 = WaterNormalUV(input.texcoord * W_DISTORT_BASE_TILE_1, input.worldPos.xz, W_DISTORT_AMP_1);
        // Authored water normals are RGB, unlike X-Ray's packed surface bumps.
        float3 n = normalMap.Sample(smp_base, uv0).rgb + normalMap.Sample(smp_base, uv1).rgb - 1;
        N = normalize(mul(n, float3x3(normalize(input.tangent), normalize(input.bitangent), N)));
    }
    float3 V = normalize(eye_position - input.worldPos);
    float3 R = reflect(-V, N);
    float fresnelPower = pow(saturate(dot(R, -V)), 9);
    float3 envDirection = float3(R.x * water_options.y - R.z * water_options.z,
        R.y * 2 - 1, R.x * water_options.z + R.z * water_options.y);
    float3 env = lerp(g_WaterSky0.Sample(smp_rtlinear, envDirection).rgb,
        g_WaterSky1.Sample(smp_rtlinear, envDirection).rgb, water_options.w);
    env = env * env * 2;
    float4 base = SampleDiffuse(mat, input.texcoord);
    float3 reflection = env * (.15 + .25 * fresnelPower);
    float sun = saturate(dot(N, -L_sun_dir_w)) * saturate(input.sunOcclusion) * SampleCSM(input.worldPos, N);
    float3 illumination = input.waterLight.rgb + L_ambient.rgb +
        L_hemi_color.rgb * L_hemi_color.w * saturate(N.y) * saturate(input.waterLight.a) + L_sun_color * sun;
    float3 color = lerp(reflection, base.rgb, base.a) * illumination * 2;
    float viewDepth = mul(m_V, float4(input.worldPos, 1)).z;
    color += EvaluateClusteredLights(input.worldPos, N, V, base.rgb, 0, .2,
        input.position.xy, viewDepth, (uint)pbr_diffuse_mode);
    float alpha = .75 + .25 * fresnelPower;
    float edge = 1;
    float3 refracted = g_WaterScene.SampleLevel(smp_rtlinear, uv, 0).rgb;
    if (water_options.x > .5) {
        float backgroundDepth = WaterBackgroundDepth(uv);
        float depth = max(backgroundDepth - viewDepth, 0);
        edge = saturate(depth * 20);
        alpha = max(min(alpha, saturate(depth)), 1 - exp(-4 * depth));
        float3 Nv = mul((float3x3)m_V, N);
        float2 refractUV = uv + Nv.xy * float2(1, -1) * .012 * saturate(depth);
        // Reject foreground objects, HUD and samples outside the image.
        if (all(refractUV > 0) && all(refractUV < 1) && WaterBackgroundDepth(refractUV) > viewDepth + .02)
            refracted = g_WaterScene.SampleLevel(smp_rtlinear, refractUV, 0).rgb;
        if (mat.detailIndex != INVALID_TEXTURE_INDEX) {
            Texture2D foamMap = GetBindlessTexture(mat.detailIndex);
            float4 foam = foamMap.Sample(smp_base, input.texcoord);
            float verticalDepth = depth * saturate(dot(N, V));
            float foamBand = smoothstep(.025, .05, verticalDepth) * (1 - smoothstep(.075, .1, verticalDepth));
            float amount = foam.a * foamBand;
            float intensity = dot(L_hemi_color.rgb, float3(1.0 / 3.0));
            color = lerp(color, foam.rgb * intensity, amount);
            alpha = lerp(alpha, foam.a, amount);
        }
    }
    float fog = distance_fog_amount(input.worldPos);
    color = apply_world_fog(color, fog, input.position.xy);
    // The background has already been fogged; fog only the water contribution.
    o.color = float4(lerp(refracted, color, alpha), edge * (1 - fog * fog));
    o.normal = float4(N, .2);
    o.baseColor = float4(base.rgb, 0);
    return o;
}
#endif
