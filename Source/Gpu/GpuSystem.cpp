#include "GpuSystem.hpp"

#include <list>

#include <dxgi1_6.h>
#include <winrt/base.h>

#ifdef _DEBUG
#include <dxgidebug.h>
#endif

#include "ErrorHandling.hpp"
#include "GpuCommandList.hpp"
#include "SmartPtrHelper.hpp"
#include "Util.hpp"

namespace MotionToGo
{
    GpuSystem::GpuSystem(std::function<bool(ID3D12Device* device)> confirm_device)
        : upload_mem_allocator_(*this, true), readback_mem_allocator_(*this, false),
          cbv_srv_uav_desc_allocator_(*this, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE)
    {
        bool debug_dxgi = false;

        winrt::com_ptr<IDXGIFactory4> dxgi_factory;
#ifdef _DEBUG
        winrt::com_ptr<ID3D12Debug> debug_ctrl;
        if (SUCCEEDED(::D3D12GetDebugInterface(winrt::guid_of<ID3D12Debug>(), debug_ctrl.put_void())))
        {
            debug_ctrl->EnableDebugLayer();
        }
        else
        {
            ::OutputDebugStringW(L"WARNING: Direct3D Debug Device is not available\n");
        }

        winrt::com_ptr<IDXGIInfoQueue> dxgi_info_queue;
        if (SUCCEEDED(::DXGIGetDebugInterface1(0, winrt::guid_of<IDXGIInfoQueue>(), dxgi_info_queue.put_void())))
        {
            debug_dxgi = true;

            TIFHR(::CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, winrt::guid_of<IDXGIFactory4>(), dxgi_factory.put_void()));

            dxgi_info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
            dxgi_info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
        }
#endif

        if (!debug_dxgi)
        {
            TIFHR(::CreateDXGIFactory2(0, winrt::guid_of<IDXGIFactory4>(), dxgi_factory.put_void()));
        }

        {
            winrt::com_ptr<IDXGIFactory6> factory6 = dxgi_factory.as<IDXGIFactory6>();

            uint32_t adapter_id = 0;
            winrt::com_ptr<IDXGIAdapter1> adapter;
            while (factory6->EnumAdapterByGpuPreference(adapter_id, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, winrt::guid_of<IDXGIAdapter1>(),
                       adapter.put_void()) != DXGI_ERROR_NOT_FOUND)
            {
                DXGI_ADAPTER_DESC1 desc;
                TIFHR(adapter->GetDesc1(&desc));

                if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                {
                    continue;
                }

                winrt::com_ptr<ID3D12Device> device;
                if (SUCCEEDED(
                        ::D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_11_0, winrt::guid_of<ID3D12Device>(), device.put_void())))
                {
                    if (confirm_device && confirm_device(device.get()))
                    {
                        device_ = std::move(device);
                        break;
                    }
                }

                ++adapter_id;

                adapter = nullptr;
            }
        }

#ifdef _DEBUG
        if (!device_)
        {
            winrt::com_ptr<IDXGIAdapter1> adapter;
            TIFHR(dxgi_factory->EnumWarpAdapter(winrt::guid_of<IDXGIAdapter1>(), adapter.put_void()));

            TIFHR(::D3D12CreateDevice(adapter.get(), D3D_FEATURE_LEVEL_11_0, winrt::guid_of<ID3D12Device>(), device_.put_void()));
        }
#endif

        Verify(device_ != nullptr);

#ifdef _DEBUG
        if (winrt::com_ptr<ID3D12InfoQueue> d3d_info_queue = device_.try_as<ID3D12InfoQueue>())
        {
            d3d_info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
            d3d_info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
        }
