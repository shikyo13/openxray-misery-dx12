Texture2D<float2> t_Motion : register(t0);
RWTexture2D<float4> u_Output : register(u0);
cbuffer MotionDebugOptions : register(b0) { float4 debugOptions; };
[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    uint width, height;
    u_Output.GetDimensions(width, height);
    if (id.x >= width || id.y >= height) return;
    uint inputWidth, inputHeight;
    t_Motion.GetDimensions(inputWidth, inputHeight);
    uint2 pixel = min(uint2((float2(id.xy) + .5) * float2(inputWidth, inputHeight) / float2(width, height)),
        uint2(inputWidth - 1, inputHeight - 1));
    float2 pixels = t_Motion.Load(int3(pixel, 0)) * float2(width, height);
    // Mid-gray means stationary. R/G encode previous-minus-current pixel
    // displacement, with +/-8 pixels covering each channel's full range.
    // Higher diagnostic scale exposes subpixel idle animation.
    u_Output[id.xy] = float4(saturate(.5 + pixels * debugOptions.x / 16), .5, 1);
}
