#define BLOCK_DIM 16
#define KERNEL_RADIUS 1

cbuffer param_cb : register(b0)
{
    float2 inv_half_frame_width_height;
    uint2 motion_vector_width_height;
    uint2 raw_motion_vector_width_height;
    float blur_radius;
    float half_exposure_x_framerate;
    float size_scale;
};

Texture2D<int2> raw_motion_vector_tex : register(t0);

RWTexture2D<unorm float2> motion_vector_tex : register(u0);
RWTexture2D<unorm float2> motion_vector_neighbor_max_tex : register(u1);

groupshared float2 sh_mv_tile[BLOCK_DIM + KERNEL_RADIUS * 2][BLOCK_DIM + KERNEL_RADIUS * 2];

[numthreads(BLOCK_DIM, BLOCK_DIM, 1)]
void main(uint3 gid : SV_GroupID, uint3 gtid : SV_GroupThreadID, uint3 dtid : SV_DispatchThreadID, uint gi : SV_GroupIndex)
{
    const float Epsilon = 0.01f;

    int2 start_coord = gid.xy * BLOCK_DIM - KERNEL_RADIUS;
    for (uint i = gi; i < (BLOCK_DIM + KERNEL_RADIUS * 2) * (BLOCK_DIM + KERNEL_RADIUS * 2); i += BLOCK_DIM * BLOCK_DIM)
    {
        uint offset_y = i / (BLOCK_DIM + KERNEL_RADIUS * 2);
        uint offset_x = i - offset_y * (BLOCK_DIM + KERNEL_RADIUS * 2);
        uint2 coord = start_coord + uint2(offset_x, offset_y);
        uint2 input_coord = uint2((coord + 0.5f) / motion_vector_width_height * raw_motion_vector_width_height);
        float2 mv;
        if (any(input_coord == 0) || (input_coord.x >= raw_motion_vector_width_height.x - 1) || (input_coord.y >= raw_motion_vector_width_height.y - 1))
        {
            mv = 0;
        }
        else
        {
            mv = -raw_motion_vector_tex.Load(int3(input_coord, 0)) * size_scale / 4.0f * inv_half_frame_width_height;

            mv *= half_exposure_x_framerate;
            float len_mv = length(mv);

            float weight = max(0.5f, min(len_mv, blur_radius));
            weight /= max(len_mv, Epsilon);
            mv *= weight;
        }

        sh_mv_tile[offset_y][offset_x] = mv;
    }
    GroupMemoryBarrierWithGroupSync();

    motion_vector_tex[dtid.xy] = sh_mv_tile[gtid.y + KERNEL_RADIUS][gtid.x + KERNEL_RADIUS] * 0.5f + 0.5f;

    float2 max_mv = 0;
    float max_magnitude_squared = 0;
    for (int s = -KERNEL_RADIUS; s <= KERNEL_RADIUS; ++s)
    {
        for (int t = -KERNEL_RADIUS; t <= KERNEL_RADIUS; ++t)
        {
            float2 mv = sh_mv_tile[gtid.y + t + KERNEL_RADIUS][gtid.x + s + KERNEL_RADIUS];

            float magnitude_squared = dot(mv, mv);
            if (max_magnitude_squared < magnitude_squared)
            {
                float displacement = abs(s) + abs(t);
                float2 orientation = sign(float2(s, t) * mv);
                float distance = orientation.x + orientation.y;
                if (abs(distance) == displacement)
                {
                    max_mv = mv;
                    max_magnitude_squared = magnitude_squared;
                }
            }
        }
    }

    motion_vector_neighbor_max_tex[dtid.xy] = max_mv * 0.5f + 0.5f;
}
