#ifndef XR_WATER_TEMPORAL
#define XR_WATER_TEMPORAL
cbuffer WaterTemporalParams : register(b8) {
    float4x4 water_previous_view_projection;
    float4x4 water_current_inverse_view_projection;
    float4 water_temporal_options; // Previous time, valid history, soft water, coverage diagnostic.
};
#endif
