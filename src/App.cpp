#include "App.h"

#include "gen/Shader.h"
#include "Mesh.h"

#include <d3dx12.h>
#include <glm/gtc/matrix_transform.hpp>

#include <span>
#include <vector>

using winrt::com_ptr;
using winrt::check_bool;
using winrt::check_hresult;

App::App(HWND hwnd) : m_hwnd(hwnd)
{
    CreateDevice();

    CreateCmdQueue();

    m_resourceManager = std::make_unique<ResourceManager>(m_device.get());

    CreateSwapChain();

    CreateCmdList();

    CreatePipeline();

    LoadScene();

    CreateAccelerationStructures();

    CreateSharedResources();

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
    {
        D3D12_DESCRIPTOR_RANGE1 ranges[Global::Range::NUM_RANGES] = {};

        ranges[Global::Range::Film].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        ranges[Global::Range::Film].NumDescriptors = 1;
        ranges[Global::Range::Film].BaseShaderRegister = 0;

        ranges[Global::Range::Sampler].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
        ranges[Global::Range::Sampler].NumDescriptors = 1;
        ranges[Global::Range::Sampler].BaseShaderRegister = 0;

        D3D12_ROOT_PARAMETER1 params[Global::Param::NUM_PARAMS] = {};

        params[Global::Param::Scene].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[Global::Param::Scene].Descriptor.ShaderRegister = 0;

        params[Global::Param::Film].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[Global::Param::Film].DescriptorTable.NumDescriptorRanges = 1;
        params[Global::Param::Film].DescriptorTable.pDescriptorRanges =
            &ranges[Global::Range::Film];

        params[Global::Param::Sampler].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[Global::Param::Sampler].DescriptorTable.NumDescriptorRanges = 1;
        params[Global::Param::Sampler].DescriptorTable.pDescriptorRanges =
            &ranges[Global::Range::Sampler];

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc{};
        rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootSigDesc.Desc_1_1.NumParameters = _countof(params);
        rootSigDesc.Desc_1_1.pParameters = params;
        rootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        com_ptr<ID3DBlob> signatureBlob;
        com_ptr<ID3DBlob> errorBlob;
        check_hresult(D3D12SerializeVersionedRootSignature(&rootSigDesc, signatureBlob.put(),
                                                           errorBlob.put()));
        check_hresult(m_device->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                    signatureBlob->GetBufferSize(),
                                                    IID_PPV_ARGS(m_globalRootSig.put())));
    }

    {
        D3D12_DESCRIPTOR_RANGE1 range{};
        range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        range.NumDescriptors = 1;
        range.BaseShaderRegister = 2;
        range.RegisterSpace = 1;

        D3D12_ROOT_PARAMETER1 params[HitGroup::Param::NUM_PARAMS] = {};

        params[HitGroup::Param::IndexBuffer].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[HitGroup::Param::IndexBuffer].Descriptor.ShaderRegister = 0;
        params[HitGroup::Param::IndexBuffer].Descriptor.RegisterSpace = 1;

        params[HitGroup::Param::UVBuffer].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[HitGroup::Param::UVBuffer].Descriptor.ShaderRegister = 1;
        params[HitGroup::Param::UVBuffer].Descriptor.RegisterSpace = 1;

        params[HitGroup::Param::Texture].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[HitGroup::Param::Texture].DescriptorTable.NumDescriptorRanges = 1;
        params[HitGroup::Param::Texture].DescriptorTable.pDescriptorRanges = &range;

        D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc{};
        rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
        rootSigDesc.Desc_1_1.NumParameters = _countof(params);
        rootSigDesc.Desc_1_1.pParameters = params;
        rootSigDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

        com_ptr<ID3DBlob> signatureBlob;
        com_ptr<ID3DBlob> errorBlob;
        check_hresult(D3D12SerializeVersionedRootSignature(&rootSigDesc, signatureBlob.put(),
                                                           errorBlob.put()));
        check_hresult(m_device->CreateRootSignature(0, signatureBlob->GetBufferPointer(),
                                                    signatureBlob->GetBufferSize(),
                                                    IID_PPV_ARGS(m_hitGroupLocalSig.put())));
    }

    D3D12_STATE_SUBOBJECT subObjs[SubObj::NUM_OBJS];

    D3D12_GLOBAL_ROOT_SIGNATURE globalRootSigSubObj{};
    globalRootSigSubObj.pGlobalRootSignature = m_globalRootSig.get();

    subObjs[SubObj::GlobalRootSig].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    subObjs[SubObj::GlobalRootSig].pDesc = &globalRootSigSubObj;

    std::vector<D3D12_EXPORT_DESC> dxilExports;
    dxilExports.push_back({L"RayGenShader", nullptr, D3D12_EXPORT_FLAG_NONE});
    dxilExports.push_back({L"ClosestHitShader", nullptr, D3D12_EXPORT_FLAG_NONE});
    dxilExports.push_back({L"MissShader", nullptr, D3D12_EXPORT_FLAG_NONE});

    D3D12_DXIL_LIBRARY_DESC dxilLibDesc{};
    dxilLibDesc.DXILLibrary.pShaderBytecode = g_shader;
    dxilLibDesc.DXILLibrary.BytecodeLength = ARRAYSIZE(g_shader);
    dxilLibDesc.NumExports = static_cast<UINT>(dxilExports.size());
    dxilLibDesc.pExports = dxilExports.data();

    subObjs[SubObj::DxilLib].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subObjs[SubObj::DxilLib].pDesc = &dxilLibDesc;

    D3D12_LOCAL_ROOT_SIGNATURE hitGroupRootSigSubObj{};
    hitGroupRootSigSubObj.pLocalRootSignature = m_hitGroupLocalSig.get();

    subObjs[SubObj::HitGroupRootSig].Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
    subObjs[SubObj::HitGroupRootSig].pDesc = &hitGroupRootSigSubObj;

    LPCWSTR hitGroupRootSigAssocExports[] = {L"ClosestHitShader"};

    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION hitGroupRootSigAssoc{};
    hitGroupRootSigAssoc.pSubobjectToAssociate = &subObjs[SubObj::HitGroupRootSig];
    hitGroupRootSigAssoc.NumExports = _countof(hitGroupRootSigAssocExports);
    hitGroupRootSigAssoc.pExports = hitGroupRootSigAssocExports;

    subObjs[SubObj::HitGroupRootSigAssoc].Type =
        D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
    subObjs[SubObj::HitGroupRootSigAssoc].pDesc = &hitGroupRootSigAssoc;

    D3D12_HIT_GROUP_DESC hitGroupDesc{};
    hitGroupDesc.HitGroupExport = L"HitGroup";
    hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitGroupDesc.ClosestHitShaderImport = L"ClosestHitShader";

    subObjs[SubObj::HitGroup].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subObjs[SubObj::HitGroup].pDesc = &hitGroupDesc;

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
    shaderConfig.MaxPayloadSizeInBytes = sizeof(float) * 4;
    shaderConfig.MaxAttributeSizeInBytes = sizeof(float) * 2;

    subObjs[SubObj::ShaderConfig].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    subObjs[SubObj::ShaderConfig].pDesc = &shaderConfig;

    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{};
    pipelineConfig.MaxTraceRecursionDepth = 1;

    subObjs[SubObj::PipelineConfig].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    subObjs[SubObj::PipelineConfig].pDesc = &pipelineConfig;

    D3D12_STATE_OBJECT_DESC pipelineDesc{};
    pipelineDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    pipelineDesc.NumSubobjects = _countof(subObjs);
    pipelineDesc.pSubobjects = subObjs;

    check_hresult(m_dxrDevice->CreateStateObject(&pipelineDesc, IID_PPV_ARGS(&m_pipeline)));
}

