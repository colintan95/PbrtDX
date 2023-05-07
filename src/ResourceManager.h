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
    winrt::com_ptr<ID3D12Resource> CreateBufferAndUpload(std::span<T> data)
    {
        auto dataInBytes = std::as_bytes(data);

        winrt::com_ptr<ID3D12Resource> resource = CreateBuffer(dataInBytes.size());
        UploadToBuffer(resource.get(), 0, dataInBytes);

        return resource;
    }

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
            return m_ptr;
        }

        UploadIterator& operator++()
        {
            m_currentOffset += m_stride;
            m_ptr = reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(m_ptr) + m_stride);

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
            winrt::check_hresult(m_buffer->Map(0, nullptr, reinterpret_cast<void**>(&m_ptr)));
        }

        T* m_ptr = nullptr;

        ID3D12Resource* m_buffer = nullptr;
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

class DescriptorHeap
{
public:
    DescriptorHeap(const D3D12_DESCRIPTOR_HEAP_DESC& heapDesc, ID3D12Device* device);

    struct Handles
    {
        D3D12_CPU_DESCRIPTOR_HANDLE CpuHandle;
        D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle;
    };

    Handles Allocate();

    ID3D12DescriptorHeap* Inner();

private:
    uint32_t m_descriptorSize = 0;

    size_t m_totalSize = 0;

    winrt::com_ptr<ID3D12DescriptorHeap> m_descriptorHeap;

    Handles m_currentHandles;
    size_t m_currentSize = 0;
};
