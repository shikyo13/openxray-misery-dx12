#ifndef XR_OBJECT_MOTION_COMMON
#define XR_OBJECT_MOTION_COMMON
cbuffer ObjectMotionParams : register(b7) {
    float4x4 motion_previous_view_projection;
    float4x4 motion_previous_world;
    float4 motion_controls; // x: continuous object and camera history is valid.
    float4 motion_previous_tree_wind;
    float4 motion_previous_tree_wave;
};
#endif
