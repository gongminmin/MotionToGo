#include "MotionBlurGenerator.hpp"

#include <format>
#include <random>

#include "ErrorHandling.hpp"
#include "Gpu/GpuCommandList.hpp"
#include "Gpu/GpuResourceViews.hpp"

#include "CompiledShaders/MotionBlurGatherCs.h"
#include "CompiledShaders/MotionBlurNeighborMaxCs.h"
#include "CompiledShaders/Nv12ToRgbCs.h"
#include "CompiledShaders/OverlayMotionVectorCs.h"
#include "CompiledShaders/RgbToNv12Cs.h"

using namespace DirectX;

namespace
{
    constexpr uint32_t DivUp(uint32_t a, uint32_t b) noexcept
    {
        return (a + b - 1) / b;
    }

    D3D12_ROOT_PARAMETER CreateRootParameterAsDescriptorTable(const D3D12_DESCRIPTOR_RANGE* descriptor_ranges,
        uint32_t num_descriptor_ranges, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL) noexcept
    {
        D3D12_ROOT_PARAMETER ret;
        ret.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        ret.DescriptorTable.NumDescriptorRanges = num_descriptor_ranges;
        ret.DescriptorTable.pDescriptorRanges = descriptor_ranges;
        ret.ShaderVisibility = visibility;
        return ret;
    }

    D3D12_ROOT_PARAMETER CreateRootParameterAsConstantBufferView(
        uint32_t shader_register, uint32_t register_space = 0, D3D12_SHADER_VISIBILITY visibility = D3D12_SHADER_VISIBILITY_ALL) noexcept
    {
        D3D12_ROOT_PARAMETER ret;
        ret.ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        ret.Descriptor.ShaderRegister = shader_register;
        ret.Descriptor.RegisterSpace = register_space;
        ret.ShaderVisibility = visibility;
        return ret;
    }
} // namespace

