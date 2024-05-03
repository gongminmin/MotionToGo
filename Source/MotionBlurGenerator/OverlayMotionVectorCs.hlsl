#define BLOCK_DIM 16

cbuffer param_cb : register(b0)
{
    float max_sample_tap_distance;
    uint motion_vector_block_size;
};

Texture2D<float2> motion_vector_tex : register(t0);

RWTexture2D<unorm float4> overlaid_tex : register(u0);

[numthreads(BLOCK_DIM, BLOCK_DIM, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    float2 curr_vel = -(motion_vector_tex.Load(uint3(dtid.xy, 0)) * 2 - 1);
    float2 mv_in_pixel = curr_vel * max_sample_tap_distance * 3;
    float2 abs_mv_in_pixel = abs(mv_in_pixel);

    int2 center = round((dtid.xy + 0.5f) * motion_vector_block_size);
    if (abs_mv_in_pixel.x > abs_mv_in_pixel.y)
    {
        for (int i = 0; i <= abs_mv_in_pixel.x; ++i)
        {
            float delta_x = i * sign(mv_in_pixel.x);
            uint2 coord = center + int2(delta_x, round(delta_x * mv_in_pixel.y / mv_in_pixel.x));
            float f = i / abs_mv_in_pixel.x;
            overlaid_tex[coord] = float4(1 - f, 0, f, 1);
        }
    }
    else
    {
        for (int i = 0; i <= abs_mv_in_pixel.y; ++i)
        {
            float delta_y = i * sign(mv_in_pixel.y);
            uint2 coord = center + int2(round(delta_y * mv_in_pixel.x / mv_in_pixel.y), delta_y);
            float f = i / abs_mv_in_pixel.y;
            overlaid_tex[coord] = float4(1 - f, 0, f, 1);
        }
    }
}
