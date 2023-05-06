#pragma once

#include <d3d12.h>
#include <wincodec.h>
#include <winrt/base.h>

#include <filesystem>
#include <span>

class ResourceManager
{
public:
    ResourceManager(ID3D12Device* device);

    winrt::com_ptr<ID3D12Resource> CreateBuffer(
        size_t size,
        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE,
        D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON);

    winrt::com_ptr<ID3D12Resource> CreateUploadBuffer(size_t size);

    winrt::com_ptr<ID3D12Resource> LoadImage(std::filesystem::path path);

    void UploadToBuffer(ID3D12Resource* dstResource, size_t dstOffset,
                        std::span<const std::byte> srcData);

    void UploadToBuffer(ID3D12Resource* dstResource, size_t dstOffset, ID3D12Resource* srcResource,
                        size_t srcSize);

    template<typename T>
    class UploadIterator
    {
    public:
        ~UploadIterator()
        {
            m_buffer->Unmap(0, nullptr);
        }

        T* operator->()
        {
            return reinterpret_cast<T*>(m_basePtr + m_currentOffset);
        }

        UploadIterator& operator++()
        {
            m_currentOffset += m_stride;
            return *this;
        }

        D3D12_GPU_VIRTUAL_ADDRESS GetGpuAddress()
        {
            return m_buffer->GetGPUVirtualAddress() + m_currentOffset;
        }

    private:
        friend class ResourceManager;

        UploadIterator(ID3D12Resource* buffer, size_t stride)
            : m_buffer(buffer), m_stride(stride)
        {
            winrt::check_hresult(m_buffer->Map(0, nullptr, reinterpret_cast<void**>(&m_basePtr)));
        }

        ID3D12Resource* m_buffer = nullptr;
        uint8_t* m_basePtr = nullptr;

        size_t m_stride = 0;

        size_t m_currentOffset = 0;
    };

    template<typename T>
    UploadIterator<T> GetUploadIterator(ID3D12Resource* uploadBuffer, size_t stride = sizeof(T))
    {
        return UploadIterator<T>(uploadBuffer, stride);
    }

private:
    void WaitForGpu();

    ID3D12Device* m_device;

    winrt::com_ptr<ID3D12CommandQueue> m_copyQueue;
    winrt::com_ptr<ID3D12CommandAllocator> m_cmdAllocator;
    winrt::com_ptr<ID3D12GraphicsCommandList> m_cmdList;

    winrt::com_ptr<ID3D12Fence> m_fence;
    uint64_t m_fenceValue = 0;

    HANDLE m_fenceEvent;

    winrt::com_ptr<IWICImagingFactory> m_wicFactory;
};