namespace MotionToGo
{
    MotionBlurGenerator::MotionBlurGenerator(GpuSystem& gpu_system) : gpu_system_(gpu_system)
    {
        winrt::com_ptr<ID3D12Device> d3d12_device;
        d3d12_device.copy_from(gpu_system_.NativeDevice());

        {
            winrt::com_ptr<ID3D12VideoDevice1> video_device = d3d12_device.try_as<ID3D12VideoDevice1>();
            if (!video_device)
            {
                ::OutputDebugStringW(L"ERROR: COULDN'T get video device.\n");
                return;
            }

            D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR motion_estimator_support{0, DXGI_FORMAT_NV12};
            TIFHR(video_device->CheckFeatureSupport(
                D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR, &motion_estimator_support, sizeof(motion_estimator_support)));

            constexpr uint32_t MaxMvWidth = 1920;
            constexpr uint32_t MaxMvHeight = 1080;
            constexpr uint32_t MinMvWidth = 512;
            constexpr uint32_t MinMvHeight = 384;
            max_mv_width_ = std::min(MaxMvWidth, motion_estimator_support.SizeRange.MaxWidth);
            max_mv_height_ = std::min(MaxMvHeight, motion_estimator_support.SizeRange.MaxWidth);
            min_mv_width_ = std::max(MinMvWidth, motion_estimator_support.SizeRange.MinWidth);
            min_mv_height_ = std::max(MinMvHeight, motion_estimator_support.SizeRange.MinWidth);

            D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE block_size;
            if (motion_estimator_support.BlockSizeFlags & D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_FLAG_16X16)
            {
                block_size = D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_16X16;
                mv_block_size_ = 16;
            }
            else
            {
                block_size = D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_8X8;
                mv_block_size_ = 8;
            }

            const D3D12_VIDEO_MOTION_ESTIMATOR_DESC motion_estimator_desc = {0, DXGI_FORMAT_NV12, block_size,
                D3D12_VIDEO_MOTION_ESTIMATOR_VECTOR_PRECISION_QUARTER_PEL, {max_mv_width_, max_mv_height_, min_mv_width_, min_mv_height_}};
            if (FAILED(video_device->CreateVideoMotionEstimator(
                    &motion_estimator_desc, nullptr, winrt::guid_of<ID3D12VideoMotionEstimator>(), video_motion_estimator_.put_void())))
            {
                ::OutputDebugStringW(L"ERROR: COULDN'T create motion estimator.\n");
                return;
            }

            const D3D12_VIDEO_MOTION_VECTOR_HEAP_DESC motion_vector_heap_desc = {motion_estimator_desc.NodeMask,
                motion_estimator_desc.InputFormat, motion_estimator_desc.BlockSize, motion_estimator_desc.Precision,
                motion_estimator_desc.SizeRange};
            for (auto& frame : frames_)
            {
                if (FAILED(video_device->CreateVideoMotionVectorHeap(&motion_vector_heap_desc, nullptr,
                        winrt::guid_of<ID3D12VideoMotionVectorHeap>(), frame.video_motion_vector_heap.put_void())))
                {
                    ::OutputDebugStringW(L"ERROR: COULDN'T create motion vector heap.\n");
                    return;
                }
            }
        }

        D3D12_STATIC_SAMPLER_DESC sampler_desc[2];
        for (uint32_t i = 0; i < std::size(sampler_desc); ++i)
        {
            sampler_desc[i] = {};
            sampler_desc[i].Filter = i == 0 ? D3D12_FILTER_MIN_MAG_MIP_POINT : D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            sampler_desc[i].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler_desc[i].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler_desc[i].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler_desc[i].MaxAnisotropy = 16;
            sampler_desc[i].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
            sampler_desc[i].MinLOD = 0.0f;
            sampler_desc[i].MaxLOD = D3D12_FLOAT32_MAX;
            sampler_desc[i].ShaderRegister = i;
        }

        {
            const uint32_t tile_width = 128;
            const uint32_t tile_height = 128;
            std::ranlux24_base gen;
            std::uniform_int_distribution<> random_dis(0, 255);
            auto rand_data = std::make_unique<uint8_t[]>(tile_width * tile_height);
            for (uint32_t j = 0; j < tile_height; ++j)
            {
                for (uint32_t i = 0; i < tile_width; ++i)
                {
                    rand_data[j * tile_width + i] = static_cast<uint8_t>(random_dis(gen));
                }
            }

            random_tex_ = GpuTexture2D(gpu_system_, tile_width, tile_height, 1, DXGI_FORMAT_R8_UNORM,
                D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, L"random_tex");
            auto cmd_list = gpu_system_.CreateCommandList(GpuSystem::CmdQueueType::Compute);
            random_tex_.Upload(gpu_system_, cmd_list, 0, rand_data.get());
            gpu_system_.Execute(std::move(cmd_list));
        }

        {
            rgb_to_nv12_cs_.cb = ConstantBuffer<ColorSpaceConstantBuffer>(gpu_system_, 1, L"rgb_to_nv12_cb");
            rgb_to_nv12_cs_.num_srvs = 1;
            rgb_to_nv12_cs_.num_uavs = 2;

            this->CreateComputeShader(
                d3d12_device.get(), rgb_to_nv12_cs_, RgbToNv12Cs_shader, std::span(sampler_desc, std::size(sampler_desc)));
        }
        {
            nv12_to_rgb_cs_.cb = ConstantBuffer<ColorSpaceConstantBuffer>(gpu_system_, 1, L"nv12_to_rgb_cb");
            nv12_to_rgb_cs_.num_srvs = 2;
            nv12_to_rgb_cs_.num_uavs = 1;

            this->CreateComputeShader(d3d12_device.get(), nv12_to_rgb_cs_, Nv12ToRgbCs_shader);
        }
        {
            neighbor_max_cs_.cb = ConstantBuffer<NeighborMaxConstantBuffer>(gpu_system_, 1, L"neighbor_max_cb");
            neighbor_max_cs_.num_srvs = 1;
            neighbor_max_cs_.num_uavs = 2;

            this->CreateComputeShader(d3d12_device.get(), neighbor_max_cs_, MotionBlurNeighborMaxCs_shader);
        }
        {
            gather_cs_.cb = ConstantBuffer<GatherConstantBuffer>(gpu_system_, 1, L"gather_cb");
            gather_cs_.num_srvs = 4;
            gather_cs_.num_uavs = 1;

            this->CreateComputeShader(
                d3d12_device.get(), gather_cs_, MotionBlurGatherCs_shader, std::span(sampler_desc, std::size(sampler_desc)));
        }
        {
            overlay_cs_.cb = ConstantBuffer<OverlayConstantBuffer>(gpu_system_, 1, L"overlay_cb");
            overlay_cs_.num_srvs = 1;
            overlay_cs_.num_uavs = 1;

            this->CreateComputeShader(d3d12_device.get(), overlay_cs_, OverlayMotionVectorCs_shader);
        }
    }

