#include "App.h"

#include "gen/Shader.h"
#include "ImageLoader.h"
#include "Mesh.h"

#include <d3dx12.h>
#include <glm/gtc/matrix_transform.hpp>

#include <vector>

using winrt::com_ptr;
using winrt::check_bool;
using winrt::check_hresult;

App::App(HWND hwnd) : m_hwnd(hwnd)
{
    CreateDevice();

    CreateCmdQueue();

    CreateSwapChain();

    CreateCmdList();

    CreatePipeline();

    LoadMeshData();

    CreateAccelerationStructures();

    CreateRaytracingBuffers();

    CreateDescriptors();

    CreateShaderTables();
}

void App::CreateDevice()
{
    com_ptr<ID3D12Debug1> debugController;
    check_hresult(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.put())));

    debugController->EnableDebugLayer();
    debugController->SetEnableGPUBasedValidation(true);

    check_hresult(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(m_factory.put())));

    static constexpr D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_1;

    com_ptr<IDXGIAdapter1> adapter;

    for (uint32_t adapterIdx = 0;
         m_factory->EnumAdapterByGpuPreference(adapterIdx, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                               IID_PPV_ARGS(adapter.put())) != DXGI_ERROR_NOT_FOUND;
        ++adapterIdx)
    {
        if (SUCCEEDED(D3D12CreateDevice(adapter.get(), featureLevel, _uuidof(ID3D12Device),
                                        nullptr)))
            break;
    }

    check_hresult(D3D12CreateDevice(adapter.get(), featureLevel, IID_PPV_ARGS(m_device.put())));

    m_device.as(m_dxrDevice);
}

void App::CreateCmdQueue()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    check_hresult(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(m_cmdQueue.put())));
}

void App::CreateSwapChain()
{
    RECT clientRect{};
    check_bool(GetClientRect(m_hwnd, &clientRect));

    m_windowWidth = clientRect.right;
    m_windowHeight = clientRect.bottom;

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.BufferCount = NUM_FRAMES;
    swapChainDesc.Width = m_windowWidth;
    swapChainDesc.Height = m_windowHeight;
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_BACK_BUFFER;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    com_ptr<IDXGISwapChain1> swapChain;
    check_hresult(m_factory->CreateSwapChainForHwnd(m_cmdQueue.get(), m_hwnd, &swapChainDesc,
                                                    nullptr, nullptr, swapChain.put()));
    swapChain.as(m_swapChain);

    for (int i = 0; i < _countof(m_frames); ++i)
    {
        check_hresult(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_frames[i].SwapChainBuffer)));
    }
}

void App::CreateCmdList()
{
    check_hresult(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                   IID_PPV_ARGS(m_cmdAllocator.put())));

    for (int i = 0; i < _countof(m_frames); ++i)
    {
        check_hresult(m_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_frames[i].CmdAllocator.put())));
    }

    check_hresult(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              m_cmdAllocator.get(), nullptr,
                                              IID_PPV_ARGS(m_cmdList.put())));
    m_cmdList->Close();

    check_hresult(m_device->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_NONE,
                                        IID_PPV_ARGS(m_fence.put())));
    ++m_fenceValue;

    m_fenceEvent = CreateEvent(nullptr, false, false, nullptr);
}

