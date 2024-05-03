#include "Reader.hpp"

#include <filesystem>
#include <string>

#include <mfapi.h>
#include <mfd3d12.h>
#include <mfreadwrite.h>

#include "ErrorHandling.hpp"
#include "Gpu/GpuCommandList.hpp"

namespace MotionToGo
{
    class VideoReader final : public Reader
    {
    public:
        VideoReader(GpuSystem& gpu_system, const std::filesystem::path& file_path) : gpu_system_(gpu_system)
        {
            TIFHR(::MFStartup(MF_VERSION, MFSTARTUP_FULL));

            TIFHR(::MFCreateDXGIDeviceManager(&reset_token_, mf_device_manager_.put()));

            ID3D12Device* d3d12_device = gpu_system.NativeDevice();
            TIFHR(mf_device_manager_->ResetDevice(d3d12_device, reset_token_));

            winrt::com_ptr<IMFAttributes> mf_attrs;
            TIFHR(::MFCreateAttributes(mf_attrs.put(), 1));

            TIFHR(mf_attrs->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, mf_device_manager_.get()));

            std::wstring url = L"file:///" + file_path.wstring();
            std::replace(url.begin(), url.end(), '\\', '/');

            TIFHR(::MFCreateSourceReaderFromURL(url.c_str(), mf_attrs.get(), source_reader_.put()));

            {
                winrt::com_ptr<IMFMediaType> type;
                TIFHR(MFCreateMediaType(type.put()));
                TIFHR(type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
                TIFHR(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12));
                TIFHR(source_reader_->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type.get()));
            }

            TIFHR(source_reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE));

            {
                winrt::com_ptr<IMFMediaType> type;
                source_reader_->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), type.put());
                TIFHR(MFGetAttributeSize(type.get(), MF_MT_FRAME_SIZE, &video_width_, &video_height_));
            }
        }

        ~VideoReader() noexcept override
        {
            source_reader_->Flush(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS));
            source_reader_ = nullptr;

            mf_device_manager_ = nullptr;

            ::MFShutdown();
        }

        bool ReadFrame(GpuTexture2D& frame_tex, float& timespan) override
        {
            winrt::com_ptr<IMFD3D12SynchronizationObjectCommands> mf_sync_cmd;

            DWORD read_flags = 0;
            DWORD actual_stream_index;
            DWORD stream_flags;
            LONGLONG timestamp;
            winrt::com_ptr<IMFSample> sample;
            do
            {
                TIFHR(source_reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), read_flags, &actual_stream_index,
                    &stream_flags, &timestamp, sample.put()));

                if (stream_flags & MF_SOURCE_READERF_ENDOFSTREAM)
                {
                    return false;
                }
            } while (sample == nullptr);

            timespan = curr_frame_ == 0 ? 0 : (timestamp - last_timestamp_) * 1e-7f;
            last_timestamp_ = timestamp;

            ++curr_frame_;

            DWORD buffer_count;
            TIFHR(sample->GetBufferCount(&buffer_count));

            winrt::com_ptr<IMFMediaBuffer> output_media_buffer;
            TIFHR(sample->GetBufferByIndex(0, output_media_buffer.put()));

            winrt::com_ptr<IMFDXGIBuffer> dxgi_buffer;
            if (output_media_buffer.try_as(dxgi_buffer))
            {
                winrt::com_ptr<ID3D12Resource> texture;
                TIFHR(dxgi_buffer->GetResource(winrt::guid_of<ID3D12Resource>(), texture.put_void()));

                TIFHR(dxgi_buffer->GetUnknown(
                    MF_D3D12_SYNCHRONIZATION_OBJECT, winrt::guid_of<IMFD3D12SynchronizationObjectCommands>(), mf_sync_cmd.put_void()));

                ID3D12CommandQueue* cmd_queue = gpu_system_.NativeCommandQueue(GpuSystem::CmdQueueType::Graphics);
                mf_sync_cmd->EnqueueResourceReadyWait(cmd_queue);

                GpuTexture2D mf_texture(texture.detach(), D3D12_RESOURCE_STATE_COMMON, L"mf_texture");
                frame_tex = GpuTexture2D(gpu_system_, video_width_, video_height_, 1, mf_texture.Format(), D3D12_RESOURCE_FLAG_NONE,
                    D3D12_RESOURCE_STATE_COMMON, L"frame_tex");

                {
                    auto cmd_list = gpu_system_.CreateCommandList(GpuSystem::CmdQueueType::Graphics);
                    for (uint32_t p = 0; p < mf_texture.Planes(); ++p)
                    {
                        const D3D12_BOX src_box{0, 0, 0, video_width_ / (1U << p), video_height_ / (1U << p), 1};
                        frame_tex.CopyFrom(gpu_system_, cmd_list, mf_texture, p, 0, 0, src_box);
                    }
                    gpu_system_.Execute(std::move(cmd_list));
                }

                mf_sync_cmd->EnqueueResourceRelease(cmd_queue);
            }

            return true;
        }

    private:
        GpuSystem& gpu_system_;

        UINT reset_token_;
        winrt::com_ptr<IMFDXGIDeviceManager> mf_device_manager_;
        winrt::com_ptr<IMFSourceReader> source_reader_;

        uint32_t curr_frame_ = 0;
        LONGLONG last_timestamp_ = 0;

        uint32_t video_width_;
        uint32_t video_height_;
    };

    std::unique_ptr<Reader> CreateVideoReader(GpuSystem& gpu_system, const std::filesystem::path& file_path)
    {
        return std::make_unique<VideoReader>(gpu_system, file_path);
    }
} // namespace MotionToGo
