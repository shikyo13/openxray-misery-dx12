#define SM_6_0
#include "common.h"

struct InstanceData
{
	float3 pos;
	uint packed;
};

struct GPUSlotData
{
	float world_min_x;
	float world_min_z;
	float y_base;
	float y_height;
	uint packed_ids;
	uint packed_palette_01;
	uint packed_palette_23;
	float hemi;
};

static const float PACK_MAX_SCALE = 4.0;
static const float TWO_PI = 6.28318530718;
static const float M_PI = 3.1415926;

struct DetailModelGPU
{
	float minScale;
	float maxScale;
	float flags;
	float geomExtentX;
	float geomExtentZ;
	float uv_min_x;
	float uv_min_y;
	float uv_max_x;
	float uv_max_y;
	uint pulledVertexBase;
	uint pulledIndexCount;
	float geomExtentY;
};

struct PulledVertex
{
	float px, py, pz;
	float u, v;
};

cbuffer DetailGlobals : register(b3)
{
	float4 consts;
	float4 wave;
	float4 dir2D;
	float4 dir2D_2;
	float4x4 g_detail_VP;
	float4 detail_params;
	float4 g_wind_direction;
	float grass_wind_displacement;
	float grass_interaction_displacement;
	uint interaction_atlas_index;
	uint perlin4d_texture_index;
	float4 grass_color_tip;
	float4 grass_color_base;
	float4 grass_sss_color;
	float grass_color_variation;
	float grass_blade_height;
	uint build_details_index;
	uint build_details_pbr_index;
	float4 detail_shadow_range; // camera position, authored-detail shadow distance
};

// Perlin4D 3D volume — bound directly at t12 (not bindless, since bindless is Texture2D only)
Texture3D g_Perlin4D : register(t12);

StructuredBuffer<uint> visible_indices : register(t33);
StructuredBuffer<DetailModelGPU> detail_models : register(t35);
StructuredBuffer<PulledVertex> pulled_vertices : register(t36);
StructuredBuffer<InstanceData> all_instances : register(t37);
StructuredBuffer<GPUSlotData> slot_data : register(t38);

#include "detail_wind.h"

#ifdef DETAIL_MOTION
#include "detail_motion.h"
#define DetailVertexOutput DetailMotionOutput
#elif defined(DETAIL_SHADOW)
struct DetailVertexOutput { float4 hpos : SV_Position; float3 uvAlpha : TEXCOORD0; };
#else
#define DetailVertexOutput v2p_billboard
#endif

DetailVertexOutput main(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
	DetailVertexOutput O;

	uint src_idx = visible_indices[instance_id];
	InstanceData raw = all_instances[src_idx];

	uint object_id = raw.packed & 0x3F;
	float rotation = float((raw.packed >> 8) & 0x3FF) / 1023.0 * TWO_PI;
	float scale = float((raw.packed >> 18) & 0x3FF) / 1023.0 * PACK_MAX_SCALE;

	DetailModelGPU mdl = detail_models[object_id];

	if (vertex_id >= mdl.pulledIndexCount)
	{
		O = (DetailVertexOutput)0;
		O.hpos = asfloat(0x7FC00000);
		return O;
	}

	PulledVertex v = pulled_vertices[mdl.pulledVertexBase + vertex_id];

	float3 local_pos = float3(v.px, v.py, v.pz) * scale;
	float height_factor = saturate(v.py / max(mdl.geomExtentY, 0.01));

	float c = cos(rotation);
	float s = sin(rotation);
	float3 rotated;
	rotated.x = local_pos.x * c - local_pos.z * s;
	rotated.y = local_pos.y;
	rotated.z = local_pos.x * s + local_pos.z * c;

	float4 world_pos = float4(rotated + raw.pos, 1.0);

    DetailBend bend = EvaluateDetailBend(raw.pos, height_factor, mdl.flags,
        g_wind_direction, grass_wind_displacement);
    world_pos.xyz = raw.pos + ApplyDetailBend(rotated, bend);

#ifdef DETAIL_MOTION
    DetailBend previousBend = EvaluateDetailBend(raw.pos, height_factor, mdl.flags,
        g_previousDetailWind, g_detailMotionControls.x);
    float4 previousWorld = float4(raw.pos + ApplyDetailBend(rotated, previousBend), 1);
    O.hpos = mul(g_detail_VP, world_pos);
    O.currentClip = O.hpos;
    O.previousClip = mul(g_previousDetailVP, previousWorld);
    O.uv = float2(v.u, v.v);
    return O;
#elif defined(DETAIL_SHADOW)
    float threshold = (asuint(mdl.flags) & 1u) != 0 ? 0.5 : 96.0 / 255.0;
    // Fade alpha coverage over the outer fifth of the selected caster range.
    float fade = saturate((length(raw.pos - detail_shadow_range.xyz) / detail_shadow_range.w - 0.8) * 5.0);
    O.uvAlpha = float3(v.u, v.v, lerp(threshold, 1.001, fade));
    O.hpos = mul(g_detail_VP, world_pos);
    return O;
#else
	float2 uv = float2(v.u, v.v);

	const float slot_size = 2.0;
	int slot_x = int(floor(raw.pos.x / slot_size));
	int slot_z = int(floor(raw.pos.z / slot_size));
	uint x_size = uint(detail_params.x);
	int x_offs = int(detail_params.z);
	int z_offs = int(detail_params.w);
	int sx_local = clamp(slot_x + x_offs, 0, int(x_size) - 1);
	int sz_local = clamp(slot_z + z_offs, 0, int(detail_params.y) - 1);
	uint slot_idx = uint(sz_local) * x_size + uint(sx_local);

	float slot_hemi = slot_data[slot_idx].hemi;
	float hemi = abs(slot_hemi);
	float sun = sign(slot_hemi) * 0.25 + 0.25;

	uint triBase = (vertex_id / 3) * 3;
	PulledVertex v0 = pulled_vertices[mdl.pulledVertexBase + triBase];
	PulledVertex v1 = pulled_vertices[mdl.pulledVertexBase + triBase + 1];
	PulledVertex v2 = pulled_vertices[mdl.pulledVertexBase + triBase + 2];
	float3 e1 = float3(v1.px - v0.px, v1.py - v0.py, v1.pz - v0.pz);
	float3 e2 = float3(v2.px - v0.px, v2.py - v0.py, v2.pz - v0.pz);
	float3 faceN = cross(e1, e2);
	float len = length(faceN);
	faceN = (len > 0.001) ? (faceN / len) : float3(0, 1, 0);
	float3 N;
	N.x = faceN.x * c - faceN.z * s;
	N.y = faceN.y;
    N.z = faceN.x * s + faceN.z * c;
    N = ApplyDetailBend(N, bend);

#if defined(USE_R2_STATIC_SUN) && !defined(USE_LM_HEMI)
	O.tcdh = float4(uv, hemi, sun);
#else
	O.tcdh = uv;
#endif

	O.position = float4(world_pos.xyz, hemi);
	O.N = N;
	O.heightParam = height_factor;
	int2 bc = int2(floor(raw.pos.xz));
	uint bh = asuint(bc.x * 73856093 + bc.y * 19349663);
	bh ^= bh >> 16;
	O.bladeHash = float(bh & 0xFFFFu) / 65535.0;
	O.hpos = mul(g_detail_VP, world_pos);
	return O;
#endif
}