void App::CreatePipeline()
{
    D3D12_DESCRIPTOR_RANGE1 ranges[3] = {};

    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 3;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    ranges[2].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
    ranges[2].NumDescriptors = 1;
    ranges[2].BaseShaderRegister = 0;
    ranges[2].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER1 rootParams[6] = {};

    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[0].Descriptor.ShaderRegister = 0;

    rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[1].DescriptorTable.pDescriptorRanges = &ranges[0];

    rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[2].Descriptor.ShaderRegister = 1;

    rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
    rootParams[3].Descriptor.ShaderRegister = 2;

    rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[4].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[4].DescriptorTable.pDescriptorRanges = &ranges[1];

    rootParams[5].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rootParams[5].DescriptorTable.NumDescriptorRanges = 1;
    rootParams[5].DescriptorTable.pDescriptorRanges = &ranges[2];

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc{};
    rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;
    rootSigDesc.Desc_1_1.NumParameters = _countof(rootParams);
    rootSigDesc.Desc_1_1.pParameters = rootParams;

    com_ptr<ID3DBlob> signatureBlob;
    com_ptr<ID3DBlob> errorBlob;
    check_hresult(D3D12SerializeVersionedRootSignature(&rootSigDesc, signatureBlob.put(),
                                                       errorBlob.put()));
    check_hresult(m_device->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                signatureBlob->GetBufferSize(),
                                                IID_PPV_ARGS(m_globalRootSig.put())));

    std::vector<D3D12_STATE_SUBOBJECT> subObjs;

    D3D12_GLOBAL_ROOT_SIGNATURE globalRootSigSubObj{};
    globalRootSigSubObj.pGlobalRootSignature = m_globalRootSig.get();

    subObjs.push_back({D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE, &globalRootSigSubObj});

    std::vector<D3D12_EXPORT_DESC> dxilLibExports;
    dxilLibExports.push_back({L"RayGenShader", nullptr, D3D12_EXPORT_FLAG_NONE});
    dxilLibExports.push_back({L"ClosestHitShader", nullptr, D3D12_EXPORT_FLAG_NONE});
    dxilLibExports.push_back({L"MissShader", nullptr, D3D12_EXPORT_FLAG_NONE});

    D3D12_DXIL_LIBRARY_DESC dxilLibSubObj{};
    dxilLibSubObj.DXILLibrary.pShaderBytecode = g_shader;
    dxilLibSubObj.DXILLibrary.BytecodeLength = ARRAYSIZE(g_shader);
    dxilLibSubObj.NumExports = static_cast<UINT>(dxilLibExports.size());
    dxilLibSubObj.pExports = dxilLibExports.data();

    subObjs.push_back({D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY, &dxilLibSubObj});

    D3D12_HIT_GROUP_DESC hitGroupDesc{};
    hitGroupDesc.HitGroupExport = L"HitGroup";
    hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitGroupDesc.ClosestHitShaderImport = L"ClosestHitShader";

    subObjs.push_back({D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP, &hitGroupDesc});

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
    shaderConfig.MaxPayloadSizeInBytes = sizeof(float) * 4;
    shaderConfig.MaxAttributeSizeInBytes = sizeof(float) * 2;

    subObjs.push_back({D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG, &shaderConfig});

    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{};
    pipelineConfig.MaxTraceRecursionDepth = 1;

    subObjs.push_back({D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG , &pipelineConfig});

    D3D12_STATE_OBJECT_DESC pipelineStateDesc{};
    pipelineStateDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    pipelineStateDesc.NumSubobjects = static_cast<UINT>(subObjs.size());
    pipelineStateDesc.pSubobjects = subObjs.data();

    check_hresult(m_dxrDevice->CreateStateObject(&pipelineStateDesc,
                                                 IID_PPV_ARGS(&m_pipelineState)));
}

void App::LoadMeshData()
{
    Mesh mesh{};

    LoadMeshFromPlyFile("scenes/pbrt-book/geometry/mesh_00003.ply", &mesh);

    m_vertexCount = mesh.Positions.size();
    m_indexCount = mesh.Indices.size();

    {
        size_t dataSize = mesh.Positions.size() * sizeof(glm::vec3);

        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(dataSize);

        check_hresult(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                        &resourceDesc, D3D12_RESOURCE_STATE_COMMON,
                                                        nullptr, IID_PPV_ARGS(m_posBuffer.put())));

        UploadToBuffer(m_posBuffer.get(), reinterpret_cast<const uint8_t*>(mesh.Positions.data()),
                       dataSize);
    }

    {
        size_t dataSize = mesh.UVs.size() * sizeof(glm::vec2);

        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(dataSize);

        check_hresult(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                        &resourceDesc, D3D12_RESOURCE_STATE_COMMON,
                                                        nullptr, IID_PPV_ARGS(m_uvBuffer.put())));

        UploadToBuffer(m_uvBuffer.get(), reinterpret_cast<const uint8_t*>(mesh.UVs.data()),
                       dataSize);
    }

    {
        size_t dataSize = mesh.Indices.size() * sizeof(uint32_t);

        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(dataSize);

        check_hresult(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                        &resourceDesc, D3D12_RESOURCE_STATE_COMMON,
                                                        nullptr,
                                                        IID_PPV_ARGS(m_indexBuffer.put())));

        UploadToBuffer(m_indexBuffer.get(), reinterpret_cast<const uint8_t*>(mesh.Indices.data()),
                       dataSize);
    }

    glm::mat4 transform =
        glm::translate(glm::mat4(1.f), glm::vec3(0.f, 2.2f, 0.f)) *
        glm::rotate(glm::mat4(1.f), 1.35f, glm::vec3(0.403f, -0.755f, -0.517f)) *
        glm::scale(glm::mat4(1.f), glm::vec3(0.5f));

    // Taking the transpose converts the matrix from column-major to row-major. glm uses
    // column-major while the geometry desc requires row-major.
    transform = glm::transpose(transform);

    {
        size_t dataSize = sizeof(float) * 3 * 4;

        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(dataSize);

        check_hresult(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                        &resourceDesc, D3D12_RESOURCE_STATE_COMMON,
                                                        nullptr,
                                                        IID_PPV_ARGS(m_transformBuffer.put())));

        UploadToBuffer(m_transformBuffer.get(), reinterpret_cast<const uint8_t*>(&transform),
                       dataSize);
    }

    ImageLoader loader(m_device.get());

    m_texture = loader.LoadImage("scenes/pbrt-book/texture/book_pbrt.png");
}

