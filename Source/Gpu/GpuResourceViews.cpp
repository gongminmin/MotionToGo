#include "GpuResourceViews.hpp"

#include "GpuSystem.hpp"
#include "GpuTexture2D.hpp"

namespace MotionToGo
{
    GpuShaderResourceView::GpuShaderResourceView() noexcept = default;

    GpuShaderResourceView::GpuShaderResourceView(GpuSystem& gpu_system, const GpuTexture2D& texture, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
        : GpuShaderResourceView(gpu_system, texture, DXGI_FORMAT_UNKNOWN, cpu_handle)
    {
    }

    GpuShaderResourceView::GpuShaderResourceView(
        GpuSystem& gpu_system, const GpuTexture2D& texture, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
        : cpu_handle_(cpu_handle)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = (format == DXGI_FORMAT_UNKNOWN) ? texture.Format() : format;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Texture2D.MostDetailedMip = 0;
        srv_desc.Texture2D.MipLevels = texture.MipLevels();
        srv_desc.Texture2D.PlaneSlice = 0;
        srv_desc.Texture2D.ResourceMinLODClamp = 0;
        gpu_system.NativeDevice()->CreateShaderResourceView(texture.NativeTexture(), &srv_desc, cpu_handle);
    }

    GpuShaderResourceView::GpuShaderResourceView(
        GpuSystem& gpu_system, const GpuTexture2D& texture, uint32_t sub_resource, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
        : GpuShaderResourceView(gpu_system, texture, sub_resource, DXGI_FORMAT_UNKNOWN, cpu_handle)
    {
    }

    GpuShaderResourceView::GpuShaderResourceView(GpuSystem& gpu_system, const GpuTexture2D& texture, uint32_t sub_resource,
        DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
        : cpu_handle_(cpu_handle)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc{};
        srv_desc.Format = (format == DXGI_FORMAT_UNKNOWN) ? texture.Format() : format;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        SubResourceToMipLevelPlane(sub_resource, texture.MipLevels(), srv_desc.Texture2D.MostDetailedMip, srv_desc.Texture2D.PlaneSlice);
        srv_desc.Texture2D.MipLevels = 1;
        srv_desc.Texture2D.ResourceMinLODClamp = 0;
        gpu_system.NativeDevice()->CreateShaderResourceView(texture.NativeTexture(), &srv_desc, cpu_handle);
    }

    GpuShaderResourceView::~GpuShaderResourceView() noexcept = default;
    GpuShaderResourceView::GpuShaderResourceView(GpuShaderResourceView&& other) noexcept = default;
    GpuShaderResourceView& GpuShaderResourceView::operator=(GpuShaderResourceView&& other) noexcept = default;

    GpuShaderResourceView::operator bool() const noexcept
    {
        return (cpu_handle_.ptr != 0);
    }

    void GpuShaderResourceView::Reset() noexcept
    {
        cpu_handle_ = {};
    }


    GpuUnorderedAccessView::GpuUnorderedAccessView() noexcept = default;

    GpuUnorderedAccessView::GpuUnorderedAccessView(
        GpuSystem& gpu_system, const GpuTexture2D& texture, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
        : GpuUnorderedAccessView(gpu_system, texture, DXGI_FORMAT_UNKNOWN, cpu_handle)
    {
    }

    GpuUnorderedAccessView::GpuUnorderedAccessView(
        GpuSystem& gpu_system, const GpuTexture2D& texture, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
        : GpuUnorderedAccessView(gpu_system, texture, 0, format, cpu_handle)
    {
    }

    GpuUnorderedAccessView::GpuUnorderedAccessView(
        GpuSystem& gpu_system, const GpuTexture2D& texture, uint32_t sub_resource, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
        : GpuUnorderedAccessView(gpu_system, texture, sub_resource, DXGI_FORMAT_UNKNOWN, cpu_handle)
    {
    }

    GpuUnorderedAccessView::GpuUnorderedAccessView(GpuSystem& gpu_system, const GpuTexture2D& texture, uint32_t sub_resource,
        DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
        : cpu_handle_(cpu_handle)
    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc{};
        uav_desc.Format = format == DXGI_FORMAT_UNKNOWN ? texture.Format() : format;
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        SubResourceToMipLevelPlane(sub_resource, texture.MipLevels(), uav_desc.Texture2D.MipSlice, uav_desc.Texture2D.PlaneSlice);
        gpu_system.NativeDevice()->CreateUnorderedAccessView(texture.NativeTexture(), nullptr, &uav_desc, cpu_handle);
    }

    GpuUnorderedAccessView::~GpuUnorderedAccessView() noexcept = default;
    GpuUnorderedAccessView::GpuUnorderedAccessView(GpuUnorderedAccessView&& other) noexcept = default;
    GpuUnorderedAccessView& GpuUnorderedAccessView::operator=(GpuUnorderedAccessView&& other) noexcept = default;

    GpuUnorderedAccessView::operator bool() const noexcept
    {
        return (cpu_handle_.ptr != 0);
    }

    void GpuUnorderedAccessView::Reset() noexcept
    {
        cpu_handle_ = {};
    }
} // namespace MotionToGo
