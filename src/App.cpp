#include "App.h"

#include "gen/shaders/Shader.h"
#include "Mesh.h"

#include <d3dx12.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <chrono>
#include <random>
#include <span>
#include <vector>

using winrt::com_ptr;
using winrt::check_bool;
using winrt::check_hresult;

static const wchar_t* const kRayGenShaderName = L"RayGenShader";
static const wchar_t* const kClosestHitShaderName = L"ClosestHitShader";
static const wchar_t* const kMissShaderName = L"MissShader";

static const wchar_t* const kHitGroupName = L"HitGroup";

static const wchar_t* const kVisibilityClosestHitShaderName = L"VisibilityClosestHitShader";
static const wchar_t* const kVisibilityMissShaderName = L"VisibilityMissShader";

static const wchar_t* const kVisibilityHitGroupName = L"VisibilityHitGroup";

static const wchar_t* const kSphereIntersectShaderName = L"SphereIntersectShader";
static const wchar_t* const kLightClosestHitShaderName = L"LightClosestHitShader";

static const wchar_t* const kLightHitGroupName = L"LightHitGroup";

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

    CreateOtherResources();

    CreateDescriptors();

    CreateShaderTables();
}

void App::CreateDevice()
{
    // com_ptr<ID3D12Debug1> debugController;
    // check_hresult(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.put())));

    // debugController->EnableDebugLayer();
    // debugController->SetEnableGPUBasedValidation(true);

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

        params[Global::Param::DrawConstants].ParameterType =
            D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[Global::Param::DrawConstants].Constants.ShaderRegister = 0;
        params[Global::Param::DrawConstants].Constants.Num32BitValues = 1;

        params[Global::Param::Sampler].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[Global::Param::Sampler].DescriptorTable.NumDescriptorRanges = 1;
        params[Global::Param::Sampler].DescriptorTable.pDescriptorRanges =
            &ranges[Global::Range::Sampler];

        params[Global::Param::Lights].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[Global::Param::Lights].Descriptor.ShaderRegister = 1;

        params[Global::Param::HaltonEntries].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[Global::Param::HaltonEntries].Descriptor.ShaderRegister = 2;

        params[Global::Param::HaltonPerms].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[Global::Param::HaltonPerms].Descriptor.ShaderRegister = 3;

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
        range.BaseShaderRegister = 4;
        range.RegisterSpace = 1;

        D3D12_ROOT_PARAMETER1 params[HitGroup::Param::NUM_PARAMS] = {};

        params[HitGroup::Param::ShaderConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        params[HitGroup::Param::ShaderConstants].Descriptor.ShaderRegister = 0;
        params[HitGroup::Param::ShaderConstants].Descriptor.RegisterSpace = 1;

        params[HitGroup::Param::Indices].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[HitGroup::Param::Indices].Descriptor.ShaderRegister = 0;
        params[HitGroup::Param::Indices].Descriptor.RegisterSpace = 1;

        params[HitGroup::Param::Normals].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[HitGroup::Param::Normals].Descriptor.ShaderRegister = 1;
        params[HitGroup::Param::Normals].Descriptor.RegisterSpace = 1;

        params[HitGroup::Param::UVs].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[HitGroup::Param::UVs].Descriptor.ShaderRegister = 2;
        params[HitGroup::Param::UVs].Descriptor.RegisterSpace = 1;

        params[HitGroup::Param::GeometryConstants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        params[HitGroup::Param::GeometryConstants].Descriptor.ShaderRegister = 3;
        params[HitGroup::Param::GeometryConstants].Descriptor.RegisterSpace = 1;

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
    dxilExports.push_back({kRayGenShaderName, nullptr, D3D12_EXPORT_FLAG_NONE});
    dxilExports.push_back({kClosestHitShaderName, nullptr, D3D12_EXPORT_FLAG_NONE});
    dxilExports.push_back({kMissShaderName, nullptr, D3D12_EXPORT_FLAG_NONE});
    dxilExports.push_back({kVisibilityClosestHitShaderName, nullptr, D3D12_EXPORT_FLAG_NONE});
    dxilExports.push_back({kVisibilityMissShaderName, nullptr, D3D12_EXPORT_FLAG_NONE});
    dxilExports.push_back({kSphereIntersectShaderName, nullptr, D3D12_EXPORT_FLAG_NONE});
    dxilExports.push_back({kLightClosestHitShaderName, nullptr, D3D12_EXPORT_FLAG_NONE});

    D3D12_DXIL_LIBRARY_DESC dxilLibDesc{};
    dxilLibDesc.DXILLibrary.pShaderBytecode = g_shader;
    dxilLibDesc.DXILLibrary.BytecodeLength = ARRAYSIZE(g_shader);
    dxilLibDesc.NumExports = static_cast<uint32_t>(dxilExports.size());
    dxilLibDesc.pExports = dxilExports.data();

    subObjs[SubObj::DxilLib].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
    subObjs[SubObj::DxilLib].pDesc = &dxilLibDesc;

    D3D12_LOCAL_ROOT_SIGNATURE hitGroupRootSigSubObj{};
    hitGroupRootSigSubObj.pLocalRootSignature = m_hitGroupLocalSig.get();

    subObjs[SubObj::HitGroupRootSig].Type = D3D12_STATE_SUBOBJECT_TYPE_LOCAL_ROOT_SIGNATURE;
    subObjs[SubObj::HitGroupRootSig].pDesc = &hitGroupRootSigSubObj;

    const wchar_t* hitGroupRootSigAssocExports[] = {kClosestHitShaderName};

    D3D12_SUBOBJECT_TO_EXPORTS_ASSOCIATION hitGroupRootSigAssoc{};
    hitGroupRootSigAssoc.pSubobjectToAssociate = &subObjs[SubObj::HitGroupRootSig];
    hitGroupRootSigAssoc.NumExports = _countof(hitGroupRootSigAssocExports);
    hitGroupRootSigAssoc.pExports = hitGroupRootSigAssocExports;

    subObjs[SubObj::HitGroupRootSigAssoc].Type =
        D3D12_STATE_SUBOBJECT_TYPE_SUBOBJECT_TO_EXPORTS_ASSOCIATION;
    subObjs[SubObj::HitGroupRootSigAssoc].pDesc = &hitGroupRootSigAssoc;

    D3D12_HIT_GROUP_DESC hitGroupDesc{};
    hitGroupDesc.HitGroupExport = kHitGroupName;
    hitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    hitGroupDesc.ClosestHitShaderImport = kClosestHitShaderName;

    subObjs[SubObj::HitGroup].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subObjs[SubObj::HitGroup].pDesc = &hitGroupDesc;

    D3D12_HIT_GROUP_DESC visibilityHitGroupDesc{};
    visibilityHitGroupDesc.HitGroupExport = kVisibilityHitGroupName;
    visibilityHitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
    visibilityHitGroupDesc.ClosestHitShaderImport = kVisibilityClosestHitShaderName;

    subObjs[SubObj::VisibilityHitGroup].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subObjs[SubObj::VisibilityHitGroup].pDesc = &visibilityHitGroupDesc;

    D3D12_HIT_GROUP_DESC lightHitGroupDesc{};
    lightHitGroupDesc.HitGroupExport = kLightHitGroupName;
    lightHitGroupDesc.Type = D3D12_HIT_GROUP_TYPE_PROCEDURAL_PRIMITIVE;
    lightHitGroupDesc.ClosestHitShaderImport = kLightClosestHitShaderName;
    lightHitGroupDesc.IntersectionShaderImport = kSphereIntersectShaderName;

    subObjs[SubObj::LightHitGroup].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
    subObjs[SubObj::LightHitGroup].pDesc = &lightHitGroupDesc;

    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
    shaderConfig.MaxPayloadSizeInBytes = sizeof(float) * 16;
    shaderConfig.MaxAttributeSizeInBytes = sizeof(float) * 2;

    subObjs[SubObj::ShaderConfig].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    subObjs[SubObj::ShaderConfig].pDesc = &shaderConfig;

    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{};
    pipelineConfig.MaxTraceRecursionDepth = 3;

    subObjs[SubObj::PipelineConfig].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    subObjs[SubObj::PipelineConfig].pDesc = &pipelineConfig;

    D3D12_STATE_OBJECT_DESC pipelineDesc{};
    pipelineDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    pipelineDesc.NumSubobjects = _countof(subObjs);
    pipelineDesc.pSubobjects = subObjs;

    check_hresult(m_device->CreateStateObject(&pipelineDesc, IID_PPV_ARGS(&m_pipeline)));
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
    m_geometries.resize(3);

    LoadGeometry("scenes/pbrt-book/geometry/mesh_00002.ply",
                 "scenes/pbrt-book/texture/book_pages.png", &m_geometries[0]);

    LoadGeometry("scenes/pbrt-book/geometry/mesh_00003.ply",
                 "scenes/pbrt-book/texture/book_pbrt.png", &m_geometries[1]);

    LoadGeometry("scenes/pbrt-book/geometry/mesh_00001.ply", std::nullopt, &m_geometries[2]);

    glm::mat4 transforms[3] = {};

    transforms[0] = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 2.2f, 0.f)) *
                    glm::rotate(glm::mat4(1.f), 1.35f, glm::vec3(0.403f, -0.755f, -0.517f)) *
                    glm::scale(glm::mat4(1.f), glm::vec3(0.5f));

    transforms[1] = glm::translate(glm::mat4(1.f), glm::vec3(0.f, 2.2f, 0.f)) *
                    glm::rotate(glm::mat4(1.f), 1.35f, glm::vec3(0.403f, -0.755f, -0.517f)) *
                    glm::scale(glm::mat4(1.f), glm::vec3(0.5f));

    transforms[2] = glm::scale(glm::mat4(1.f), glm::vec3(0.213f));

    {
        m_transformBuffer = m_resourceManager->CreateUploadBuffer(
            sizeof(Mat3x4) * m_geometries.size());

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

    {
        m_hitGroupGeomConstantsBuffer =
            m_resourceManager->CreateUploadBuffer(sizeof(HitGroupGeometryConstants) * 3);

        auto it = m_resourceManager->GetUploadIterator<HitGroupGeometryConstants>(
            m_hitGroupGeomConstantsBuffer.get());

        it->IsTextured = 1;
        it->NormalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(transforms[0])));
        ++it;

        it->IsTextured = 1;
        it->NormalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(transforms[1])));
        ++it;

        it->IsTextured = 0;
        it->NormalMatrix = glm::mat4(glm::inverseTranspose(glm::mat3(transforms[2])));
        ++it;
    }

    {
        D3D12_RAYTRACING_AABB lightAabb{};
        lightAabb.MinX = -1.f;
        lightAabb.MinY = -1.f;
        lightAabb.MinZ = -1.f;
        lightAabb.MaxX = 1.f;
        lightAabb.MaxY = 1.f;
        lightAabb.MaxZ = 1.f;

        m_aabbBuffer = m_resourceManager->CreateBufferAndUpload(std::span(&lightAabb, 1));
    }

    {
        m_lightBuffer = m_resourceManager->CreateUploadBuffer(sizeof(SphereLight) * 2);

        auto it = m_resourceManager->GetUploadIterator<SphereLight>(m_lightBuffer.get());

        it->Position = glm::vec3(34.92f, 55.92f, -15.351f);
        it->Radius = 7.5f;
        it->L = glm::vec3(41.5594f, 43.3127f, 45.066f);
        ++it;

        it->Position = glm::vec3(-32.892f, 55.92f, 36.293f);
        it->Radius = 7.5f;
        it->L = glm::vec3(65.066f, 63.3127f, 61.5594f);
        ++it;
    }
}

