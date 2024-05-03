#define BLOCK_DIM 16

cbuffer param_cb : register(b0)
{
    float2 inv_frame_width_height;
    float blur_radius;
    float half_exposure;
    uint reconstruction_samples;
    float max_sample_tap_distance;
};

SamplerState point_sampler : register(s0);
SamplerState linear_sampler : register(s1);

Texture2D frame_tex : register(t0);
Texture2D<float2> motion_vector_tex : register(t1);
Texture2D<float2> motion_vector_neighbor_max_tex : register(t2);
Texture2D<float> random_tex : register(t3);

RWTexture2D<unorm float4> motion_blurred_tex : register(u0);

float Cone(float mag_diff, float mag_v)
{
    return 1 - abs(mag_diff) / mag_v;
}

float Cylinder(float mag_diff, float mag_v)
{
    const float CylinderCorner1 = 0.95f;
    const float CylinderCorner2 = 1.05f;
    return 1 - smoothstep(CylinderCorner1 * mag_v, CylinderCorner2 * mag_v, abs(mag_diff));
}

[numthreads(BLOCK_DIM, BLOCK_DIM, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    const float Epsilon = 0.01f;
    //const float HalfVelocityCutoff = 0.25f;
    const float HalfVelocityCutoff = 0.2f;
    const float VarianceThreshold = 1.5f;
    const float WeightCorrectionFactor = 60;

    float2 tex_coord = (dtid.xy + 0.5f) * inv_frame_width_height;

    float4 color = frame_tex.SampleLevel(linear_sampler, tex_coord, 0);

    float2 neighbor_vel = motion_vector_neighbor_max_tex.SampleLevel(point_sampler, tex_coord, 0) * 2 - 1;
    float len_neighbor_vel = length(neighbor_vel);

    float temp_neighbor_vel = len_neighbor_vel * half_exposure;
    bool flag_neighbor_vel = (temp_neighbor_vel >= Epsilon);
    temp_neighbor_vel = clamp(temp_neighbor_vel, 0.1f, blur_radius);

    [branch]
    if (temp_neighbor_vel < HalfVelocityCutoff)
    {
        motion_blurred_tex[dtid.xy] = color;
        return;
    }

    if (flag_neighbor_vel)
    {
        neighbor_vel *= temp_neighbor_vel / len_neighbor_vel;
    }

    float2 curr_vel = motion_vector_tex.SampleLevel(point_sampler, tex_coord, 0) * 2 - 1;
    float len_curr_vel = length(curr_vel);

    float temp_curr_vel = len_curr_vel * half_exposure;
    bool flag_curr_vel = (temp_curr_vel >= Epsilon);
    temp_curr_vel = clamp(temp_curr_vel, 0.1f, blur_radius);
    if (flag_curr_vel)
    {
        curr_vel *= temp_curr_vel / len_curr_vel;
        len_curr_vel = length(curr_vel);
    }

    float rand = random_tex.SampleLevel(point_sampler, tex_coord * blur_radius, 0) - 0.5f;

    // If current velocity is too small, then we use neighbor velocity
    float2 corrected_vel = normalize((len_curr_vel < VarianceThreshold) ? neighbor_vel : curr_vel);

    // Weight value (suggested by the article authors' implementation)
    float weight = reconstruction_samples / WeightCorrectionFactor / temp_curr_vel;

    float4 sum = float4(color.xyz, 1) * weight;

    uint self_index = (reconstruction_samples - 1) / 2;

    float max_distance = max_sample_tap_distance * inv_frame_width_height.x;
    float2 half_texel = 0.5f * inv_frame_width_height.x;

    for (uint i = 0; i < reconstruction_samples; ++i)
    {
        [branch]
        if (i != self_index)
        {
            // t is distance between current fragment and sample tap.
            // NOTE: we are not sampling adjacent ones; we are extending our taps
            //       a little further
            float lerp_amount = (i + rand + 1) / (reconstruction_samples + 1);
            float t = lerp(-max_distance, max_distance, lerp_amount);

            // The authors' implementation suggests alternating between the corrected velocity and the neighborhood's
            float2 velocity = ((i & 1) == 1) ? corrected_vel : neighbor_vel;

            float2 sample_coord = float2(tex_coord + float2(velocity * t + half_texel));

            float2 sample_vel = motion_vector_tex.SampleLevel(point_sampler, sample_coord, 0) * 2 - 1;
            float len_sample_vel = length(sample_vel);

            float temp_sample_vel = len_sample_vel * half_exposure;
            bool flag_sample_vel = (temp_sample_vel >= Epsilon);
            temp_sample_vel = clamp(temp_sample_vel, 0.1f, blur_radius);
            if (flag_sample_vel)
            {
                sample_vel *= temp_sample_vel / len_sample_vel;
            }

            // alpha = foreground contribution + background contribution + blur of both foreground and background
            weight = 1 + Cone(t, temp_sample_vel)
                + 1 + Cone(t, temp_curr_vel)
                + Cylinder(t, temp_sample_vel) * Cylinder(t, temp_curr_vel) * 2;

            sum += float4(frame_tex.SampleLevel(linear_sampler, sample_coord, 0).xyz, 1) * weight;
        }
    }

    motion_blurred_tex[dtid.xy] = float4(sum.xyz / sum.w, 1);
}
