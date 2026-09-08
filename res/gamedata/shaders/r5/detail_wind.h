// Shared by visible authored details, their shadows and temporal motion.
// All paths must bend the same vertex to the same depth.
struct DetailBend
{
    float3 axis;
    float sine;
    float cosine;
};

DetailBend EvaluateDetailBend(float3 root, float heightFactor, float modelFlags,
    float4 wind, float displacement)
{
    float speed = max(wind.y, 0.1);
    float angle = wind.x * (M_PI / 180.0);
    float2 globalDirection = float2(sin(angle), cos(angle));
    float2 directionUV = root.zx * 0.005 + wind.z * 0.005;
    float directionNoise = g_Perlin4D.SampleLevel(smp_linear, float3(directionUV, 0), 0).r;
    float2 strengthUV = root.xz * 0.025 + wind.z * 0.025;
    float strengthNoise = g_Perlin4D.SampleLevel(smp_linear, float3(strengthUV, 0), 0).r;
    float strength = lerp(0.25, 1.0, strengthNoise);
    strength *= strength;
    strength *= speed;
    float turbulence = (directionNoise * 2.0 - 1.0) * 0.3;
    float2 perpendicular = float2(-globalDirection.y, globalDirection.x);
    float2 direction = normalize(globalDirection + perpendicular * turbulence);
    float bendAngle = atan(max(strength * displacement, 0.0)) * heightFactor;
    if ((asuint(modelFlags) & 1u) != 0) bendAngle = 0.0;
    DetailBend bend;
    bend.axis = float3(direction.y, 0.0, -direction.x);
    sincos(bendAngle, bend.sine, bend.cosine);
    return bend;
}

float3 ApplyDetailBend(float3 vertex, DetailBend bend)
{
    return vertex * bend.cosine + cross(bend.axis, vertex) * bend.sine
        + bend.axis * dot(bend.axis, vertex) * (1.0 - bend.cosine);
}
