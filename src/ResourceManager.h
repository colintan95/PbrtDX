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
    class CpuIterator
    {
    public:
        ~CpuIterator()
        {
            m_buffer->Unmap(0, nullptr);
        }

        T* operator->()
        {
            return reinterpret_cast<T*>(m_ptr);
        }

        CpuIterator& operator++()
        {
            m_ptr += m_stride;
            return *this;
        }

    private:
        friend class ResourceManager;

        CpuIterator(ID3D12Resource* buffer, size_t stride)
            : m_buffer(buffer), m_stride(stride)
        {
            winrt::check_hresult(m_buffer->Map(0, nullptr, reinterpret_cast<void**>(&m_ptr)));
        }

        ID3D12Resource* m_buffer = nullptr;
        uint8_t* m_ptr = nullptr;

        size_t m_stride = 0;
    };

    template<typename T>
    CpuIterator<T> GetCpuIterator(ID3D12Resource* uploadBuffer, size_t stride = sizeof(T))
    {
        return CpuIterator<T>(uploadBuffer, stride);
    }

    template<typename T>
    class Allocator
    {
    public:
        ~Allocator()
        {
            m_uploadBuffer->Unmap(0, nullptr);
        }

        struct Entry
        {
            D3D12_GPU_VIRTUAL_ADDRESS GpuAddress;
            T* Data;
        };

        Entry Allocate()
        {
            Entry entry{};
            entry.GpuAddress = m_baseAddr + m_currentOffset;
            entry.Data = reinterpret_cast<T*>(m_baseUploadPtr + m_currentOffset);

            m_currentOffset += m_stride;

            return entry;
        }

        void Upload()
        {
            m_manager->UploadToBuffer(m_gpuBuffer, 0, m_uploadBuffer.get(), m_currentOffset);
        }

    private:
        friend class ResourceManager;

        Allocator(ResourceManager* manager, ID3D12Resource* gpuBuffer,
                  winrt::com_ptr<ID3D12Resource> uploadBuffer, size_t stride)
            : m_manager(manager), m_gpuBuffer(gpuBuffer), m_uploadBuffer(uploadBuffer),
              m_baseAddr(gpuBuffer->GetGPUVirtualAddress()), m_stride(stride)
        {
            winrt::check_hresult(m_uploadBuffer->Map(0, nullptr,
                                                     reinterpret_cast<void**>(&m_baseUploadPtr)));
        }

        ResourceManager* m_manager = nullptr;

        ID3D12Resource* m_gpuBuffer = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS m_baseAddr;

        winrt::com_ptr<ID3D12Resource> m_uploadBuffer;
        uint8_t* m_baseUploadPtr = nullptr;

        size_t m_stride = 0;

        size_t m_currentOffset = 0;
    };

    template<typename T>
    Allocator<T> GetAllocator(ID3D12Resource* buffer, size_t numElements,size_t stride = sizeof(T))
    {
        size_t size = numElements * stride;

        winrt::com_ptr<ID3D12Resource> uploadBuffer = CreateUploadBuffer(size);

        return Allocator<T>(this, buffer, uploadBuffer, stride);
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
