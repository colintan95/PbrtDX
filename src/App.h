#pragma once

#include <d3d12.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

class App
{
public:
    App(HWND hwnd);

    void Render();

private:
    void CreateDevice();

    void CreateCmdQueue();

    void CreateSwapChain();

    void CreateCmdList();

    void CreatePipeline();

    void CreateAccelerationStructures();

    void CreateRaytracingBuffers();

    void CreateDescriptorHeap();

    void CreateShaderTables();

    void WaitForGpu();

    HWND m_hwnd;

    UINT m_windowWidth = 0;
    UINT m_windowHeight = 0;

    winrt::com_ptr<IDXGIFactory6> m_factory;

    winrt::com_ptr<ID3D12Device> m_device;
    winrt::com_ptr<ID3D12Device5> m_dxrDevice;

    winrt::com_ptr<ID3D12CommandQueue> m_cmdQueue;

    static constexpr int NUM_FRAMES = 2;

    winrt::com_ptr<IDXGISwapChain3> m_swapChain;

    D3D12_VIEWPORT m_viewport;
    D3D12_RECT m_scissorRect;

    winrt::com_ptr<ID3D12CommandAllocator> m_cmdAllocator;

    winrt::com_ptr<ID3D12GraphicsCommandList4> m_cmdList;

    winrt::com_ptr<ID3D12Fence> m_fence;
    uint64_t m_fenceValue = 0;

    HANDLE m_fenceEvent;

    struct Frame
    {
        winrt::com_ptr<ID3D12Resource> SwapChainBuffer;
        winrt::com_ptr<ID3D12CommandAllocator> CmdAllocator;

        uint64_t FenceWaitValue = 0;
    };

    Frame m_frames[NUM_FRAMES];

    int m_currentFrame = 0;

    winrt::com_ptr<ID3D12RootSignature> m_globalRootSig;

    winrt::com_ptr<ID3D12StateObject> m_pipelineState;

    winrt::com_ptr<ID3D12Resource> m_blas;
    winrt::com_ptr<ID3D12Resource> m_tlas;

    winrt::com_ptr<ID3D12Resource> m_film;

    winrt::com_ptr<ID3D12DescriptorHeap> m_descriptorHeap;

    D3D12_GPU_DESCRIPTOR_HANDLE m_filmUav;

    winrt::com_ptr<ID3D12Resource> m_rayGenShaderTable;
    winrt::com_ptr<ID3D12Resource> m_missShaderTable;
};
