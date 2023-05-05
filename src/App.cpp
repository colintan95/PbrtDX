#include "App.h"

#include <d3dx12.h>

using winrt::com_ptr;
using winrt::check_bool;
using winrt::check_hresult;

App::App(HWND hwnd) : m_hwnd(hwnd)
{
    CreateDevice();

    CreateCmdQueue();

    CreateSwapChain();

    CreateCmdList();
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

    m_viewport = CD3DX12_VIEWPORT(0.f, 0.f, static_cast<float>(m_windowWidth),
                                  static_cast<float>(m_windowHeight));
    m_scissorRect = CD3DX12_RECT(0, 0, m_windowWidth, m_windowHeight);
}

void App::CreateCmdList()
{
    for (int i = 0; i < _countof(m_frames); ++i)
    {
        check_hresult(m_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(m_frames[i].CmdAllocator.put())));
    }

    check_hresult(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                              m_frames[0].CmdAllocator.get(), nullptr,
                                              IID_PPV_ARGS(m_cmdList.put())));
    m_cmdList->Close();

    check_hresult(m_device->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_NONE,
                                        IID_PPV_ARGS(m_fence.put())));
    ++m_fenceValue;

    m_fenceEvent = CreateEvent(nullptr, false, false, nullptr);
}
