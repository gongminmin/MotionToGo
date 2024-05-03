#pragma once

#include <filesystem>
#include <memory>

#include "Gpu/GpuSystem.hpp"
#include "Gpu/GpuTexture2D.hpp"
#include "Noncopyable.hpp"

namespace MotionToGo
{
    class Reader
    {
        DISALLOW_COPY_AND_ASSIGN(Reader)

    public:
        Reader() noexcept;
        virtual ~Reader() noexcept;

        virtual bool ReadFrame(GpuTexture2D& frame_tex, float& timespan) = 0;
    };

    std::unique_ptr<Reader> CreateImageSeqReader(GpuSystem& gpu_system, const std::filesystem::path& dir, float framerate);
    std::unique_ptr<Reader> CreateVideoReader(GpuSystem& gpu_system, const std::filesystem::path& file_path);
} // namespace MotionToGo