#endif

        for (uint32_t i = 0; i < static_cast<uint32_t>(CmdQueueType::Num); ++i)
        {
            D3D12_COMMAND_LIST_TYPE type;
            switch (static_cast<CmdQueueType>(i))
            {
            case CmdQueueType::Compute:
                type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
                break;

            case CmdQueueType::VideoEncode:
                type = D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE;
                break;

            default:
                Unreachable();
            }

            const D3D12_COMMAND_QUEUE_DESC queue_qesc{type, 0, D3D12_COMMAND_QUEUE_FLAG_NONE, 0};
            TIFHR(device_->CreateCommandQueue(&queue_qesc, winrt::guid_of<ID3D12CommandQueue>(), cmd_queues_[i].cmd_queue.put_void()));
            cmd_queues_[i].cmd_queue->SetName(std::format(L"cmd_queue {}", i).c_str());

            for (auto& allocator : cmd_queues_[i].cmd_allocators)
            {
                TIFHR(device_->CreateCommandAllocator(type, winrt::guid_of<ID3D12CommandAllocator>(), allocator.put_void()));
            }
        }

        TIFHR(device_->CreateFence(fence_vals_[frame_index_], D3D12_FENCE_FLAG_NONE, winrt::guid_of<ID3D12Fence>(), fence_.put_void()));
        ++fence_vals_[frame_index_];

        fence_event_ = MakeWin32UniqueHandle(::CreateEvent(nullptr, FALSE, FALSE, nullptr));
        Verify(fence_event_.get() != INVALID_HANDLE_VALUE);
    }

    GpuSystem::~GpuSystem() noexcept = default;
    GpuSystem::GpuSystem(GpuSystem&& other) noexcept = default;
    GpuSystem& GpuSystem::operator=(GpuSystem&& other) noexcept = default;

    ID3D12Device* GpuSystem::NativeDevice() const noexcept
    {
        return device_.get();
    }

    ID3D12CommandQueue* GpuSystem::NativeCommandQueue(CmdQueueType type) const noexcept
    {
        return cmd_queues_[static_cast<uint32_t>(type)].cmd_queue.get();
    }

    uint32_t GpuSystem::FrameIndex() const noexcept
    {
        return frame_index_;
    }

    void GpuSystem::MoveToNextFrame()
    {
        const uint64_t curr_fence_value = fence_vals_[frame_index_];
        for (uint32_t i = 0; i < static_cast<uint32_t>(CmdQueueType::Num); ++i)
        {
            TIFHR(cmd_queues_[i].cmd_queue->Signal(fence_.get(), curr_fence_value));
        }

        frame_index_ = (frame_index_ + 1) % FrameCount;

        if (fence_->GetCompletedValue() < fence_vals_[frame_index_])
        {
            TIFHR(fence_->SetEventOnCompletion(fence_vals_[frame_index_], fence_event_.get()));
            ::WaitForSingleObjectEx(fence_event_.get(), INFINITE, FALSE);
        }

        fence_vals_[frame_index_] = curr_fence_value + 1;

        for (uint32_t i = 0; i < static_cast<uint32_t>(CmdQueueType::Num); ++i)
        {
            TIFHR(this->CurrentCommandAllocator(static_cast<CmdQueueType>(i))->Reset());
        }
    }

    GpuCommandList GpuSystem::CreateCommandList(GpuSystem::CmdQueueType type)
    {
        auto* cmd_allocator = this->CurrentCommandAllocator(type);
        if (cmd_queues_[static_cast<uint32_t>(type)].cmd_list_pool.empty())
        {
            return GpuCommandList(*this, cmd_allocator, type);
        }
        else
        {
            GpuCommandList cmd_list = std::move(cmd_queues_[static_cast<uint32_t>(type)].cmd_list_pool.front());
            cmd_queues_[static_cast<uint32_t>(type)].cmd_list_pool.pop_front();
            cmd_list.Reset(cmd_allocator);
            return cmd_list;
        }
    }

    uint64_t GpuSystem::Execute(GpuCommandList&& cmd_list, uint64_t wait_fence_value)
    {
        const uint64_t new_fence_value = this->ExecuteOnly(cmd_list, wait_fence_value);
        cmd_queues_[static_cast<uint32_t>(cmd_list.Type())].cmd_list_pool.emplace_back(std::move(cmd_list));
        return new_fence_value;
    }

    uint64_t GpuSystem::ExecuteAndReset(GpuCommandList& cmd_list, uint64_t wait_fence_value)
    {
        const uint64_t new_fence_value = this->ExecuteOnly(cmd_list, wait_fence_value);
        cmd_list.Reset(this->CurrentCommandAllocator(cmd_list.Type()));
        return new_fence_value;
    }

    uint32_t GpuSystem::CbvSrvUavDescSize() const noexcept
    {
        return cbv_srv_uav_desc_allocator_.DescriptorSize();
    }

    GpuDescriptorBlock GpuSystem::AllocCbvSrvUavDescBlock(uint32_t size)
    {
        return cbv_srv_uav_desc_allocator_.Allocate(size);
    }

    void GpuSystem::DeallocCbvSrvUavDescBlock(GpuDescriptorBlock&& desc_block)
    {
        return cbv_srv_uav_desc_allocator_.Deallocate(std::move(desc_block), fence_vals_[frame_index_]);
    }

    void GpuSystem::ReallocCbvSrvUavDescBlock(GpuDescriptorBlock& desc_block, uint32_t size)
    {
        return cbv_srv_uav_desc_allocator_.Reallocate(desc_block, fence_vals_[frame_index_], size);
    }

    GpuMemoryBlock GpuSystem::AllocUploadMemBlock(uint32_t size_in_bytes, uint32_t alignment)
    {
        return upload_mem_allocator_.Allocate(size_in_bytes, alignment);
    }

    void GpuSystem::DeallocUploadMemBlock(GpuMemoryBlock&& mem_block)
    {
        return upload_mem_allocator_.Deallocate(std::move(mem_block), fence_vals_[frame_index_]);
    }

    void GpuSystem::ReallocUploadMemBlock(GpuMemoryBlock& mem_block, uint32_t size_in_bytes, uint32_t alignment)
    {
        return upload_mem_allocator_.Reallocate(mem_block, fence_vals_[frame_index_], size_in_bytes, alignment);
    }

    GpuMemoryBlock GpuSystem::AllocReadbackMemBlock(uint32_t size_in_bytes, uint32_t alignment)
    {
        return readback_mem_allocator_.Allocate(size_in_bytes, alignment);
    }

    void GpuSystem::DeallocReadbackMemBlock(GpuMemoryBlock&& mem_block)
    {
        return readback_mem_allocator_.Deallocate(std::move(mem_block), fence_vals_[frame_index_]);
    }

    void GpuSystem::ReallocReadbackMemBlock(GpuMemoryBlock& mem_block, uint32_t size_in_bytes, uint32_t alignment)
    {
        return readback_mem_allocator_.Reallocate(mem_block, fence_vals_[frame_index_], size_in_bytes, alignment);
    }

    void GpuSystem::WaitForGpu(uint64_t fence_value)
    {
        if (fence_ && (fence_event_.get() != INVALID_HANDLE_VALUE))
        {
            for (auto& cmd_queue : cmd_queues_)
            {
                if (cmd_queue.cmd_queue)
                {
                    const uint64_t wait_fence_value = (fence_value == MaxFenceValue) ? fence_vals_[frame_index_] : fence_value;
                    if (SUCCEEDED(cmd_queue.cmd_queue->Signal(fence_.get(), wait_fence_value)))
                    {
                        if (fence_->GetCompletedValue() < wait_fence_value)
                        {
                            if (SUCCEEDED(fence_->SetEventOnCompletion(wait_fence_value, fence_event_.get())))
                            {
                                ::WaitForSingleObjectEx(fence_event_.get(), INFINITE, FALSE);

                                fence_vals_[frame_index_] = wait_fence_value + 1;
                                if (fence_value != MaxFenceValue)
                                {
                                    ++fence_value;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    void GpuSystem::HandleDeviceLost()
    {
        upload_mem_allocator_.Clear();
        readback_mem_allocator_.Clear();

        cbv_srv_uav_desc_allocator_.Clear();

        for (auto& cmd_queue : cmd_queues_)
        {
            cmd_queue.cmd_queue = nullptr;
            for (auto& cmd_allocator : cmd_queue.cmd_allocators)
            {
                cmd_allocator = nullptr;
            }
            cmd_queue.cmd_list_pool.clear();
        }

        fence_ = nullptr;
        device_ = nullptr;

        frame_index_ = 0;
    }

    ID3D12CommandAllocator* GpuSystem::CurrentCommandAllocator(GpuSystem::CmdQueueType type) const noexcept
    {
        return cmd_queues_[static_cast<uint32_t>(type)].cmd_allocators[frame_index_].get();
    }

    uint64_t GpuSystem::ExecuteOnly(GpuCommandList& cmd_list, uint64_t wait_fence_value)
    {
        cmd_list.Close();

        ID3D12CommandQueue* cmd_queue = cmd_queues_[static_cast<uint32_t>(cmd_list.Type())].cmd_queue.get();

        if (wait_fence_value != MaxFenceValue)
        {
            cmd_queue->Wait(fence_.get(), wait_fence_value);
        }

        ID3D12CommandList* cmd_lists[] = {cmd_list.NativeCommandListBase()};
        cmd_queue->ExecuteCommandLists(static_cast<uint32_t>(std::size(cmd_lists)), cmd_lists);

        const uint64_t curr_fence_value = fence_vals_[frame_index_];
        TIFHR(cmd_queue->Signal(fence_.get(), curr_fence_value));
        fence_vals_[frame_index_] = curr_fence_value + 1;

        return curr_fence_value;
    }
} // namespace MotionToGo
