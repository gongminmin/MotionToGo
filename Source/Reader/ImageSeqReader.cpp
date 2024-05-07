#include "Reader.hpp"

#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "Gpu/GpuCommandList.hpp"
#include "Gpu/GpuSystem.hpp"

using namespace MotionToGo;

namespace
{
    void LoadTexture(GpuSystem& gpu_system, const std::filesystem::path& file_path, DXGI_FORMAT format, GpuTexture2D& output_tex)
    {
        int width, height;
        uint8_t* data = stbi_load(file_path.string().c_str(), &width, &height, nullptr, 4);
        if (data != nullptr)
        {
            if (!output_tex || (output_tex.Width(0) != static_cast<uint32_t>(width)) ||
                (output_tex.Height(0) != static_cast<uint32_t>(height)) || (output_tex.Format() != format))
            {
                output_tex = GpuTexture2D(
                    gpu_system, width, height, 1, format, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
            }

            auto cmd_list = gpu_system.CreateCommandList(GpuSystem::CmdQueueType::Compute);
            output_tex.Upload(gpu_system, cmd_list, 0, data);
            gpu_system.Execute(std::move(cmd_list));

            stbi_image_free(data);
        }
    }
} // namespace

namespace MotionToGo
{
    class ImageSeqReader final : public Reader
    {
    public:
        ImageSeqReader(GpuSystem& gpu_system, const std::filesystem::path& dir, float framerate)
            : gpu_system_(gpu_system), dir_(dir), framerate_(framerate)
        {
            constexpr const std::string_view SupportedExts[] = {
                "jpg",
                "png",
                "tga",
                "bmp",
                "psd",
                "pnm",
            };

            for (const auto& entry : std::filesystem::directory_iterator(dir))
            {
                if (!entry.is_directory())
                {
                    const std::filesystem::path file(entry);
                    auto ext = file.extension().string().substr(1);
                    std::transform(ext.begin(), ext.end(), ext.begin(), [](char ch) { return static_cast<char>(std::tolower(ch)); });
                    if (std::find(std::begin(SupportedExts), std::end(SupportedExts), ext))
                    {
                        files_.push_back(file.filename());
                    }
                }
            }
            std::sort(files_.begin(), files_.end());
        }

        bool ReadFrame(GpuTexture2D& frame_tex, float& timespan)
        {
            timespan = 1.0f / framerate_;
            if (curr_frame_ < files_.size())
            {
                LoadTexture(gpu_system_, dir_ / files_[curr_frame_], DXGI_FORMAT_R8G8B8A8_UNORM, frame_tex);
                ++curr_frame_;

                return true;
            }

            return false;
        }

    private:
        GpuSystem& gpu_system_;
        std::filesystem::path dir_;
        float framerate_;
        std::vector<std::filesystem::path> files_;
        uint32_t curr_frame_ = 0;
    };

    std::unique_ptr<Reader> CreateImageSeqReader(GpuSystem& gpu_system, const std::filesystem::path& dir, float framerate)
    {
        return std::make_unique<ImageSeqReader>(gpu_system, dir, framerate);
    }
} // namespace MotionToGo
