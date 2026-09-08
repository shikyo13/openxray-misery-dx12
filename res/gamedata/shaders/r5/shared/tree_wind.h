#ifndef XR_TREE_WIND
#define XR_TREE_WIND
static const uint GPU_INSTANCE_TREE_WIND = 0x10;

// Authored X-Ray tree wave; rigidity is misc.z / FTreeVisual_quant.
// All visible, shadow and temporal paths use this same position calculation.
float4 TreeWindPosition(float4 position, float baseHeight, float rigidity, float4 wave, float4 wind)
{
    float cyclic = calc_cyclic(wave.w + dot(position.xyz, wave.xyz));
    position.xz += wind.xz * ((position.y - baseHeight) * cyclic * rigidity);
    return position;
}
#endif
