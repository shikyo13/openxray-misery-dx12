struct SlotAABB {
    float3 aabb_min; float padding0;
    float3 aabb_max; float padding1;
    uint instance_base; uint instance_count; int slot_x; int slot_z;
    float4 padding2;
};
struct InstanceData { float3 pos; uint packed; };
StructuredBuffer<SlotAABB> t_Slots : register(t0);
StructuredBuffer<InstanceData> t_Instances : register(t1);
RWStructuredBuffer<uint> u_Visible : register(u0);
RWByteAddressBuffer u_DrawArgs : register(u1);
cbuffer DetailShadowCull : register(b0) {
    float4 planes[6];
    uint slotCount; uint capacity; float maximumRadius; uint instanceCapacity;
    float4 rootRadii[16];
    float4 cameraRange;
    uint4 slotWindow; // startX, startZ, width, database stride
    float4 lightRange; // position and radius; zero radius for sunlight
};

[numthreads(256, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= slotCount) return;
    uint slotIndex = (slotWindow.y + id.x / slotWindow.z) * slotWindow.w + slotWindow.x + id.x % slotWindow.z;
    SlotAABB slot = t_Slots[slotIndex];
    if (slot.instance_count == 0 || slot.instance_base >= instanceCapacity) return;
    float3 closest = clamp(cameraRange.xyz, slot.aabb_min, slot.aabb_max);
    if (dot(closest - cameraRange.xyz, closest - cameraRange.xyz) > cameraRange.w * cameraRange.w) return;
    // A cubemap face extends beyond the light's spherical attenuation range.
    // Keep any mesh that can cross that range, including wind-bent foliage.
    if (lightRange.w > 0) {
        float3 delta = clamp(lightRange.xyz, slot.aabb_min, slot.aabb_max) - lightRange.xyz;
        float reach = lightRange.w + maximumRadius;
        if (dot(delta, delta) > reach * reach) return;
    }
    // Use the light's complete frustum, including its near plane. Camera Hi-Z
    // and camera visibility cannot reject something that casts into the view.
    [unroll] for (uint p = 0; p < 6; ++p) {
        float3 corner = float3(
            planes[p].x < 0 ? slot.aabb_max.x : slot.aabb_min.x,
            planes[p].y < 0 ? slot.aabb_max.y : slot.aabb_min.y,
            planes[p].z < 0 ? slot.aabb_max.z : slot.aabb_min.z);
        if (dot(planes[p].xyz, corner) + planes[p].w > maximumRadius) return;
    }
    uint count = min(slot.instance_count, instanceCapacity - slot.instance_base);
    for (uint i = 0; i < count; ++i) {
        uint source = slot.instance_base + i;
        InstanceData instance = t_Instances[source];
        float3 fromCamera = instance.pos - cameraRange.xyz;
        if (dot(fromCamera, fromCamera) > cameraRange.w * cameraRange.w) continue;
        uint model = instance.packed & 63u;
        float scale = float((instance.packed >> 18) & 1023u) * (4.0 / 1023.0);
        float radius = rootRadii[model / 4][model % 4] * scale;
        if (lightRange.w > 0) {
            float3 delta = instance.pos - lightRange.xyz;
            float reach = lightRange.w + radius;
            if (dot(delta, delta) > reach * reach) continue;
        }
        bool visible = true;
        [unroll] for (uint p = 0; p < 6; ++p)
            visible = visible && dot(planes[p].xyz, instance.pos) + planes[p].w <= radius;
        if (!visible) continue;
        uint index;
        u_DrawArgs.InterlockedAdd(4, 1, index);
        // Capacity covers the complete generated instance set, not just the view.
        if (index < capacity) u_Visible[index] = source;
    }
}
