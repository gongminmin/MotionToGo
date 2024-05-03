#pragma once

#include <array>
#include <span>

#include <DirectXMath.h>
#include <directx/d3d12.h>
#include <winrt/base.h>

#include "Gpu/GpuBufferHelper.hpp"
#include "Gpu/GpuSystem.hpp"
#include "Gpu/GpuTexture2D.hpp"
#include "Noncopyable.hpp"

namespace MotionToGo
{
    class MotionBlurGenerator final
    {
        DISALLOW_COPY_AND_ASSIGN(MotionBlurGenerator)

    public:
        explicit MotionBlurGenerator(GpuSystem& gpu_system);
        ~MotionBlurGenerator() noexcept;

        MotionBlurGenerator(MotionBlurGenerator&& other) noexcept;
        MotionBlurGenerator& operator=(MotionBlurGenerator&& other) noexcept;

        static bool ConfirmDeviceFunc(ID3D12Device* device);

        uint64_t AddFrame(GpuTexture2D& motion_blurred_tex, const GpuTexture2D& frame_tex, float time_span, bool overlay_mv);

    private:
        uint64_t ConvertToNv12(GpuTexture2D& frame_rgb_tex, GpuTexture2D& output_frame_nv12_tex);
        uint64_t ConvertToRgb(GpuTexture2D& frame_nv12_tex, GpuTexture2D& output_frame_rgb_tex);
        uint64_t EstimateMotionVectors(GpuTexture2D& ref_frame_nv12_tex, GpuTexture2D& input_frame_nv12_tex,
            GpuTexture2D& output_motion_vector_tex, ID3D12VideoMotionVectorHeap* video_mv_heap, uint64_t wait_fence_value);
        uint64_t PropagateMotionBlur(float time_span, GpuTexture2D& raw_motion_vector_tex, GpuTexture2D& output_motion_vector_tex,
            GpuTexture2D& output_motion_vector_neighbor_max_tex, uint64_t wait_fence_value);
        uint64_t GatherMotionBlur(GpuTexture2D& frame_tex, GpuTexture2D& motion_vector_tex, GpuTexture2D& motion_vector_neighbor_max_tex,
            GpuTexture2D& output_motion_blurred_tex);
        uint64_t OverlayMotionVector(GpuTexture2D& motion_vector_tex, GpuTexture2D& output_overlaid_tex);

        template <typename T>
        struct ComputeShaderHelper
        {
            ConstantBuffer<T> cb;
            winrt::com_ptr<ID3D12RootSignature> root_sig;
            winrt::com_ptr<ID3D12PipelineState> pso;
            GpuDescriptorBlock desc_block;

            uint32_t num_srvs;
            uint32_t num_uavs;
        };

        struct SrvHelper
        {
            GpuTexture2D* tex;
            uint32_t sub_resource = ~0u;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        };

        struct UavHelper
        {
            GpuTexture2D* tex;
            uint32_t sub_resource = 0;
            DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        };

        template <typename CbType, size_t ShaderSize>
        void CreateComputeShader(ID3D12Device* device, ComputeShaderHelper<CbType>& cs, const unsigned char (&shader)[ShaderSize],
            std::span<const D3D12_STATIC_SAMPLER_DESC> samplers = {});

        template <typename CbType>
        uint64_t RunComputeShader(const SrvHelper srv_texs[], const UavHelper uav_texs[], const ComputeShaderHelper<CbType>& cs,
            uint32_t dispatch_x, uint32_t dispatch_y, uint64_t wait_fence_value = GpuSystem::MaxFenceValue);

    private:
        static constexpr float Exposure = 1;
        static constexpr uint32_t BlurRadius = 1;
        static constexpr uint32_t ReconstructionSamples = 15;

        GpuSystem& gpu_system_;

        GpuTexture2D random_tex_;

        winrt::com_ptr<ID3D12VideoMotionEstimator> video_motion_estimator_;
        uint32_t max_mv_width_;
        uint32_t max_mv_height_;
        uint32_t min_mv_width_;
        uint32_t min_mv_height_;
        uint32_t mv_block_size_;

        struct ColorSpaceConstantBuffer
        {
            DirectX::XMUINT2 frame_width_height;
        };
        ComputeShaderHelper<ColorSpaceConstantBuffer> rgb_to_nv12_cs_;
        ComputeShaderHelper<ColorSpaceConstantBuffer> nv12_to_rgb_cs_;

        struct NeighborMaxConstantBuffer
        {
            DirectX::XMFLOAT2 inv_half_frame_width_height;
            DirectX::XMUINT2 motion_vector_width_height;
            DirectX::XMUINT2 raw_motion_vector_width_height;
            float blur_radius;
            float half_exposure_x_framerate;
            float size_scale;
        };
        ComputeShaderHelper<NeighborMaxConstantBuffer> neighbor_max_cs_;

        struct GatherConstantBuffer
        {
            DirectX::XMFLOAT2 inv_frame_width_height;
            float blur_radius;
            float half_exposure;
            uint32_t reconstruction_samples;
            float max_sample_tap_distance;
        };
        ComputeShaderHelper<GatherConstantBuffer> gather_cs_;

        struct OverlayConstantBuffer
        {
            float max_sample_tap_distance;
            uint32_t motion_vector_block_size;
        };
        ComputeShaderHelper<OverlayConstantBuffer> overlay_cs_;

        struct Frame
        {
            winrt::com_ptr<ID3D12VideoMotionVectorHeap> video_motion_vector_heap;

            GpuTexture2D frame_rgb_tex;
            GpuTexture2D frame_nv12_tex;
            GpuTexture2D scaled_frame_nv12_tex;
            GpuTexture2D raw_motion_vector_tex;
            GpuTexture2D motion_vector_tex;
            GpuTexture2D motion_vector_neighbor_max_tex;
        };
        std::array<Frame, GpuSystem::FrameCount> frames_;
    };
} // namespace MotionToGo
