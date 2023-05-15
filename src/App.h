#pragma once

#include "ResourceManager.h"

#include "shaders/Common.h"

#include <d3d12.h>
#include <dxgi1_6.h>
#include <winrt/base.h>

#include <optional>
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

    void LoadGeometry(std::filesystem::path path, std::optional<std::filesystem::path> texture,
                      Geometry* geometry);

    void CreateAccelerationStructures();

    void CreateOtherResources();

    void CreateDescriptors();

    void CreateShaderTables();

    void WaitForGpu();

    std::unique_ptr<ResourceManager> m_resourceManager;

    HWND m_hwnd;

    UINT m_windowWidth = 0;
    UINT m_windowHeight = 0;

    winrt::com_ptr<IDXGIFactory6> m_factory;

    winrt::com_ptr<ID3D12Device5> m_device;

    winrt::com_ptr<ID3D12CommandQueue> m_cmdQueue;

    winrt::com_ptr<IDXGISwapChain3> m_swapChain;

    winrt::com_ptr<ID3D12CommandAllocator> m_cmdAllocator;
    winrt::com_ptr<ID3D12CommandAllocator> m_cmdAllocator2;

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

    static constexpr int NUM_FRAMES = 2;

    Frame m_frames[NUM_FRAMES];
    int m_currentFrame = 0;

    winrt::com_ptr<ID3D12RootSignature> m_globalRootSig;
    winrt::com_ptr<ID3D12RootSignature> m_hitGroupLocalSig;

    winrt::com_ptr<ID3D12StateObject> m_pipeline;

    winrt::com_ptr<ID3D12RootSignature> m_testRootSig;
    winrt::com_ptr<ID3D12StateObject> m_testPipeline;

    uint32_t m_sampleIdx = 0;

    struct Geometry
    {
        winrt::com_ptr<ID3D12Resource> Positions;
        winrt::com_ptr<ID3D12Resource> Normals;
        winrt::com_ptr<ID3D12Resource> UVs;

        winrt::com_ptr<ID3D12Resource> Indices;

        uint32_t VertexCount = 0;
        uint32_t IndexCount = 0;

        D3D12_GPU_VIRTUAL_ADDRESS Transform;

        winrt::com_ptr<ID3D12Resource> Texture;
        D3D12_GPU_DESCRIPTOR_HANDLE TextureSrv;
    };

    std::vector<Geometry> m_geometries;

    winrt::com_ptr<ID3D12Resource> m_transformBuffer;
    winrt::com_ptr<ID3D12Resource> m_hitGroupGeomConstantsBuffer;

    winrt::com_ptr<ID3D12Resource> m_aabbBuffer;

    winrt::com_ptr<ID3D12Resource> m_lightBuffer;

    winrt::com_ptr<ID3D12Resource> m_blas;
    winrt::com_ptr<ID3D12Resource> m_lightBlas;

    winrt::com_ptr<ID3D12Resource> m_tlas;

    winrt::com_ptr<ID3D12Resource> m_film;
    winrt::com_ptr<ID3D12Resource> m_haltonEntries;
    winrt::com_ptr<ID3D12Resource> m_haltonPerms;

    winrt::com_ptr<ID3D12Resource> m_testFilm;

    winrt::com_ptr<ID3D12Resource> m_hitGroupShaderConstantsBuffer;
    HitGroupShaderConstants* m_hitGroupShaderConstants = nullptr;

    std::unique_ptr<DescriptorHeap> m_descriptorHeap;

    D3D12_GPU_DESCRIPTOR_HANDLE m_filmUav;
    D3D12_GPU_DESCRIPTOR_HANDLE m_testFilmUav;

    std::unique_ptr<DescriptorHeap> m_samplerHeap;

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

    ShaderTable m_testRayGenShaderTable;
    ShaderTable m_testHitGroupShaderTable;
    ShaderTable m_testMissShaderTable;

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
                DrawConstants,
                Sampler,
                Lights,
                HaltonEntries,
                HaltonPerms,
                NUM_PARAMS
            };
        };
    };

    struct HitGroup
    {
        enum Param
        {
            ShaderConstants = 0,
            Indices,
            Normals,
            UVs,
            GeometryConstants,
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
            VisibilityHitGroup,
            LightHitGroup,
            ShaderConfig,
            PipelineConfig,
            NUM_OBJS
        };
    };
};