void App::CreateAccelerationStructures()
{
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInputs{};
    tlasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    tlasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    tlasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    tlasInputs.NumDescs = 1;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlasPrebuildInfo{};
    m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&tlasInputs, &tlasPrebuildInfo);

    D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
    geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
    geometryDesc.Triangles.Transform3x4 = m_transformBuffer->GetGPUVirtualAddress();
    geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
    geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    geometryDesc.Triangles.IndexCount = static_cast<uint32_t>(m_indexCount);
    geometryDesc.Triangles.VertexCount = static_cast<uint32_t>(m_vertexCount);
    geometryDesc.Triangles.IndexBuffer = m_indexBuffer->GetGPUVirtualAddress();
    geometryDesc.Triangles.VertexBuffer.StartAddress = m_posBuffer->GetGPUVirtualAddress();
    geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(float) * 3;

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blasInputs{};
    blasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    blasInputs.NumDescs = 1;
    blasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blasInputs.pGeometryDescs = &geometryDesc;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blasPrebuildInfo{};
    m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&blasInputs, &blasPrebuildInfo);

    com_ptr<ID3D12Resource> scratchResource;

    {
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC resourceDesc =
            CD3DX12_RESOURCE_DESC::Buffer(std::max(blasPrebuildInfo.ResultDataMaxSizeInBytes,
                                                   tlasPrebuildInfo.ResultDataMaxSizeInBytes),
                                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        check_hresult(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                        &resourceDesc, D3D12_RESOURCE_STATE_COMMON,
                                                        nullptr,
                                                        IID_PPV_ARGS(scratchResource.put())));
    }

    {
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC resourceDesc =
            CD3DX12_RESOURCE_DESC::Buffer(blasPrebuildInfo.ResultDataMaxSizeInBytes,
                                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        check_hresult(m_device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr,
            IID_PPV_ARGS(m_blas.put())));
    }

    {
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC resourceDesc =
            CD3DX12_RESOURCE_DESC::Buffer(tlasPrebuildInfo.ResultDataMaxSizeInBytes,
                                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        check_hresult(m_device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE, nullptr,
            IID_PPV_ARGS(m_tlas.put())));
    }

    D3D12_RAYTRACING_INSTANCE_DESC instanceDesc{};
    instanceDesc.Transform[0][0] = 1;
    instanceDesc.Transform[1][1] = 1;
    instanceDesc.Transform[2][2] = 1;
    instanceDesc.InstanceMask = 1;
    instanceDesc.AccelerationStructure = m_blas->GetGPUVirtualAddress();

    com_ptr<ID3D12Resource> instanceDescBuffer;

    {
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(instanceDesc));

        check_hresult(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                        &resourceDesc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                        IID_PPV_ARGS(instanceDescBuffer.put())));

        D3D12_RAYTRACING_INSTANCE_DESC* ptr = nullptr;
        instanceDescBuffer->Map(0, nullptr, reinterpret_cast<void**>(&ptr));

        *ptr = instanceDesc;

        instanceDescBuffer->Unmap(0, nullptr);
    }

    tlasInputs.InstanceDescs = instanceDescBuffer->GetGPUVirtualAddress();

    check_hresult(m_cmdAllocator->Reset());
    check_hresult(m_cmdList->Reset(m_cmdAllocator.get(), nullptr));

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blasDesc{};
    blasDesc.DestAccelerationStructureData = m_blas->GetGPUVirtualAddress();
    blasDesc.Inputs = blasInputs;
    blasDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();

    m_cmdList->BuildRaytracingAccelerationStructure(&blasDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.UAV.pResource = m_blas.get();

    m_cmdList->ResourceBarrier(1, &barrier);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlasDesc{};
    tlasDesc.DestAccelerationStructureData = m_tlas->GetGPUVirtualAddress();
    tlasDesc.Inputs = tlasInputs;
    tlasDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();

    m_cmdList->BuildRaytracingAccelerationStructure(&tlasDesc, 0, nullptr);

    m_cmdList->Close();

    ID3D12CommandList* cmdLists[] = {m_cmdList.get()};
    m_cmdQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

    WaitForGpu();
}

