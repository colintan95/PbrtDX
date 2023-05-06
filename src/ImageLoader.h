#pragma once

#include <d3d12.h>
#include <wincodec.h>
#include <winrt/base.h>

#include <filesystem>

class ImageLoader
{
public:
    ImageLoader(ID3D12Device* device);

    winrt::com_ptr<ID3D12Resource> LoadImage(std::filesystem::path path);

private:
    ID3D12Device* m_device;

    winrt::com_ptr<ID3D12CommandQueue> m_copyQueue;
    winrt::com_ptr<ID3D12CommandAllocator> m_cmdAllocator;
    winrt::com_ptr<ID3D12GraphicsCommandList> m_cmdList;

    winrt::com_ptr<ID3D12Fence> m_fence;
    uint64_t m_fenceValue = 0;

    winrt::com_ptr<IWICImagingFactory> m_wicFactory;
};
