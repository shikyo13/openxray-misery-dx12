// Screen-space light gathering inspired by Ascii1457's SSS indirect lighting.
// See SSGI_CREDITS.txt. Native DX12 implementation; not the SSS 23 shader.
#include "ssgi_common.h"
RWTexture2D<float4> u_Output : register(u0);

float3 SourceRadiance(int2 p, float3 position) {
    float fog = GIFog(position), transmission = GITransmission(fog);
    if (transmission < .1) return 0;
    // Invert the existing two-stage world fog, then remove authored ambient.
    // This gathers direct/emissive light without bouncing sky fog or ambient again.
    float3 fogColor = gi_fog_color.rgb * fog * (1 - fog * fog) +
        t_Sky.Load(int3(p, 0)).rgb * fog * fog;
    float3 radiance = max(0, t_Color.Load(int3(p, 0)).rgb -
        t_Ambient.Load(int3(p, 0)).rgb - fogColor) / transmission;
    return radiance * min(1.0, 8.0 / max(.001, max(radiance.r, max(radiance.g, radiance.b))));
}

bool SegmentVisible(float3 a, float3 b) {
    // Short depth checks reject occluders between the gathered surface and receiver.
    // Geometry outside the camera view cannot be recovered by this approximation.
    for (uint step = 1; step <= 2; ++step) {
        float3 point = lerp(a, b, float(step) / 3.0);
        float2 uv;
        if (!GIProject(point, uv)) return false;
        int2 p = min(int2(uv * gi_screen.xy), int2(gi_screen.xy) - 1);
        float d = t_Depth.Load(int3(p, 0));
        if (d >= .9) return false;
        if (!GIWorldDepth(d)) continue;
        float sceneZ = mul(gi_view, float4(GIPosition(p, d), 1)).z;
        float rayZ = mul(gi_view, float4(point, 1)).z;
        if (sceneZ < rayZ - max(.08, rayZ * .002)) return false;
    }
    return true;
}

[numthreads(8,8,1)]
void main(uint3 thread : SV_DispatchThreadID) {
    int2 p = int2(thread.xy), size = GISize();
    if (any(p >= size)) return;
    int2 pixel = GIFullPixel(p);
    float depth = t_Depth.Load(int3(pixel, 0));
    if (!GIWorldDepth(depth)) { u_Output[p] = 0; return; }
    float3 position = GIPosition(pixel, depth), normal = GINormal(pixel);
    float linearDepth = mul(gi_view, float4(position, 1)).z;
    // Diagnostic 2 isolates the available direct/emissive source light.
    if (gi_controls.y == 2) { u_Output[p] = float4(SourceRadiance(pixel, position), min(linearDepth,65000.0)); return; }
    if (dot(normal, normal) < .5) { u_Output[p] = float4(0,0,0,min(linearDepth,65000.0)); return; }
    float3 tangent = normalize(cross(abs(normal.z) < .99 ? float3(0,0,1) : float3(0,1,0), normal));
    float3 bitangent = cross(normal, tangent);
    float radius = gi_settings.x;
    float jitter = frac(52.9829189 * frac(dot(float2(p), float2(.06711056, .00583715))));
    float3 indirect = 0;
    float3 accepted = 0; // Debug RGB: in range, facing correctly, visible.
    uint count = uint(gi_settings.z);
    for (uint i = 0; i < count; ++i) {
        float z = (float(i) + .5) / count;
        float angle = 6.2831853 * (float(i) * .618033989 + jitter);
        float r = sqrt(1 - z * z);
        float3 direction = tangent * (cos(angle) * r) + bitangent * (sin(angle) * r) + normal * z;
        float2 uv;
        if (!GIProject(position + direction * radius, uv)) continue;
        int2 samplePixel = min(int2(uv * gi_screen.xy), int2(gi_screen.xy) - 1);
        float sampleDepth = t_Depth.Load(int3(samplePixel, 0));
        if (!GIWorldDepth(sampleDepth)) continue;
        float3 samplePosition = GIPosition(samplePixel, sampleDepth);
        float3 delta = samplePosition - position;
        float distanceSquared = dot(delta, delta);
        if (distanceSquared < .0025 || distanceSquared > radius * radius) continue;
        accepted.x += 1;
        float3 lightDirection = delta * rsqrt(distanceSquared);
        float3 sampleNormal = GINormal(samplePixel);
        float weight = saturate(dot(normal, lightDirection) - gi_settings.w) *
            saturate(dot(sampleNormal, -lightDirection)) * saturate(1 - distanceSquared / (radius * radius));
        if (weight < .001) continue;
        accepted.y += 1;
        if (!SegmentVisible(position + normal * .04, samplePosition + sampleNormal * .04)) continue;
        accepted.z += 1;
        indirect += SourceRadiance(samplePixel, samplePosition) * weight * GIEdgeFade(uv);
    }
    u_Output[p] = float4(gi_controls.y == 3 ? accepted / count : indirect * (2.0 / count), min(linearDepth, 65000.0));
}