void App::CreateRaytracingBuffers()
{
    CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
    CD3DX12_RESOURCE_DESC resourceDesc =
        CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, m_windowWidth, m_windowHeight, 1,
                                     1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    check_hresult(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                    &resourceDesc,
                                                    D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                                    IID_PPV_ARGS(m_film.put())));
}

void App::CreateDescriptors()
{
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = 2;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        check_hresult(m_device->CreateDescriptorHeap(&heapDesc,
                                                     IID_PPV_ARGS(m_descriptorHeap.put())));
    }

    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle = m_descriptorHeap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = m_descriptorHeap->GetGPUDescriptorHandleForHeapStart();

    uint32_t descriptorSize = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    m_device->CreateUnorderedAccessView(m_film.get(), nullptr, &uavDesc, cpuHandle);
    m_filmUav = gpuHandle;

    cpuHandle.ptr += descriptorSize;
    gpuHandle.ptr += descriptorSize;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = 1;

    m_device->CreateShaderResourceView(m_texture.get(), &srvDesc, cpuHandle);
    m_textureSrv = gpuHandle;

    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = 1;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        check_hresult(m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(m_samplerHeap.put())));
    }

    D3D12_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.MinLOD = 0.f;
    samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
    samplerDesc.MipLODBias = 0.0f;
    samplerDesc.MaxAnisotropy = 1;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    samplerDesc.BorderColor[0] = 0;
    samplerDesc.BorderColor[1] = 0;
    samplerDesc.BorderColor[2] = 0;
    samplerDesc.BorderColor[3] = 0;

    m_device->CreateSampler(&samplerDesc, m_samplerHeap->GetCPUDescriptorHandleForHeapStart());
    m_sampler = m_samplerHeap->GetGPUDescriptorHandleForHeapStart();
}

static size_t Align(size_t size, size_t alignment) {
    return (size + (alignment - 1)) & ~(alignment - 1);
}

void App::CreateShaderTables()
{
    com_ptr<ID3D12StateObjectProperties> stateObjProps;
    m_pipelineState.as(stateObjProps);

    constexpr uint32_t shaderIdSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

    {
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC resourceDesc =
            CD3DX12_RESOURCE_DESC::Buffer(Align(shaderIdSize,
                                                D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));

        check_hresult(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                        &resourceDesc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                        IID_PPV_ARGS(m_rayGenShaderTable.put())));

        void* shaderId = stateObjProps->GetShaderIdentifier(L"RayGenShader");

        uint8_t* ptr = nullptr;
        check_hresult(m_rayGenShaderTable->Map(0, nullptr, reinterpret_cast<void**>(&ptr)));

        memcpy(ptr, shaderId, shaderIdSize);

        m_rayGenShaderTable->Unmap(0, nullptr);
    }

    {
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC resourceDesc =
            CD3DX12_RESOURCE_DESC::Buffer(Align(shaderIdSize,
                                                D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));

        check_hresult(m_device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &resourceDesc, D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(m_hitGroupShaderTable.put())));

        void* shaderId = stateObjProps->GetShaderIdentifier(L"HitGroup");

        uint8_t* ptr = nullptr;
        check_hresult(m_hitGroupShaderTable->Map(0, nullptr, reinterpret_cast<void**>(&ptr)));

        memcpy(ptr, shaderId, shaderIdSize);

        m_hitGroupShaderTable->Unmap(0, nullptr);
    }

    {
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC resourceDesc =
            CD3DX12_RESOURCE_DESC::Buffer(Align(shaderIdSize,
                                                D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT));

        check_hresult(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                        &resourceDesc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                        IID_PPV_ARGS(m_missShaderTable.put())));

        void* shaderId = stateObjProps->GetShaderIdentifier(L"MissShader");

        uint8_t* ptr = nullptr;
        check_hresult(m_missShaderTable->Map(0, nullptr, reinterpret_cast<void**>(&ptr)));

        memcpy(ptr, shaderId, shaderIdSize);

        m_missShaderTable->Unmap(0, nullptr);
    }
}