namespace
{

struct Mat3x4
{
    glm::vec4 Rows[3];
};

} // namespace

void App::LoadScene()
{
    m_geometries.resize(2);

    LoadGeometry("scenes/pbrt-book/geometry/mesh_00002.ply",
                 "scenes/pbrt-book/texture/book_pages.png", &m_geometries[0]);

    LoadGeometry("scenes/pbrt-book/geometry/mesh_00003.ply",
                 "scenes/pbrt-book/texture/book_pbrt.png", &m_geometries[1]);

    glm::mat4 transforms[2] = {};

    transforms[0] = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 2.2f, 0.f)) *
                    glm::rotate(glm::mat4(1.f), 1.35f, glm::vec3(0.403f, -0.755f, -0.517f)) *
                    glm::scale(glm::mat4(1.f), glm::vec3(0.5f));

    transforms[1] = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 2.2f, 0.f)) *
                    glm::rotate(glm::mat4(1.f), 1.35f, glm::vec3(0.403f, -0.755f, -0.517f)) *
                    glm::scale(glm::mat4(1.f), glm::vec3(0.5f));

    m_transformBuffer = m_resourceManager->CreateUploadBuffer(sizeof(Mat3x4) * m_geometries.size());

    auto it = m_resourceManager->GetUploadIterator<Mat3x4>(m_transformBuffer.get());

    for (size_t i = 0; i < m_geometries.size(); ++i)
    {
        glm::mat4 transform = glm::transpose(transforms[i]);

        it->Rows[0] = transform[0];
        it->Rows[1] = transform[1];
        it->Rows[2] = transform[2];

        m_geometries[i].Transform = it.GetGpuAddress();

        ++it;
    }
}