    MotionBlurGenerator::~MotionBlurGenerator() noexcept
    {
        gpu_system_.DeallocCbvSrvUavDescBlock(std::move(rgb_to_nv12_cs_.desc_block));
        gpu_system_.DeallocCbvSrvUavDescBlock(std::move(nv12_to_rgb_cs_.desc_block));
        gpu_system_.DeallocCbvSrvUavDescBlock(std::move(neighbor_max_cs_.desc_block));
        gpu_system_.DeallocCbvSrvUavDescBlock(std::move(gather_cs_.desc_block));
        gpu_system_.DeallocCbvSrvUavDescBlock(std::move(overlay_cs_.desc_block));
    }

    MotionBlurGenerator::MotionBlurGenerator(MotionBlurGenerator&& other) noexcept
        : gpu_system_(other.gpu_system_), random_tex_(std::move(other.random_tex_)),
          video_motion_estimator_(std::move(other.video_motion_estimator_)), max_mv_width_(std::exchange(other.max_mv_width_, 0)),
          max_mv_height_(std::exchange(other.max_mv_height_, 0)), min_mv_width_(std::exchange(other.min_mv_width_, 0)),
          min_mv_height_(std::exchange(other.min_mv_height_, 0)), mv_block_size_(std::exchange(other.mv_block_size_, 0)),
          rgb_to_nv12_cs_(std::move(other.rgb_to_nv12_cs_)), nv12_to_rgb_cs_(std::move(other.nv12_to_rgb_cs_)),
          neighbor_max_cs_(std::move(other.neighbor_max_cs_)), gather_cs_(std::move(other.gather_cs_)),
          overlay_cs_(std::move(other.overlay_cs_)), frames_(std::move(other.frames_))
    {
    }

    MotionBlurGenerator& MotionBlurGenerator::operator=(MotionBlurGenerator&& other) noexcept
    {
        if (this != &other)
        {
            assert(&gpu_system_ == &other.gpu_system_);

            random_tex_ = std::move(other.random_tex_);
            video_motion_estimator_ = std::move(other.video_motion_estimator_);
            max_mv_width_ = std::exchange(other.max_mv_width_, 0);
            max_mv_height_ = std::exchange(other.max_mv_height_, 0);
            min_mv_width_ = std::exchange(other.min_mv_width_, 0);
            min_mv_height_ = std::exchange(other.min_mv_height_, 0);
            mv_block_size_ = std::exchange(other.mv_block_size_, 0);
            rgb_to_nv12_cs_ = std::move(other.rgb_to_nv12_cs_);
            nv12_to_rgb_cs_ = std::move(other.nv12_to_rgb_cs_);
            neighbor_max_cs_ = std::move(other.neighbor_max_cs_);
            gather_cs_ = std::move(other.gather_cs_);
            overlay_cs_ = std::move(other.overlay_cs_);
            frames_ = std::move(other.frames_);
        }

        return *this;
    }

    bool MotionBlurGenerator::ConfirmDeviceFunc(ID3D12Device* device)
    {
        D3D12_FEATURE_DATA_VIDEO_MOTION_ESTIMATOR motion_estimator_support{0, DXGI_FORMAT_NV12};

        winrt::com_ptr<ID3D12Device> d3d12_device;
        d3d12_device.copy_from(device);
        winrt::com_ptr<ID3D12VideoDevice1> video_device = d3d12_device.try_as<ID3D12VideoDevice1>();
        if (video_device)
        {
            TIFHR(video_device->CheckFeatureSupport(
                D3D12_FEATURE_VIDEO_MOTION_ESTIMATOR, &motion_estimator_support, sizeof(motion_estimator_support)));

            if ((motion_estimator_support.BlockSizeFlags != D3D12_VIDEO_MOTION_ESTIMATOR_SEARCH_BLOCK_SIZE_FLAG_NONE) &&
                (motion_estimator_support.PrecisionFlags != D3D12_VIDEO_MOTION_ESTIMATOR_VECTOR_PRECISION_FLAG_NONE))
            {
                return true;
            }
        }

        return false;
    }

