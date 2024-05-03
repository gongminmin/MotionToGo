#include "GpuCommandList.hpp"

#include <winrt/base.h>

#include "ErrorHandling.hpp"
#include "GpuSystem.hpp"
#include "Util.hpp"

namespace MotionToGo
{
    GpuCommandList::GpuCommandList() noexcept = default;

    GpuCommandList::GpuCommandList(GpuSystem& gpu_system, ID3D12CommandAllocator* cmd_allocator, GpuSystem::CmdQueueType type) : type_(type)
    {
        switch (type)
        {
        case GpuSystem::CmdQueueType::Graphics:
            TIFHR(gpu_system.NativeDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, cmd_allocator, nullptr,
                winrt::guid_of<ID3D12GraphicsCommandList>(), cmd_list_.put_void()));
            break;

        case GpuSystem::CmdQueueType::VideoEncode:
            TIFHR(gpu_system.NativeDevice()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE, cmd_allocator, nullptr,
                winrt::guid_of<ID3D12VideoEncodeCommandList>(), cmd_list_.put_void()));
            break;

        default:
            Unreachable();
        }
    }

    GpuCommandList::~GpuCommandList() noexcept = default;

    GpuCommandList::GpuCommandList(GpuCommandList&& other) noexcept = default;
    GpuCommandList& GpuCommandList::operator=(GpuCommandList&& other) noexcept = default;

    GpuSystem::CmdQueueType GpuCommandList::Type() const noexcept
    {
        return type_;
    }

    GpuCommandList::operator bool() const noexcept
    {
        return cmd_list_ ? true : false;
    }

    void GpuCommandList::Transition(std::span<const D3D12_RESOURCE_BARRIER> barriers) const noexcept
    {
        switch (type_)
        {
        case GpuSystem::CmdQueueType::Graphics:
            static_cast<ID3D12GraphicsCommandList*>(cmd_list_.get())
                ->ResourceBarrier(static_cast<uint32_t>(barriers.size()), barriers.data());
            break;

        case GpuSystem::CmdQueueType::VideoEncode:
            static_cast<ID3D12VideoEncodeCommandList*>(cmd_list_.get())
                ->ResourceBarrier(static_cast<uint32_t>(barriers.size()), barriers.data());
            break;

        default:
            Unreachable();
        }
    }

    void GpuCommandList::Close()
    {
        switch (type_)
        {
        case GpuSystem::CmdQueueType::Graphics:
            static_cast<ID3D12GraphicsCommandList*>(cmd_list_.get())->Close();
            break;

        case GpuSystem::CmdQueueType::VideoEncode:
            static_cast<ID3D12VideoEncodeCommandList*>(cmd_list_.get())->Close();
            break;

        default:
            Unreachable();
        }
    }

    void GpuCommandList::Reset(ID3D12CommandAllocator* cmd_allocator)
    {
        switch (type_)
        {
        case GpuSystem::CmdQueueType::Graphics:
            static_cast<ID3D12GraphicsCommandList*>(cmd_list_.get())->Reset(cmd_allocator, nullptr);
            break;

        case GpuSystem::CmdQueueType::VideoEncode:
            static_cast<ID3D12VideoEncodeCommandList*>(cmd_list_.get())->Reset(cmd_allocator);
            break;

        default:
            Unreachable();
        }
    }
} // namespace MotionToGo
