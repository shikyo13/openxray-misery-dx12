Texture2D<float> t_Depth : register(t0);
RWTexture2D<float> u_Depth : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 tid : SV_DispatchThreadID)
{
    uint width, height;
    u_Depth.GetDimensions(width, height);
    if (tid.x >= width || tid.y >= height) return;
    float depth = t_Depth.Load(int3(tid.xy, 0));
    // The foreground weapon viewport compresses reverse-Z depth into [.9, 1].
    // NGX needs the original device depth to dilate its motion consistently.
    u_Depth[tid.xy] = depth >= .9 ? saturate((depth - .9) * 10) : depth;
}