    uint64_t MotionBlurGenerator::AddFrame(
        GpuTexture2D& motion_blurred_tex, const GpuTexture2D& frame_tex, float time_span, bool overlay_mv)
    {
        const uint32_t this_frame = gpu_system_.FrameIndex() % GpuSystem::FrameCount;
        const uint32_t prev_frame = (gpu_system_.FrameIndex() + GpuSystem::FrameCount - 1) % GpuSystem::FrameCount;

        const bool first_frame = !static_cast<bool>(frames_[prev_frame].frame_rgb_tex);
        if (first_frame)
        {
            const uint32_t width = frame_tex.Width(0);
            const uint32_t height = frame_tex.Height(0);

            uint32_t scaled_width = width;
            uint32_t scaled_height = height;
            if ((width > max_mv_width_) || (height > max_mv_height_))
            {
                if (static_cast<float>(max_mv_width_) / width < static_cast<float>(max_mv_height_) / height)
                {
                    scaled_width = max_mv_width_;
                    scaled_height = static_cast<uint32_t>(height * max_mv_width_ / width);
                }
                else
                {
                    scaled_width = static_cast<uint32_t>(width * max_mv_height_ / height);
                    scaled_height = max_mv_height_;
                }
            }
            if ((width < min_mv_width_) || (height < min_mv_height_))
            {
                if (static_cast<float>(min_mv_width_) / width < static_cast<float>(min_mv_height_) / height)
                {
                    scaled_width = min_mv_width_;
                    scaled_height = static_cast<uint32_t>(height * min_mv_width_ / width);
                }
                else
                {
                    scaled_width = static_cast<uint32_t>(width * min_mv_height_ / height);
                    scaled_height = min_mv_height_;
                }
            }
            // NV12 must be in multiple of 2
            scaled_width &= ~1u;
            scaled_height &= ~1u;

            for (uint32_t i = 0; i < GpuSystem::FrameCount; ++i)
            {
                DXGI_FORMAT rgb_fmt;
                if (frame_tex.Format() != DXGI_FORMAT_NV12)
                {
                    rgb_fmt = frame_tex.Format();
                }
                else
                {
                    rgb_fmt = DXGI_FORMAT_R8G8B8A8_UNORM;
                }
                frames_[i].frame_rgb_tex = GpuTexture2D(gpu_system_, width, height, 1, rgb_fmt, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_COMMON, std::format(L"frame_rgb {}", i));
                frames_[i].frame_nv12_tex = GpuTexture2D(gpu_system_, width, height, 1, DXGI_FORMAT_NV12,
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, std::format(L"frame_nv12 {}", i));

                frames_[i].scaled_frame_nv12_tex = GpuTexture2D(gpu_system_, scaled_width, scaled_height, 1, DXGI_FORMAT_NV12,
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, std::format(L"scaled_frame_nv12 {}", i));

                frames_[i].raw_motion_vector_tex =
                    GpuTexture2D(gpu_system_, DivUp(scaled_width, mv_block_size_), DivUp(scaled_height, mv_block_size_), 1,
                        DXGI_FORMAT_R16G16_SINT, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS,
                        D3D12_RESOURCE_STATE_COMMON, std::format(L"raw_motion_vector_tex {}", i));

                // Always scale to 16x16 block size
                const DXGI_FORMAT motion_vector_fmt = DXGI_FORMAT_R8G8_UNORM;
                frames_[i].motion_vector_tex = GpuTexture2D(gpu_system_, DivUp(width, 16), DivUp(height, 16), 1, motion_vector_fmt,
                    D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON, std::format(L"motion_vector_tex {}", i));
                frames_[i].motion_vector_neighbor_max_tex = GpuTexture2D(gpu_system_, frames_[i].motion_vector_tex.Width(0),
                    frames_[i].motion_vector_tex.Height(0), 1, motion_vector_fmt, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                    D3D12_RESOURCE_STATE_COMMON, std::format(L"motion_vector_neighbor_max_tex {}", i));
            }

            {
                rgb_to_nv12_cs_.cb->frame_width_height = {scaled_width, scaled_height};
                rgb_to_nv12_cs_.cb.UploadToGpu();
            }
            {
                nv12_to_rgb_cs_.cb->frame_width_height = {width, height};
                nv12_to_rgb_cs_.cb.UploadToGpu();
            }
            {
                neighbor_max_cs_.cb->inv_half_frame_width_height = {2.0f / width, 2.0f / height};
                neighbor_max_cs_.cb->motion_vector_width_height = {
                    frames_[0].motion_vector_tex.Width(0), frames_[0].motion_vector_tex.Height(0)};
                neighbor_max_cs_.cb->raw_motion_vector_width_height = {
                    frames_[0].raw_motion_vector_tex.Width(0), frames_[0].raw_motion_vector_tex.Height(0)};
                neighbor_max_cs_.cb->blur_radius = BlurRadius;
                neighbor_max_cs_.cb->size_scale = static_cast<float>(width) / scaled_width;
                // Upload later
            }
            {
                gather_cs_.cb->inv_frame_width_height = {1.0f / width, 1.0f / height};
                gather_cs_.cb->blur_radius = BlurRadius;
                gather_cs_.cb->half_exposure = Exposure / 2;
                gather_cs_.cb->reconstruction_samples = ReconstructionSamples;
                gather_cs_.cb->max_sample_tap_distance = (2 * height + 1056) / 416.0f;
                gather_cs_.cb.UploadToGpu();
            }
            {
                overlay_cs_.cb->max_sample_tap_distance = (2 * height + 1056) / 416.0f;
                overlay_cs_.cb->motion_vector_block_size = 16;
                overlay_cs_.cb.UploadToGpu();
            }
        }
        else
        {
            assert(frames_[prev_frame].frame_rgb_tex.Width(0) == frame_tex.Width(0));
            assert(frames_[prev_frame].frame_rgb_tex.Height(0) == frame_tex.Height(0));
        }

        uint64_t fence_value;
        if (frame_tex.Format() == DXGI_FORMAT_NV12)
        {
            auto cmd_list = gpu_system_.CreateCommandList(GpuSystem::CmdQueueType::Compute);
            for (uint32_t p = 0; p < frame_tex.Planes(); ++p)
            {
                const D3D12_BOX src_box{0, 0, 0, frame_tex.Width(0) / (1U << p), frame_tex.Height(0) / (1U << p), 1};
                frames_[this_frame].frame_nv12_tex.CopyFrom(gpu_system_, cmd_list, frame_tex, p, 0, 0, src_box);
            }
            gpu_system_.Execute(std::move(cmd_list));

            fence_value = this->ConvertToRgb(frames_[this_frame].frame_nv12_tex, frames_[this_frame].frame_rgb_tex);
            fence_value = this->ConvertToNv12(frames_[this_frame].frame_rgb_tex, frames_[this_frame].scaled_frame_nv12_tex);
        }
        else
        {
            auto cmd_list = gpu_system_.CreateCommandList(GpuSystem::CmdQueueType::Compute);
            for (uint32_t p = 0; p < frame_tex.Planes(); ++p)
            {
                const D3D12_BOX src_box{0, 0, 0, frame_tex.Width(0) / (1U << p), frame_tex.Height(0) / (1U << p), 1};
                frames_[this_frame].frame_rgb_tex.CopyFrom(gpu_system_, cmd_list, frame_tex, p, 0, 0, src_box);
            }
            gpu_system_.Execute(std::move(cmd_list));

            fence_value = this->ConvertToNv12(frames_[this_frame].frame_rgb_tex, frames_[this_frame].scaled_frame_nv12_tex);
        }

        if (first_frame)
        {
            auto cmd_list = gpu_system_.CreateCommandList(GpuSystem::CmdQueueType::Compute);
            const D3D12_BOX src_box{0, 0, 0, frame_tex.Width(0), frame_tex.Height(0), 1};
            motion_blurred_tex.CopyFrom(gpu_system_, cmd_list, frames_[this_frame].frame_rgb_tex, 0, 0, 0, src_box);
            fence_value = gpu_system_.Execute(std::move(cmd_list));
        }
        else
        {
            fence_value = this->EstimateMotionVectors(frames_[prev_frame].scaled_frame_nv12_tex, frames_[this_frame].scaled_frame_nv12_tex,
                frames_[this_frame].raw_motion_vector_tex, frames_[this_frame].video_motion_vector_heap.get(), fence_value);
            fence_value = this->PropagateMotionBlur(time_span, frames_[this_frame].raw_motion_vector_tex,
                frames_[this_frame].motion_vector_tex, frames_[this_frame].motion_vector_neighbor_max_tex, fence_value);
            fence_value = this->GatherMotionBlur(frames_[this_frame].frame_rgb_tex, frames_[this_frame].motion_vector_tex,
                frames_[this_frame].motion_vector_neighbor_max_tex, motion_blurred_tex);

            if (overlay_mv)
            {
                fence_value = this->OverlayMotionVector(frames_[this_frame].motion_vector_tex, motion_blurred_tex);
            }
        }

        return fence_value;
    }