void App::LoadGeometry(std::filesystem::path path, std::optional<std::filesystem::path> texture,
                       Geometry* geometry)
{
    Mesh mesh{};
    LoadMeshFromPlyFile(path, &mesh);

    geometry->Positions = m_resourceManager->CreateBufferAndUpload(std::span(mesh.Positions));
    geometry->Normals = m_resourceManager->CreateBufferAndUpload(std::span(mesh.Normals));
    geometry->UVs = m_resourceManager->CreateBufferAndUpload(std::span(mesh.UVs));
    geometry->Indices  = m_resourceManager->CreateBufferAndUpload(std::span(mesh.Indices));

    geometry->VertexCount = static_cast<uint32_t>(mesh.Positions.size());
    geometry->IndexCount = static_cast<uint32_t>(mesh.Indices.size());

    if (texture)
        geometry->Texture = m_resourceManager->LoadImage(*texture);
}

void App::CreateAccelerationStructures()
{
    check_hresult(m_cmdAllocator->Reset());
    check_hresult(m_cmdList->Reset(m_cmdAllocator.get(), nullptr));

    std::vector<com_ptr<ID3D12Resource>> scratchResources;

    {
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
            geometryDesc.Triangles.VertexBuffer.StartAddress =
                geom.Positions->GetGPUVirtualAddress();
            geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(float) * 3;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blasInputs{};
        blasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        blasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        blasInputs.NumDescs = static_cast<uint32_t>(geometryDescs.size());
        blasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        blasInputs.pGeometryDescs = geometryDescs.data();

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo{};
        m_device->GetRaytracingAccelerationStructurePrebuildInfo(&blasInputs, &prebuildInfo);

        m_blas = m_resourceManager->CreateBuffer(
            prebuildInfo.ResultDataMaxSizeInBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

        auto& scratchResource = scratchResources.emplace_back();

        scratchResource = m_resourceManager->CreateBuffer(
            prebuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blasDesc{};
        blasDesc.DestAccelerationStructureData = m_blas->GetGPUVirtualAddress();
        blasDesc.Inputs = blasInputs;
        blasDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();

        m_cmdList->BuildRaytracingAccelerationStructure(&blasDesc, 0, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = m_blas.get();

        m_cmdList->ResourceBarrier(1, &barrier);
    }

    {
        D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc{};
        geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_PROCEDURAL_PRIMITIVE_AABBS;
        geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        geometryDesc.AABBs.AABBCount = 1;
        geometryDesc.AABBs.AABBs.StartAddress = m_aabbBuffer->GetGPUVirtualAddress();
        geometryDesc.AABBs.AABBs.StrideInBytes = sizeof(D3D12_RAYTRACING_AABB);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS blasInputs{};
        blasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        blasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        blasInputs.NumDescs = 1;
        blasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        blasInputs.pGeometryDescs = &geometryDesc;

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo{};
        m_device->GetRaytracingAccelerationStructurePrebuildInfo(&blasInputs, &prebuildInfo);

        m_lightBlas = m_resourceManager->CreateBuffer(
            prebuildInfo.ResultDataMaxSizeInBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

        auto& scratchResource = scratchResources.emplace_back();

        scratchResource = m_resourceManager->CreateBuffer(
            prebuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC blasDesc{};
        blasDesc.DestAccelerationStructureData = m_lightBlas->GetGPUVirtualAddress();
        blasDesc.Inputs = blasInputs;
        blasDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();

        m_cmdList->BuildRaytracingAccelerationStructure(&blasDesc, 0, nullptr);

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = m_lightBlas.get();

        m_cmdList->ResourceBarrier(1, &barrier);
    }

    com_ptr<ID3D12Resource> instanceDescBuffer;

    {
        instanceDescBuffer = m_resourceManager->CreateUploadBuffer(
            sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * 1);

        {
            auto it = m_resourceManager->GetUploadIterator<D3D12_RAYTRACING_INSTANCE_DESC>(
                instanceDescBuffer.get());

            it->Transform[0][0] = 1.f;
            it->Transform[1][1] = 1.f;
            it->Transform[2][2] = 1.f;
            it->InstanceMask = 1;
            it->AccelerationStructure = m_blas->GetGPUVirtualAddress();
            ++it;

            // it->Transform[0][0] = 1.f;
            // it->Transform[1][1] = 1.f;
            // it->Transform[2][2] = 1.f;
            // it->Transform[0][3] = 0.f;
            // it->Transform[1][3] = 2.f;
            // it->Transform[2][3] = 6.f;
            // it->InstanceMask = 2;
            // it->InstanceContributionToHitGroupIndex = m_geometries.size();
            // it->AccelerationStructure = m_lightBlas->GetGPUVirtualAddress();
            // ++it;
        }

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS tlasInputs{};
        tlasInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
        tlasInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
        tlasInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
        tlasInputs.NumDescs = 1;
        tlasInputs.InstanceDescs = instanceDescBuffer->GetGPUVirtualAddress();

        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO tlasPrebuildInfo{};
        m_device->GetRaytracingAccelerationStructurePrebuildInfo(&tlasInputs, &tlasPrebuildInfo);

        m_tlas = m_resourceManager->CreateBuffer(
            tlasPrebuildInfo.ResultDataMaxSizeInBytes,
            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

        auto& scratchResource = scratchResources.emplace_back();

        scratchResource = m_resourceManager->CreateBuffer(
            tlasPrebuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC tlasDesc{};
        tlasDesc.DestAccelerationStructureData = m_tlas->GetGPUVirtualAddress();
        tlasDesc.Inputs = tlasInputs;
        tlasDesc.ScratchAccelerationStructureData = scratchResource->GetGPUVirtualAddress();

        m_cmdList->BuildRaytracingAccelerationStructure(&tlasDesc, 0, nullptr);
    }

    m_cmdList->Close();

    ID3D12CommandList* cmdLists[] = {m_cmdList.get()};
    m_cmdQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

    WaitForGpu();
}

static constexpr uint16_t PRIMES[] = {
    2, 3, 5, 7, 11,
    13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53, 59, 61, 67, 71, 73, 79, 83, 89, 97, 101,
    103, 107, 109, 113, 127, 131, 137, 139, 149, 151, 157, 163, 167, 173, 179, 181, 191,
    193, 197, 199, 211, 223, 227, 229, 233, 239, 241, 251, 257, 263, 269, 271, 277, 281,
    283, 293, 307, 311, 313, 317, 331, 337, 347, 349, 353, 359, 367, 373, 379, 383, 389,
    397, 401, 409, 419, 421, 431, 433, 439, 443, 449, 457, 461, 463, 467, 479, 487, 491,
    499, 503, 509, 521, 523, 541, 547, 557, 563, 569, 571, 577, 587, 593, 599, 601, 607,
    613, 617, 619, 631, 641, 643, 647, 653, 659, 661, 673, 677, 683, 691, 701, 709, 719,
    727, 733, 739, 743, 751, 757, 761, 769, 773, 787, 797, 809, 811, 821, 823, 827, 829,
    839, 853, 857, 859, 863, 877, 881, 883, 887, 907, 911, 919, 929, 937, 941, 947, 953,
    967, 971, 977, 983, 991, 997, 1009, 1013, 1019, 1021, 1031, 1033, 1039, 1049, 1051,
    1061, 1063, 1069, 1087, 1091, 1093, 1097, 1103, 1109, 1117, 1123, 1129, 1151, 1153,
    1163, 1171, 1181, 1187, 1193, 1201, 1213, 1217, 1223, 1229, 1231, 1237, 1249, 1259,
    1277, 1279, 1283, 1289, 1291, 1297, 1301, 1303, 1307, 1319, 1321, 1327, 1361, 1367,
    1373, 1381, 1399, 1409, 1423, 1427, 1429, 1433, 1439, 1447, 1451, 1453, 1459, 1471,
    1481, 1483, 1487, 1489, 1493, 1499, 1511, 1523, 1531, 1543, 1549, 1553, 1559, 1567,
    1571, 1579, 1583, 1597, 1601, 1607, 1609, 1613, 1619, 1621, 1627, 1637, 1657, 1663,
    1667, 1669, 1693, 1697, 1699, 1709, 1721, 1723, 1733, 1741, 1747, 1753, 1759, 1777,
    1783, 1787, 1789, 1801, 1811, 1823, 1831, 1847, 1861, 1867, 1871, 1873, 1877, 1879,
    1889, 1901, 1907, 1913, 1931, 1933, 1949, 1951, 1973, 1979, 1987, 1993, 1997, 1999,
    2003, 2011, 2017, 2027, 2029, 2039, 2053, 2063, 2069, 2081, 2083, 2087, 2089, 2099,
    2111, 2113, 2129, 2131, 2137, 2141, 2143, 2153, 2161, 2179, 2203, 2207, 2213, 2221,
    2237, 2239, 2243, 2251, 2267, 2269, 2273, 2281, 2287, 2293, 2297, 2309, 2311, 2333,
    2339, 2341, 2347, 2351, 2357, 2371, 2377, 2381, 2383, 2389, 2393, 2399, 2411, 2417,
    2423, 2437, 2441, 2447, 2459, 2467, 2473, 2477, 2503, 2521, 2531, 2539, 2543, 2549,
    2551, 2557, 2579, 2591, 2593, 2609, 2617, 2621, 2633, 2647, 2657, 2659, 2663, 2671,
    2677, 2683, 2687, 2689, 2693, 2699, 2707, 2711, 2713, 2719, 2729, 2731, 2741, 2749,
    2753, 2767, 2777, 2789, 2791, 2797, 2801, 2803, 2819, 2833, 2837, 2843, 2851, 2857,
    2861, 2879, 2887, 2897, 2903, 2909, 2917, 2927, 2939, 2953, 2957, 2963, 2969, 2971,
    2999, 3001, 3011, 3019, 3023, 3037, 3041, 3049, 3061, 3067, 3079, 3083, 3089, 3109,
    3119, 3121, 3137, 3163, 3167, 3169, 3181, 3187, 3191, 3203, 3209, 3217, 3221, 3229,
    3251, 3253, 3257, 3259, 3271, 3299, 3301, 3307, 3313, 3319, 3323, 3329, 3331, 3343,
    3347, 3359, 3361, 3371, 3373, 3389, 3391, 3407, 3413, 3433, 3449, 3457, 3461, 3463,
    3467, 3469, 3491, 3499, 3511, 3517, 3527, 3529, 3533, 3539, 3541, 3547, 3557, 3559,
    3571, 3581, 3583, 3593, 3607, 3613, 3617, 3623, 3631, 3637, 3643, 3659, 3671, 3673,
    3677, 3691, 3697, 3701, 3709, 3719, 3727, 3733, 3739, 3761, 3767, 3769, 3779, 3793,
    3797, 3803, 3821, 3823, 3833, 3847, 3851, 3853, 3863, 3877, 3881, 3889, 3907, 3911,
    3917, 3919, 3923, 3929, 3931, 3943, 3947, 3967, 3989, 4001, 4003, 4007, 4013, 4019,
    4021, 4027, 4049, 4051, 4057, 4073, 4079, 4091, 4093, 4099, 4111, 4127, 4129, 4133,
    4139, 4153, 4157, 4159, 4177, 4201, 4211, 4217, 4219, 4229, 4231, 4241, 4243, 4253,
    4259, 4261, 4271, 4273, 4283, 4289, 4297, 4327, 4337, 4339, 4349, 4357, 4363, 4373,
    4391, 4397, 4409, 4421, 4423, 4441, 4447, 4451, 4457, 4463, 4481, 4483, 4493, 4507,
    4513, 4517, 4519, 4523, 4547, 4549, 4561, 4567, 4583, 4591, 4597, 4603, 4621, 4637,
    4639, 4643, 4649, 4651, 4657, 4663, 4673, 4679, 4691, 4703, 4721, 4723, 4729, 4733,
    4751, 4759, 4783, 4787, 4789, 4793, 4799, 4801, 4813, 4817, 4831, 4861, 4871, 4877,
    4889, 4903, 4909, 4919, 4931, 4933, 4937, 4943, 4951, 4957, 4967, 4969, 4973, 4987,
    4993, 4999, 5003, 5009, 5011, 5021, 5023, 5039, 5051, 5059, 5077, 5081, 5087, 5099,
    5101, 5107, 5113, 5119, 5147, 5153, 5167, 5171, 5179, 5189, 5197, 5209, 5227, 5231,
    5233, 5237, 5261, 5273, 5279, 5281, 5297, 5303, 5309, 5323, 5333, 5347, 5351, 5381,
    5387, 5393, 5399, 5407, 5413, 5417, 5419, 5431, 5437, 5441, 5443, 5449, 5471, 5477,
    5479, 5483, 5501, 5503, 5507, 5519, 5521, 5527, 5531, 5557, 5563, 5569, 5573, 5581,
    5591, 5623, 5639, 5641, 5647, 5651, 5653, 5657, 5659, 5669, 5683, 5689, 5693, 5701,
    5711, 5717, 5737, 5741, 5743, 5749, 5779, 5783, 5791, 5801, 5807, 5813, 5821, 5827,
    5839, 5843, 5849, 5851, 5857, 5861, 5867, 5869, 5879, 5881, 5897, 5903, 5923, 5927,
    5939, 5953, 5981, 5987, 6007, 6011, 6029, 6037, 6043, 6047, 6053, 6067, 6073, 6079,
    6089, 6091, 6101, 6113, 6121, 6131, 6133, 6143, 6151, 6163, 6173, 6197, 6199, 6203,
    6211, 6217, 6221, 6229, 6247, 6257, 6263, 6269, 6271, 6277, 6287, 6299, 6301, 6311,
    6317, 6323, 6329, 6337, 6343, 6353, 6359, 6361, 6367, 6373, 6379, 6389, 6397, 6421,
    6427, 6449, 6451, 6469, 6473, 6481, 6491, 6521, 6529, 6547, 6551, 6553, 6563, 6569,
    6571, 6577, 6581, 6599, 6607, 6619, 6637, 6653, 6659, 6661, 6673, 6679, 6689, 6691,
    6701, 6703, 6709, 6719, 6733, 6737, 6761, 6763, 6779, 6781, 6791, 6793, 6803, 6823,
    6827, 6829, 6833, 6841, 6857, 6863, 6869, 6871, 6883, 6899, 6907, 6911, 6917, 6947,
    6949, 6959, 6961, 6967, 6971, 6977, 6983, 6991, 6997, 7001, 7013, 7019, 7027, 7039,
    7043, 7057, 7069, 7079, 7103, 7109, 7121, 7127, 7129, 7151, 7159, 7177, 7187, 7193,
    7207, 7211, 7213, 7219, 7229, 7237, 7243, 7247, 7253, 7283, 7297, 7307, 7309, 7321,
    7331, 7333, 7349, 7351, 7369, 7393, 7411, 7417, 7433, 7451, 7457, 7459, 7477, 7481,
    7487, 7489, 7499, 7507, 7517, 7523, 7529, 7537, 7541, 7547, 7549, 7559, 7561, 7573,
    7577, 7583, 7589, 7591, 7603, 7607, 7621, 7639, 7643, 7649, 7669, 7673, 7681, 7687,
    7691, 7699, 7703, 7717, 7723, 7727, 7741, 7753, 7757, 7759, 7789, 7793, 7817, 7823,
    7829, 7841, 7853, 7867, 7873, 7877, 7879, 7883, 7901, 7907, 7919};


void App::CreateOtherResources()
{
    {
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        CD3DX12_RESOURCE_DESC resourceDesc =
            CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, m_windowWidth, m_windowHeight,
                                         1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        check_hresult(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                        &resourceDesc,
                                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                        nullptr, IID_PPV_ARGS(m_film.put())));
    }

    size_t numPrimes = _countof(PRIMES);

    std::vector<HaltonEntry> haltonEntries(numPrimes);

    size_t permSize = 0;

    for (int i = 0; i < numPrimes; ++i)
    {
        haltonEntries[i].Prime = PRIMES[i];
        haltonEntries[i].PermutationOffset = static_cast<uint32_t>(permSize);

        permSize += haltonEntries[i].Prime;
    }

    m_haltonEntries = m_resourceManager->CreateBufferAndUpload(std::span(haltonEntries));

    std::vector<uint16_t> permutations(permSize);

    uint32_t seed =
        static_cast<uint32_t>(std::chrono::system_clock::now().time_since_epoch().count());

    for (const auto& entry : haltonEntries)
    {
        for (int j = 0; j < entry.Prime; ++j)
        {
            permutations[entry.PermutationOffset + j] = static_cast<uint16_t>(j);
        }

        auto it = permutations.begin() + entry.PermutationOffset;
        std::shuffle(it, it + entry.Prime, std::default_random_engine(seed));
    }

    m_haltonPerms = m_resourceManager->CreateBufferAndUpload(std::span(permutations));

    m_hitGroupShaderConstantsBuffer =
        m_resourceManager->CreateUploadBufferAndMap(&m_hitGroupShaderConstants);
}

void App::CreateDescriptors()
{
    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = static_cast<uint32_t>(1 + m_geometries.size());
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        m_descriptorHeap = std::make_unique<DescriptorHeap>(heapDesc, m_device.get());
    }

    {
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
        uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

        auto handles = m_descriptorHeap->Allocate();

        m_device->CreateUnorderedAccessView(m_film.get(), nullptr, &uavDesc, handles.CpuHandle);
        m_filmUav = handles.GpuHandle;
    }

    for (auto& geom : m_geometries)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = 1;

        auto handles = m_descriptorHeap->Allocate();

        m_device->CreateShaderResourceView(geom.Texture.get(), &srvDesc, handles.CpuHandle);
        geom.TextureSrv = handles.GpuHandle;
    }

    {
        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.NumDescriptors = 1;
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

        m_samplerHeap = std::make_unique<DescriptorHeap>(heapDesc, m_device.get());
    }

    {
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

        auto handles = m_samplerHeap->Allocate();

        m_device->CreateSampler(&samplerDesc, handles.CpuHandle);
        m_sampler = handles.GpuHandle;
    }
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

union GeometryHitGroupShaderRecord
{
    struct
    {
        ShaderId ShaderId;
        D3D12_GPU_VIRTUAL_ADDRESS ShaderConstants;
        D3D12_GPU_VIRTUAL_ADDRESS Indices;
        D3D12_GPU_VIRTUAL_ADDRESS Normals;
        D3D12_GPU_VIRTUAL_ADDRESS UVs;
        D3D12_GPU_VIRTUAL_ADDRESS GeometryConstants;
        D3D12_GPU_DESCRIPTOR_HANDLE TextureSrv;
    } L;

    struct
    {
        ShaderId ShaderId;
    } Visibility;
};

struct LightHitGroupShaderRecord
{
    ShaderId ShaderId;
};

union HitGroupShaderRecord
{
    GeometryHitGroupShaderRecord Geometry;
    LightHitGroupShaderRecord Light;
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

        it->ShaderId = ShaderId(pipelineProps->GetShaderIdentifier(kRayGenShaderName));
        ++it;
    }

    {
        m_hitGroupShaderTable.Stride = Align(sizeof(HitGroupShaderRecord),
                                             D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
        m_hitGroupShaderTable.Size = m_hitGroupShaderTable.Stride * (m_geometries.size() * 2 + 1);
        m_hitGroupShaderTable.Buffer =
             m_resourceManager->CreateUploadBuffer(m_hitGroupShaderTable.Size);

        auto it = m_resourceManager->GetUploadIterator<HitGroupShaderRecord>(
            m_hitGroupShaderTable.Buffer.get(), m_hitGroupShaderTable.Stride);

        for (const auto& geom : m_geometries)
        {
            it->Geometry.L.ShaderId = ShaderId(pipelineProps->GetShaderIdentifier(kHitGroupName));
            it->Geometry.L.ShaderConstants =
                m_hitGroupShaderConstantsBuffer->GetGPUVirtualAddress();
            it->Geometry.L.Indices = geom.Indices->GetGPUVirtualAddress();
            it->Geometry.L.Normals = geom.Normals->GetGPUVirtualAddress();
            it->Geometry.L.UVs = geom.UVs->GetGPUVirtualAddress();
            it->Geometry.L.GeometryConstants =
                m_hitGroupGeomConstantsBuffer->GetGPUVirtualAddress();
            it->Geometry.L.TextureSrv = geom.TextureSrv;
            ++it;
        }

        it->Light.ShaderId = ShaderId(pipelineProps->GetShaderIdentifier(kLightHitGroupName));
        ++it;

        m_hitGroupShaderConstants->VisibilityHitGroupBaseIndex =
            static_cast<uint32_t>(m_geometries.size() + 1);

        for (int i = 0; i < m_geometries.size(); ++i)
        {
            it->Geometry.Visibility.ShaderId =
                ShaderId(pipelineProps->GetShaderIdentifier(kVisibilityHitGroupName));
            ++it;
        }
    }

    {
        m_missShaderTable.Stride = Align(sizeof(MissShaderRecord),
                                         D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
        m_missShaderTable.Size = m_missShaderTable.Stride * 2;
        m_missShaderTable.Buffer =
            m_resourceManager->CreateUploadBuffer(m_missShaderTable.Size);

        auto it = m_resourceManager->GetUploadIterator<MissShaderRecord>(
            m_missShaderTable.Buffer.get(), m_missShaderTable.Stride);

        it->ShaderId = ShaderId(pipelineProps->GetShaderIdentifier(kMissShaderName));
        ++it;

        it->ShaderId = ShaderId(pipelineProps->GetShaderIdentifier(kVisibilityMissShaderName));
        ++it;
    }
}

void App::Render()
{
    check_hresult(m_cmdAllocator->Reset());
    check_hresult(m_cmdList->Reset(m_cmdAllocator.get(), nullptr));

    static constexpr int MAX_SAMPLES = 2048;

    if (m_sampleIdx < MAX_SAMPLES)
    {
        m_cmdList->SetComputeRootSignature(m_globalRootSig.get());

        ID3D12DescriptorHeap* descriptorHeaps[] = {
            m_descriptorHeap->Inner(), m_samplerHeap->Inner()
        };
        m_cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

        m_cmdList->SetComputeRootShaderResourceView(Global::Param::Scene,
                                                    m_tlas->GetGPUVirtualAddress());

        m_cmdList->SetComputeRootDescriptorTable(Global::Param::Film, m_filmUav);

        m_cmdList->SetComputeRoot32BitConstant(Global::Param::DrawConstants, m_sampleIdx, 0);
        ++m_sampleIdx;

        m_cmdList->SetComputeRootDescriptorTable(Global::Param::Sampler, m_sampler);

        m_cmdList->SetComputeRootShaderResourceView(Global::Param::Lights,
                                                    m_lightBuffer->GetGPUVirtualAddress());

        m_cmdList->SetComputeRootShaderResourceView(Global::Param::HaltonEntries,
                                                    m_haltonEntries->GetGPUVirtualAddress());
        m_cmdList->SetComputeRootShaderResourceView(Global::Param::HaltonPerms,
                                                    m_haltonPerms->GetGPUVirtualAddress());

        D3D12_DISPATCH_RAYS_DESC dispatchDesc{};

        dispatchDesc.RayGenerationShaderRecord.StartAddress =
            m_rayGenShaderTable.Buffer->GetGPUVirtualAddress();
        dispatchDesc.RayGenerationShaderRecord.SizeInBytes = m_rayGenShaderTable.Size;

        dispatchDesc.HitGroupTable.StartAddress =
            m_hitGroupShaderTable.Buffer->GetGPUVirtualAddress();
        dispatchDesc.HitGroupTable.SizeInBytes = m_hitGroupShaderTable.Size;
        dispatchDesc.HitGroupTable.StrideInBytes = m_hitGroupShaderTable.Stride;

        dispatchDesc.MissShaderTable.StartAddress =
            m_missShaderTable.Buffer->GetGPUVirtualAddress();
        dispatchDesc.MissShaderTable.SizeInBytes = m_missShaderTable.Size;
        dispatchDesc.MissShaderTable.StrideInBytes = m_missShaderTable.Stride;

        dispatchDesc.Width = m_windowWidth;
        dispatchDesc.Height = m_windowHeight;
        dispatchDesc.Depth = 1;

        m_cmdList->SetPipelineState1(m_pipeline.get());
        m_cmdList->DispatchRays(&dispatchDesc);
    }

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

void App::WaitForGpu()
{
    uint64_t waitValue = m_fenceValue;
    ++m_fenceValue;

    m_cmdQueue->Signal(m_fence.get(), waitValue);

    check_hresult(m_fence->SetEventOnCompletion(waitValue, m_fenceEvent));

    WaitForSingleObjectEx(m_fenceEvent, INFINITE, false);
}
