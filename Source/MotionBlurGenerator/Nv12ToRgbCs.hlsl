#define BLOCK_DIM 16

cbuffer param_cb : register(b0)
{
    uint2 frame_width_height;
};

Texture2D<float> luma_tex : register(t0);
Texture2D<float2> chroma_tex : register(t1);

RWTexture2D<float4> rgb_tex : register(u0);

float3 YCbCrToRgb(float3 ycbcr)
{
    float kr = 0.2627f;
    float kb = 0.0593f;
    float kg = 1 - kr - kb;
    float kcr = (1 - kr) / 0.5f;
    float kcb = (1 - kb) / 0.5f;

    return float3(dot(float3(1, 0, kcr), ycbcr), dot(float3(1, -kb * kcb / kg, -kr * kcr / kg), ycbcr), dot(float3(1, kcb, 0), ycbcr));
}

[numthreads(BLOCK_DIM, BLOCK_DIM, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    float3 ycbcr;
    ycbcr.x = (luma_tex.Load(int3(clamp(dtid.xy, 0, frame_width_height), 0)) * 255 - 16) / 219.0f;
    ycbcr.yz = (chroma_tex.Load(int3(clamp(dtid.xy / 2, 0, frame_width_height / 2), 0)) * 255 - 128) / 224.0f;
    rgb_tex[dtid.xy] = float4(YCbCrToRgb(ycbcr), 1);
}
