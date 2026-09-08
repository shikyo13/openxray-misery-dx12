cbuffer FsrFrameGenerationParams : register(b5)
{
    float4 g_FsrMotion; // xy: previous-current jitter in UV units, z: reset.
};
Texture2D<float> t_Depth : register(t0);
Texture2D<float2> t_Motion : register(t1);
RWTexture2D<float> u_Depth : register(u0);
RWTexture2D<float2> u_Motion : register(u1);

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint width, height;
    u_Depth.GetDimensions(width, height);
    if (tid.x >= width || tid.y >= height) return;
    float depth = t_Depth.Load(int3(tid.xy, 0));
    u_Depth[tid.xy] = depth >= .9 ? saturate((depth - .9) * 10) : depth;
    // The interpolator receives the resolved, unjittered display image. Remove
    // camera sampling jitter from its motion without changing NGX's own input.
    float2 motion = t_Motion.Load(int3(tid.xy, 0)) - g_FsrMotion.xy;
    u_Motion[tid.xy] = g_FsrMotion.z > 0 || !all(isfinite(motion)) ? float2(0,0) : motion;
}