    uint64_t MotionBlurGenerator::ConvertToNv12(GpuTexture2D& frame_rgb_tex, GpuTexture2D& output_frame_nv12_tex)
    {
        const SrvHelper srv_texs[] = {
            {&frame_rgb_tex},
        };
        const UavHelper uav_texs[] = {
            {&output_frame_nv12_tex, 0, DXGI_FORMAT_R8_UNORM},
            {&output_frame_nv12_tex, 1, DXGI_FORMAT_R8G8_UNORM},
        };
        return this->RunComputeShader(
            srv_texs, uav_texs, rgb_to_nv12_cs_, output_frame_nv12_tex.Width(0) / 2, output_frame_nv12_tex.Height(0) / 2);
    }

    uint64_t MotionBlurGenerator::ConvertToRgb(GpuTexture2D& frame_nv12_tex, GpuTexture2D& output_frame_rgb_tex)
    {
        const SrvHelper srv_texs[] = {
            {&frame_nv12_tex, 0, DXGI_FORMAT_R8_UNORM},
            {&frame_nv12_tex, 1, DXGI_FORMAT_R8G8_UNORM},
        };
        const UavHelper uav_texs[] = {
            {&output_frame_rgb_tex},
        };
        return this->RunComputeShader(srv_texs, uav_texs, nv12_to_rgb_cs_, output_frame_rgb_tex.Width(0), output_frame_rgb_tex.Height(0));
    }

