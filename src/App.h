#pragma once

#include "ResourceManager.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

#include <vector>

class App
{
public:
    App(HWND hwnd);

    void Render();

private:
    // Forward declaration.
    struct Geometry;

    void CreateDevice();

    void CreateCmdQueue();

    void CreateSwapChain();

    void CreateCmdList();

    void CreatePipeline();

    void LoadScene();

    void LoadGeometry(std::filesystem::path path, std::filesystem::path texture,
                      Geometry* geometry);

    void CreateAccelerationStructures();

    void CreateSharedResources();

    void CreateDescriptors();

    void CreateShaderTables();

    void UploadToBuffer(ID3D12Resource* buffer, const uint8_t* data, size_t size);

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

    winrt::com_ptr<ID3D12CommandAllocator> m_cmdAllocator;

    winrt::com_ptr<ID3D12GraphicsCommandList4> m_cmdList;

    winrt::com_ptr<ID3D12Fence> m_fence;
    uint64_t m_fenceValue = 0;

    HANDLE m_fenceEvent;

    std::unique_ptr<ResourceManager> m_resourceManager;

    struct Frame
    {
        winrt::com_ptr<ID3D12Resource> SwapChainBuffer;
        winrt::com_ptr<ID3D12CommandAllocator> CmdAllocator;

        uint64_t FenceWaitValue = 0;
    };

    Frame m_frames[NUM_FRAMES];

    int m_currentFrame = 0;

    struct Global
    {
        struct Range
        {
            enum
            {
                Film = 0,
                Sampler,
                NUM_RANGES
            };
        };

        struct Param
        {
            enum
            {
                Scene = 0,
                Film,
                Sampler,
                NUM_PARAMS
            };
        };
    };

    struct HitGroup
    {
        enum Param
        {
            IndexBuffer = 0,
            UVBuffer,
            Texture,
            NUM_PARAMS
        };
    };

    struct SubObj
    {
        enum
        {
            GlobalRootSig = 0,
            DxilLib,
            HitGroupRootSig,
            HitGroupRootSigAssoc,
            HitGroup,
            ShaderConfig,
            PipelineConfig,
            NUM_OBJS
        };
    };

    winrt::com_ptr<ID3D12RootSignature> m_globalRootSig;
    winrt::com_ptr<ID3D12RootSignature> m_hitGroupLocalSig;

    winrt::com_ptr<ID3D12StateObject> m_pipeline;

    winrt::com_ptr<ID3D12Resource> m_transformBuffer;

    struct Geometry
    {
        winrt::com_ptr<ID3D12Resource> Positions;
        winrt::com_ptr<ID3D12Resource> UVs;

        winrt::com_ptr<ID3D12Resource> Indices;

        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;

        D3D12_GPU_VIRTUAL_ADDRESS Transform;

        winrt::com_ptr<ID3D12Resource> Texture;
        D3D12_GPU_DESCRIPTOR_HANDLE TextureSrv;
    };

    std::vector<Geometry> m_geometries;

    winrt::com_ptr<ID3D12Resource> m_blas;
    winrt::com_ptr<ID3D12Resource> m_tlas;

    winrt::com_ptr<ID3D12Resource> m_film;

    winrt::com_ptr<ID3D12DescriptorHeap> m_descriptorHeap;

    D3D12_GPU_DESCRIPTOR_HANDLE m_filmUav;

    winrt::com_ptr<ID3D12DescriptorHeap> m_samplerHeap;

    D3D12_GPU_DESCRIPTOR_HANDLE m_sampler;

    struct ShaderTable
    {
        winrt::com_ptr<ID3D12Resource> Buffer;
        size_t Size = 0;
        size_t Stride = 0;
    };

    ShaderTable m_rayGenShaderTable;
    ShaderTable m_hitGroupShaderTable;
    ShaderTable m_missShaderTable;
};
