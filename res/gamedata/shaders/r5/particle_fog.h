#ifndef PARTICLE_FOG_H
#define PARTICLE_FOG_H

// Matches ParticleBlendMode and the flags in ParticleVertex.
float particle_fog_amount(float3 worldPos, uint flags)
{
    return (flags & 8u) != 0 ? 0.0 : distance_fog_amount(worldPos);
}

float particle_fog_transmittance(float fog)
{
    // Same attenuation as the world fog-color / fog-squared sky composite.
    return (1.0 - fog) * (1.0 - fog * fog);
}

float3 apply_particle_fog(float3 color, float fog, uint flags, float2 svPosition)
{
    uint blendMode = flags & 7u;
    float transmission = particle_fog_transmittance(fog);
    if (blendMode == 2u || blendMode == 5u) // ADD / ALPHA-ADD
        return color * transmission;
    if (blendMode == 3u) // MUL: one is neutral.
        return lerp(float3(1.0, 1.0, 1.0), color, transmission);
    if (blendMode == 4u) // MUL_2X: one half is neutral.
        return lerp(float3(0.5, 0.5, 0.5), color, transmission);
    // SET / BLEND receive the same in-scattered fog as ordinary surfaces.
    return apply_world_fog(color, fog, svPosition);
}

#endif