    uint64_t MotionBlurGenerator::EstimateMotionVectors(GpuTexture2D& ref_frame_nv12_tex, GpuTexture2D& input_frame_nv12_tex,
        GpuTexture2D& output_motion_vector_tex, ID3D12VideoMotionVectorHeap* video_mv_heap, uint64_t wait_fence_value)
    {
        GpuCommandList cmd_list = gpu_system_.CreateCommandList(GpuSystem::CmdQueueType::VideoEncode);
        auto* video_cmd_list = cmd_list.NativeCommandList<ID3D12VideoEncodeCommandList>();

        {
            ref_frame_nv12_tex.Transition(cmd_list, D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ);
            input_frame_nv12_tex.Transition(cmd_list, D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ);

            const D3D12_VIDEO_MOTION_ESTIMATOR_OUTPUT output_args = {video_mv_heap};
            const D3D12_VIDEO_MOTION_ESTIMATOR_INPUT input_args = {
                input_frame_nv12_tex.NativeTexture(), 0, ref_frame_nv12_tex.NativeTexture(), 0, nullptr};
            video_cmd_list->EstimateMotion(video_motion_estimator_.get(), &output_args, &input_args);
        }
        {
            output_motion_vector_tex.Transition(cmd_list, D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE);

            const D3D12_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_OUTPUT output_args = {output_motion_vector_tex.NativeTexture(), {}};
            const D3D12_RESOLVE_VIDEO_MOTION_VECTOR_HEAP_INPUT input_args = {
                video_mv_heap, ref_frame_nv12_tex.Width(0), ref_frame_nv12_tex.Height(0)};
            video_cmd_list->ResolveMotionVectorHeap(&output_args, &input_args);
        }

        {
            ref_frame_nv12_tex.Transition(cmd_list, D3D12_RESOURCE_STATE_COMMON);
            input_frame_nv12_tex.Transition(cmd_list, D3D12_RESOURCE_STATE_COMMON);
            output_motion_vector_tex.Transition(cmd_list, D3D12_RESOURCE_STATE_COMMON);
        }

        return gpu_system_.Execute(std::move(cmd_list), wait_fence_value);
    }

