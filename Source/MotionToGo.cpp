#include <chrono>
#include <filesystem>
#include <format>
#include <future>
#include <iostream>
#include <string>
#include <string_view>

#ifndef _DEBUG
#define CXXOPTS_NO_RTTI
#endif
#include <cxxopts.hpp>

namespace
{
    unsigned char* StbiZlibCompress(unsigned char* data, int data_len, int* out_len, int quality);
}

#define STBIW_ZLIB_COMPRESS StbiZlibCompress
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <zlib.h>

#include "ErrorHandling.hpp"
#include "Gpu/GpuCommandList.hpp"
#include "Gpu/GpuSystem.hpp"
#include "MotionBlurGenerator/MotionBlurGenerator.hpp"
#include "Reader/Reader.hpp"

using namespace MotionToGo;

namespace
{
    // Port from https://blog.gibson.sh/2015/07/18/comparing-png-compression-ratios-of-stb_image_write-lodepng-miniz-and-libpng/
    unsigned char* StbiZlibCompress(unsigned char* data, int data_len, int* out_len, int quality)
    {
        uLong buff_len = compressBound(data_len);
        uint8_t* buf = reinterpret_cast<uint8_t*>(std::malloc(buff_len));
        if ((buf == nullptr) || (compress2(buf, &buff_len, data, data_len, quality) != 0))
        {
            free(buf);
            return nullptr;
        }
        *out_len = buff_len;
        return buf;
    }

    std::future<void> SaveTexture(GpuSystem& gpu_system, const GpuTexture2D& texture, const std::filesystem::path& file_path)
    {
        assert(texture);

        const uint32_t width = texture.Width(0);
        const uint32_t height = texture.Height(0);
        const uint32_t format_size = FormatSize(texture.Format());

        std::vector<uint8_t> data(width * height * format_size);
        auto cmd_list = gpu_system.CreateCommandList(GpuSystem::CmdQueueType::Compute);
        texture.Readback(gpu_system, cmd_list, 0, data.data());
        gpu_system.Execute(std::move(cmd_list));

        return std::async(std::launch::async, [file_path = file_path.string(), width, height, format_size, data = std::move(data)]() {
            stbi_write_png(
                file_path.c_str(), static_cast<int>(width), static_cast<int>(height), 4, data.data(), static_cast<int>(width * format_size));
            });
    }
}

int main(int argc, char* argv[])
{
    stbi_write_png_compression_level = 5;

    cxxopts::Options options("MotionToGo", "MotionToGo: Add motion blur to a image sequence.");
    // clang-format off
    options.add_options()
        ("H,help", "Produce help message.")
        ("I,input-path", "The directory that contains the input image sequence, or the path of the video file.", cxxopts::value<std::string>())
        ("O,output-directory", "The output directory (\"<input-dir>/Output\" by default).", cxxopts::value<std::string>())
        ("F,framerate", "The framerate of the image sequence (24 by default).", cxxopts::value<float>())
        ("L,overlay", "Overlay motion vector to outputs (Off by default).", cxxopts::value<bool>())
        ("v,version", "Version.");
    // clang-format on

    const auto vm = options.parse(argc, argv);

    if ((argc <= 1) || (vm.count("help") > 0))
    {
        std::cout << std::format("{}\n", options.help());
        return 0;
    }
    if (vm.count("version") > 0)
    {
        std::cout << "MotionToGo, Version 0.1.0\n";
        return 0;
    }

    std::filesystem::path input_path;
    if (vm.count("input-path") > 0)
    {
        input_path = vm["input-path"].as<std::string>();
    }
    else
    {
        std::cerr << std::format("ERROR: MUST have a input path\n");
        return 1;
    }

    if (!std::filesystem::exists(input_path))
    {
        std::cerr << std::format("ERROR: COULDN'T find {}\n", input_path.string());
        return 1;
    }

    const bool image_seq = std::filesystem::is_directory(input_path);
    if (!image_seq && !std::filesystem::is_regular_file(input_path))
    {
        std::cerr << std::format("ERROR: {} is not a file or a directory\n", input_path.string());
        return 1;
    }

    std::filesystem::path output_dir;
    if (vm.count("output-directory") > 0)
    {
        output_dir = vm["output-directory"].as<std::string>();
    }
    else
    {
        if (image_seq)
        {
            output_dir = input_path / "Output";
        }
        else
        {
            output_dir = input_path.parent_path() / "Output";
        }
    }

    float framerate;
    if (vm.count("framerate") > 0)
    {
        framerate = vm["framerate"].as<float>();
    }
    else
    {
        framerate = 24;
    }

    bool overlay_mv;
    if (vm.count("overlay") > 0)
    {
        overlay_mv = vm["overlay"].as<bool>();
    }
    else
    {
        overlay_mv = false;
    }

    TIFHR(CoInitializeEx(0, COINIT_MULTITHREADED));

    GpuSystem gpu_system(MotionBlurGenerator::ConfirmDeviceFunc);

    std::unique_ptr<Reader> reader;
    if (image_seq)
    {
        reader = CreateImageSeqReader(gpu_system, input_path, framerate);
    }
    else
    {
        reader = CreateVideoReader(gpu_system, input_path);
    }

    MotionBlurGenerator motion_blur_gen(gpu_system);

    std::filesystem::create_directories(output_dir);

    GpuTexture2D frame_texs[GpuSystem::FrameCount];
    GpuTexture2D motion_blurred_texs[GpuSystem::FrameCount];
    std::vector<std::future<void>> saving_threads;

    uint32_t total_frames = 0;
    const auto start = std::chrono::high_resolution_clock::now();
    for (uint32_t i = 0;; ++i)
    {
        if (total_frames == 0)
        {
            const uint32_t this_frame = gpu_system.FrameIndex() % GpuSystem::FrameCount;
            float timespan;
            if (reader->ReadFrame(frame_texs[this_frame], timespan))
            {
                std::cout << std::format("Processing frame {}\n", i + 1);

                if (!motion_blurred_texs[this_frame])
                {
                    motion_blurred_texs[this_frame] = GpuTexture2D(gpu_system, frame_texs[this_frame].Width(0),
                        frame_texs[this_frame].Height(0), 1, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                        D3D12_RESOURCE_STATE_COMMON, std::format(L"motion_blurred_tex {}", this_frame));
                }

                motion_blur_gen.AddFrame(motion_blurred_texs[this_frame], frame_texs[this_frame], timespan, overlay_mv);

                gpu_system.MoveToNextFrame();
            }
            else
            {
                total_frames = i;
            }
        }

        if (i >= GpuSystem::FrameCount - 1)
        {
            for (auto iter = saving_threads.begin(); iter != saving_threads.end();)
            {
                auto& th = *iter;
                if (th.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
                {
                    iter = saving_threads.erase(iter);
                }
                else
                {
                    ++iter;
                }
            }

            const size_t saving_index = i - (GpuSystem::FrameCount - 1);
            saving_threads.push_back(SaveTexture(gpu_system, motion_blurred_texs[saving_index % GpuSystem::FrameCount],
                output_dir / std::format("Frame_{}.png", saving_index + 1)));
        }

        if ((total_frames != 0) && (i == total_frames - 1 + GpuSystem::FrameCount - 1))
        {
            break;
        }
    }

    for (auto& th : saving_threads)
    {
        th.wait();
    }

    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start);

    std::cout << std::format("\nDone. Outputs are saved to {}.\n", output_dir.string());
    std::cout << std::format(
        "Processing time per frame: {}\n", std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(duration / total_frames));

    gpu_system.WaitForGpu();
    reader.reset();

    CoUninitialize();

    return 0;
}
