#define BLOCK_DIM 16

cbuffer param_cb : register(b0)
{
    uint2 frame_width_height;
};

SamplerState point_sampler : register(s0);
SamplerState linear_sampler : register(s1);

Texture2D frame_tex : register(t0);

RWTexture2D<unorm float> luma_tex : register(u0);
RWTexture2D<unorm float2> chroma_tex : register(u1);

float3 RgbToYCbCr(float3 rgb)
{
    float kr = 0.2627f;
    float kb = 0.0593f;
    float kg = 1 - kr - kb;
    float kcr = (1 - kr) / 0.5f;
    float kcb = (1 - kb) / 0.5f;

    float y = dot(float3(kr, kg, kb), rgb);
    float cb = (rgb.b - y) / kcb;
    float cr = (rgb.r - y) / kcr;
    return int3(float3(y, cb, cr) * int3(219, 224, 224) + int3(16, 128, 128)) / 255.0f;
}

[numthreads(BLOCK_DIM, BLOCK_DIM, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    float2 chroma = 0;
    for (uint dy = 0; dy < 2; ++dy)
    {
        for (uint dx = 0; dx < 2; ++dx)
        {
            uint2 coord = dtid.xy * 2 + uint2(dx, dy);
            float2 tex_coord = (coord + 0.5f) / frame_width_height;
            float3 rgb = frame_tex.SampleLevel(linear_sampler, tex_coord, 0).rgb;
            float3 yuv = RgbToYCbCr(rgb);

            luma_tex[coord] = yuv.x;
            chroma += yuv.yz;
        }
    }

    chroma_tex[dtid.xy] = chroma / 4;
}