    uint64_t MotionBlurGenerator::PropagateMotionBlur(float time_span, GpuTexture2D& raw_motion_vector_tex,
        GpuTexture2D& output_motion_vector_tex, GpuTexture2D& output_motion_vector_neighbor_max_tex, uint64_t wait_fence_value)
    {
        {
            neighbor_max_cs_.cb->half_exposure_x_framerate = Exposure / 2 / time_span;
            neighbor_max_cs_.cb.UploadToGpu();
        }

        const SrvHelper srv_texs[] = {
            {&raw_motion_vector_tex},
        };
        const UavHelper uav_texs[] = {
            {&output_motion_vector_tex},
            {&output_motion_vector_neighbor_max_tex},
        };
        return this->RunComputeShader(
            srv_texs, uav_texs, neighbor_max_cs_, output_motion_vector_tex.Width(0), output_motion_vector_tex.Height(0), wait_fence_value);
    }

    uint64_t MotionBlurGenerator::GatherMotionBlur(GpuTexture2D& frame_tex, GpuTexture2D& motion_vector_tex,
        GpuTexture2D& motion_vector_neighbor_max_tex, GpuTexture2D& output_motion_blurred_tex)
    {
        const SrvHelper srv_texs[] = {
            {&frame_tex},
            {&motion_vector_tex},
            {&motion_vector_neighbor_max_tex},
            {&random_tex_},
        };
        const UavHelper uav_texs[] = {
            {&output_motion_blurred_tex},
        };
        return this->RunComputeShader(srv_texs, uav_texs, gather_cs_, frame_tex.Width(0), frame_tex.Height(0));
    }

    uint64_t MotionBlurGenerator::OverlayMotionVector(GpuTexture2D& motion_vector_tex, GpuTexture2D& output_overlaid_tex)
    {
        const SrvHelper srv_texs[] = {
            {&motion_vector_tex},
        };
        const UavHelper uav_texs[] = {
            {&output_overlaid_tex},
        };
        return this->RunComputeShader(srv_texs, uav_texs, overlay_cs_, motion_vector_tex.Width(0), motion_vector_tex.Height(0));
    }

    template <typename CbType, size_t ShaderSize>
    void MotionBlurGenerator::CreateComputeShader(ID3D12Device* device, ComputeShaderHelper<CbType>& cs,
        const unsigned char (&shader)[ShaderSize], std::span<const D3D12_STATIC_SAMPLER_DESC> samplers)
    {
        cs.desc_block = gpu_system_.AllocCbvSrvUavDescBlock((cs.num_srvs + cs.num_uavs) * GpuSystem::FrameCount);

        const D3D12_DESCRIPTOR_RANGE ranges[] = {
            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, cs.num_srvs, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
            {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, cs.num_uavs, 0, 0, D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND},
        };

        const D3D12_ROOT_PARAMETER root_params[] = {
            CreateRootParameterAsDescriptorTable(&ranges[0], 1),
            CreateRootParameterAsDescriptorTable(&ranges[1], 1),
            CreateRootParameterAsConstantBufferView(0),
        };

        const D3D12_ROOT_SIGNATURE_DESC root_signature_desc = {static_cast<uint32_t>(std::size(root_params)), root_params,
            static_cast<uint32_t>(samplers.size()), samplers.data(), D3D12_ROOT_SIGNATURE_FLAG_NONE};

        winrt::com_ptr<ID3DBlob> blob;
        winrt::com_ptr<ID3DBlob> error;
        HRESULT hr = ::D3D12SerializeRootSignature(&root_signature_desc, D3D_ROOT_SIGNATURE_VERSION_1, blob.put(), error.put());
        if (FAILED(hr))
        {
            ::OutputDebugStringW(
                std::format(L"D3D12SerializeRootSignature failed: {}\n", static_cast<wchar_t*>(error->GetBufferPointer())).c_str());
            TIFHR(hr);
        }

        TIFHR(device->CreateRootSignature(
            1, blob->GetBufferPointer(), blob->GetBufferSize(), winrt::guid_of<ID3D12RootSignature>(), cs.root_sig.put_void()));

        D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc;
        pso_desc.pRootSignature = cs.root_sig.get();
        pso_desc.CS.pShaderBytecode = shader;
        pso_desc.CS.BytecodeLength = ShaderSize;
        pso_desc.NodeMask = 0;
        pso_desc.CachedPSO.pCachedBlob = nullptr;
        pso_desc.CachedPSO.CachedBlobSizeInBytes = 0;
        pso_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        TIFHR(device->CreateComputePipelineState(&pso_desc, winrt::guid_of<ID3D12PipelineState>(), cs.pso.put_void()));
    }