void App::LoadGeometry(std::filesystem::path path, std::filesystem::path texture,
                       Geometry* geometry)
{
    Mesh mesh{};
    LoadMeshFromPlyFile(path, &mesh);

    {
        auto data = std::as_bytes(std::span(mesh.Positions));

        geometry->Positions = m_resourceManager->CreateBuffer(data.size());

        m_resourceManager->UploadToBuffer(geometry->Positions.get(), 0, data);
    }

    {
        auto data = std::as_bytes(std::span(mesh.UVs));

        geometry->UVs = m_resourceManager->CreateBuffer(data.size());

        m_resourceManager->UploadToBuffer(geometry->UVs.get(), 0, data);
    }

    {
        auto data = std::as_bytes(std::span(mesh.Indices));

        geometry->Indices = m_resourceManager->CreateBuffer(data.size());

        m_resourceManager->UploadToBuffer(geometry->Indices.get(), 0, data);
    }

    geometry->VertexCount = static_cast<uint32_t>(mesh.Positions.size());
    geometry->IndexCount = static_cast<uint32_t>(mesh.Indices.size());

    geometry->Texture = m_resourceManager->LoadImage(texture);
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

    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geometryDescs;
    geometryDescs.reserve(m_geometries.size());

    for (const auto& geom : m_geometries)
    {
        auto& geometryDesc = geometryDescs.emplace_back();

        geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geometryDesc.Triangles.Transform3x4 = geom.Transform;
        geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
        geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        geometryDesc.Triangles.IndexCount = geom.IndexCount;
        geometryDesc.Triangles.VertexCount = geom.VertexCount;
        geometryDesc.Triangles.IndexBuffer = geom.Indices->GetGPUVirtualAddress();
        geometryDesc.Triangles.VertexBuffer.StartAddress = geom.Positions->GetGPUVirtualAddress();
        geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(float) * 3;
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blasInputs{};
    blasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    blasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    blasInputs.NumDescs = static_cast<uint32_t>(geometryDescs.size());
    blasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    blasInputs.pGeometryDescs = geometryDescs.data();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO blasPrebuildInfo{};
    m_dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&blasInputs, &blasPrebuildInfo);

    size_t scratchSize = std::max(blasPrebuildInfo.ResultDataMaxSizeInBytes,
                                  tlasPrebuildInfo.ResultDataMaxSizeInBytes);

    com_ptr<ID3D12Resource> scratchResource =
        m_resourceManager->CreateBuffer(scratchSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    m_blas = m_resourceManager->CreateBuffer(
        blasPrebuildInfo.ResultDataMaxSizeInBytes,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

    m_tlas = m_resourceManager->CreateBuffer(
        tlasPrebuildInfo.ResultDataMaxSizeInBytes,
        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

    com_ptr<ID3D12Resource> instanceDescBuffer =
        m_resourceManager->CreateUploadBuffer(sizeof(D3D12_RAYTRACING_INSTANCE_DESC));

    {
        auto it = m_resourceManager->GetUploadIterator<D3D12_RAYTRACING_INSTANCE_DESC>(
            instanceDescBuffer.get());

        it->Transform[0][0] = 1;
        it->Transform[1][1] = 1;
        it->Transform[2][2] = 1;
        it->InstanceMask = 1;
        it->AccelerationStructure = m_blas->GetGPUVirtualAddress();

        ++it;
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

void App::CreateSharedResources()
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
        heapDesc.NumDescriptors = static_cast<uint32_t>(1 + m_geometries.size());
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

    for (auto& geom : m_geometries)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;

        m_device->CreateShaderResourceView(geom.Texture.get(), &srvDesc, cpuHandle);
        geom.TextureSrv = gpuHandle;

        cpuHandle.ptr += descriptorSize;
        gpuHandle.ptr += descriptorSize;
    }

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

namespace
{

struct ShaderId
{
    uint8_t Data[D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES];

    ShaderId() = default;

    explicit ShaderId(void* data)
    {
        memcpy(Data, data, sizeof(Data));
    }
};

struct RayGenShaderRecord
{
    ShaderId ShaderId;
};

struct HitGroupShaderRecord
{
    ShaderId ShaderId;
    D3D12_GPU_VIRTUAL_ADDRESS IndexBuffer;
    D3D12_GPU_VIRTUAL_ADDRESS UVBuffer;
    D3D12_GPU_DESCRIPTOR_HANDLE TextureSrv;
};

struct MissShaderRecord
{
    ShaderId ShaderId;
};

} // namespace

void App::CreateShaderTables()
{
    com_ptr<ID3D12StateObjectProperties> pipelineProps;
    m_pipeline.as(pipelineProps);

    {
        m_rayGenShaderTable.Stride = Align(sizeof(RayGenShaderRecord),
                                           D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
        m_rayGenShaderTable.Size = m_rayGenShaderTable.Stride;
        m_rayGenShaderTable.Buffer =
            m_resourceManager->CreateUploadBuffer(m_rayGenShaderTable.Size);

        auto it = m_resourceManager->GetUploadIterator<RayGenShaderRecord>(
            m_rayGenShaderTable.Buffer.get(), m_rayGenShaderTable.Stride);

        it->ShaderId = ShaderId(pipelineProps->GetShaderIdentifier(L"RayGenShader"));
        ++it;
    }

    {
        m_hitGroupShaderTable.Stride = Align(sizeof(HitGroupShaderRecord),
                                             D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
        m_hitGroupShaderTable.Size = m_hitGroupShaderTable.Stride * m_geometries.size();
        m_hitGroupShaderTable.Buffer =
             m_resourceManager->CreateUploadBuffer(m_hitGroupShaderTable.Size);

        auto it = m_resourceManager->GetUploadIterator<HitGroupShaderRecord>(
            m_hitGroupShaderTable.Buffer.get(), m_hitGroupShaderTable.Stride);

        for (const auto& geom : m_geometries)
        {
            auto& record = it.Get();

            record.ShaderId = ShaderId(pipelineProps->GetShaderIdentifier(L"HitGroup"));
            record.IndexBuffer = geom.Indices->GetGPUVirtualAddress();
            record.UVBuffer = geom.UVs->GetGPUVirtualAddress();
            record.TextureSrv = geom.TextureSrv;

            ++it;
        }
    }

    {
        m_missShaderTable.Stride = Align(sizeof(MissShaderRecord),
                                         D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
        m_missShaderTable.Size = m_missShaderTable.Stride;
        m_missShaderTable.Buffer =
            m_resourceManager->CreateUploadBuffer(m_missShaderTable.Stride);

        auto it = m_resourceManager->GetUploadIterator<MissShaderRecord>(
            m_missShaderTable.Buffer.get(), m_missShaderTable.Stride);

        it->ShaderId = ShaderId(pipelineProps->GetShaderIdentifier(L"MissShader"));
        ++it;
    }
}

void App::Render()
{
    check_hresult(m_cmdAllocator->Reset());
    check_hresult(m_cmdList->Reset(m_cmdAllocator.get(), nullptr));

    m_cmdList->SetComputeRootSignature(m_globalRootSig.get());

    ID3D12DescriptorHeap* descriptorHeaps[] = {m_descriptorHeap.get(), m_samplerHeap.get()};
    m_cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    m_cmdList->SetComputeRootShaderResourceView(Global::Param::Scene,
                                                m_tlas->GetGPUVirtualAddress());

    m_cmdList->SetComputeRootDescriptorTable(Global::Param::Film, m_filmUav);

    m_cmdList->SetComputeRootDescriptorTable(Global::Param::Sampler, m_sampler);

    D3D12_DISPATCH_RAYS_DESC dispatchDesc{};

    dispatchDesc.RayGenerationShaderRecord.StartAddress =
        m_rayGenShaderTable.Buffer->GetGPUVirtualAddress();
    dispatchDesc.RayGenerationShaderRecord.SizeInBytes = m_rayGenShaderTable.Size;

    dispatchDesc.HitGroupTable.StartAddress = m_hitGroupShaderTable.Buffer->GetGPUVirtualAddress();
    dispatchDesc.HitGroupTable.SizeInBytes = m_hitGroupShaderTable.Size;
    dispatchDesc.HitGroupTable.StrideInBytes = m_hitGroupShaderTable.Stride;

    dispatchDesc.MissShaderTable.StartAddress = m_missShaderTable.Buffer->GetGPUVirtualAddress();
    dispatchDesc.MissShaderTable.SizeInBytes = m_missShaderTable.Size;
    dispatchDesc.MissShaderTable.StrideInBytes = m_missShaderTable.Stride;

    dispatchDesc.Width = m_windowWidth;
    dispatchDesc.Height = m_windowHeight;
    dispatchDesc.Depth = 1;

    m_cmdList->SetPipelineState1(m_pipeline.get());
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