void App::Render()
{
    check_hresult(m_cmdAllocator->Reset());
    check_hresult(m_cmdList->Reset(m_cmdAllocator.get(), nullptr));

    m_cmdList->SetComputeRootSignature(m_globalRootSig.get());

    ID3D12DescriptorHeap* descriptorHeaps[] = {m_descriptorHeap.get(), m_samplerHeap.get()};
    m_cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    m_cmdList->SetComputeRootShaderResourceView(0, m_tlas->GetGPUVirtualAddress());

    m_cmdList->SetComputeRootDescriptorTable(1, m_filmUav);

    m_cmdList->SetComputeRootShaderResourceView(2, m_indexBuffer->GetGPUVirtualAddress());
    m_cmdList->SetComputeRootShaderResourceView(3, m_uvBuffer->GetGPUVirtualAddress());

    m_cmdList->SetComputeRootDescriptorTable(4, m_textureSrv);

    m_cmdList->SetComputeRootDescriptorTable(5, m_sampler);

    D3D12_DISPATCH_RAYS_DESC dispatchDesc{};

    dispatchDesc.RayGenerationShaderRecord.StartAddress =
        m_rayGenShaderTable->GetGPUVirtualAddress();
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes = m_rayGenShaderTable->GetDesc().Width;

    dispatchDesc.HitGroupTable.StartAddress = m_hitGroupShaderTable->GetGPUVirtualAddress();
    dispatchDesc.HitGroupTable.SizeInBytes = m_hitGroupShaderTable->GetDesc().Width;

    dispatchDesc.MissShaderTable.StartAddress = m_missShaderTable->GetGPUVirtualAddress();
    dispatchDesc.MissShaderTable.SizeInBytes = m_missShaderTable->GetDesc().Width;

    dispatchDesc.Width = m_windowWidth;
    dispatchDesc.Height = m_windowHeight;
    dispatchDesc.Depth = 1;

    m_cmdList->SetPipelineState1(m_pipelineState.get());
    m_cmdList->DispatchRays(&dispatchDesc);

    {
        D3D12_RESOURCE_BARRIER barriers[2] = {};

        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = m_frames[m_currentFrame].SwapChainBuffer.get();
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource = m_film.get();
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

        m_cmdList->ResourceBarrier(_countof(barriers), barriers);
    }

    m_cmdList->CopyResource(m_frames[m_currentFrame].SwapChainBuffer.get(), m_film.get());

    {
        D3D12_RESOURCE_BARRIER barriers[2] = {};

        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = m_frames[m_currentFrame].SwapChainBuffer.get();
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;

        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource = m_film.get();
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        m_cmdList->ResourceBarrier(_countof(barriers), barriers);
    }

    check_hresult(m_cmdList->Close());

    ID3D12CommandList* cmdLists[] = {m_cmdList.get()};
    m_cmdQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

    check_hresult(m_swapChain->Present(1, 0));

    check_hresult(m_cmdQueue->Signal(m_fence.get(), m_fenceValue));

    m_frames[m_currentFrame].FenceWaitValue = m_fenceValue;
    ++m_fenceValue;

    m_currentFrame = m_swapChain->GetCurrentBackBufferIndex();

    if (m_fence->GetCompletedValue() < m_frames[m_currentFrame].FenceWaitValue)
    {
        check_hresult(m_fence->SetEventOnCompletion(m_frames[m_currentFrame].FenceWaitValue,
                                                    m_fenceEvent));

        WaitForSingleObjectEx(m_fenceEvent, INFINITE, false);
    }
}

void App::UploadToBuffer(ID3D12Resource* buffer, const uint8_t* data, size_t size)
{
    winrt::com_ptr<ID3D12Resource> uploadBuffer;

    {
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC resourceDesc = CD3DX12_RESOURCE_DESC::Buffer(size);

        check_hresult(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                        &resourceDesc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ,
                                                        nullptr,
                                                        IID_PPV_ARGS(uploadBuffer.put())));
    }

    uint8_t* ptr = nullptr;
    check_hresult(uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&ptr)));

    memcpy(ptr, data, size);

    uploadBuffer->Unmap(0, nullptr);

    check_hresult(m_cmdAllocator->Reset());
    check_hresult(m_cmdList->Reset(m_cmdAllocator.get(), nullptr));

    m_cmdList->CopyBufferRegion(buffer, 0, uploadBuffer.get(), 0, size);

    check_hresult(m_cmdList->Close());

    ID3D12CommandList* cmdLists[] = {m_cmdList.get()};
    m_cmdQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

    WaitForGpu();
}

void App::WaitForGpu()
{
    uint64_t waitValue = m_fenceValue;
    ++m_fenceValue;

    m_cmdQueue->Signal(m_fence.get(), waitValue);

    check_hresult(m_fence->SetEventOnCompletion(waitValue, m_fenceEvent));

    WaitForSingleObjectEx(m_fenceEvent, INFINITE, false);
}