    template <typename CbType>
    uint64_t MotionBlurGenerator::RunComputeShader(const SrvHelper srv_texs[], const UavHelper uav_texs[],
        const ComputeShaderHelper<CbType>& cs, uint32_t dispatch_x, uint32_t dispatch_y, uint64_t wait_fence_value)
    {
        const uint32_t descriptor_size = gpu_system_.CbvSrvUavDescSize();
        const uint32_t desc_block_base = (cs.num_srvs + cs.num_uavs) * gpu_system_.FrameIndex();

        auto srvs = std::make_unique<GpuShaderResourceView[]>(cs.num_srvs);
        for (uint32_t i = 0; i < cs.num_srvs; ++i)
        {
            if (srv_texs[i].sub_resource != ~0u)
            {
                srvs[i] = GpuShaderResourceView(gpu_system_, *srv_texs[i].tex, srv_texs[i].sub_resource, srv_texs[i].format,
                    OffsetHandle(cs.desc_block.CpuHandle(), desc_block_base + i, descriptor_size));
            }
            else
            {
                srvs[i] = GpuShaderResourceView(
                    gpu_system_, *srv_texs[i].tex, OffsetHandle(cs.desc_block.CpuHandle(), desc_block_base + i, descriptor_size));
            }
        }

        auto uavs = std::make_unique<GpuUnorderedAccessView[]>(cs.num_uavs);
        for (uint32_t i = 0; i < cs.num_uavs; ++i)
        {
            uavs[i] = GpuUnorderedAccessView(gpu_system_, *uav_texs[i].tex, uav_texs[i].sub_resource, uav_texs[i].format,
                OffsetHandle(cs.desc_block.CpuHandle(), desc_block_base + cs.num_srvs + i, descriptor_size));
        }

        auto cmd_list = gpu_system_.CreateCommandList(GpuSystem::CmdQueueType::Compute);
        auto* d3d12_cmd_list = cmd_list.NativeCommandList<ID3D12GraphicsCommandList>();

        d3d12_cmd_list->SetComputeRootSignature(cs.root_sig.get());
        d3d12_cmd_list->SetPipelineState(cs.pso.get());

        ID3D12DescriptorHeap* heaps[] = {cs.desc_block.NativeDescriptorHeap()};
        d3d12_cmd_list->SetDescriptorHeaps(static_cast<uint32_t>(std::size(heaps)), heaps);
        d3d12_cmd_list->SetComputeRootDescriptorTable(0, OffsetHandle(cs.desc_block.GpuHandle(), desc_block_base + 0, descriptor_size));
        d3d12_cmd_list->SetComputeRootDescriptorTable(
            1, OffsetHandle(cs.desc_block.GpuHandle(), desc_block_base + cs.num_srvs, descriptor_size));
        d3d12_cmd_list->SetComputeRootConstantBufferView(2, cs.cb.GpuVirtualAddress());

        for (uint32_t i = 0; i < cs.num_srvs; ++i)
        {
            srv_texs[i].tex->Transition(cmd_list, D3D12_RESOURCE_STATE_COMMON);
        }
        for (uint32_t i = 0; i < cs.num_uavs; ++i)
        {
            uav_texs[i].tex->Transition(cmd_list, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        }

        constexpr uint32_t BlockDim = 16;
        d3d12_cmd_list->Dispatch(DivUp(dispatch_x, BlockDim), DivUp(dispatch_y, BlockDim), 1);

        for (uint32_t i = 0; i < cs.num_uavs; ++i)
        {
            // Need to transite it back to COMMON state here, since video encode command list can't handle UNORDERED_ACCESS state.
            uav_texs[i].tex->Transition(cmd_list, D3D12_RESOURCE_STATE_COMMON);
        }

        return gpu_system_.Execute(std::move(cmd_list), wait_fence_value);
    }
} // namespace MotionToGo
