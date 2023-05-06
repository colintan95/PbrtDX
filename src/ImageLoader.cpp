#include "ImageLoader.h"

#include <d3dx12.h>

using winrt::check_hresult;
using winrt::com_ptr;

ImageLoader::ImageLoader(ID3D12Device* device) : m_device(device)
{
    static constexpr auto cmdListType = D3D12_COMMAND_LIST_TYPE_COPY;

    D3D12_COMMAND_QUEUE_DESC copyQueueDesc{};
    copyQueueDesc.Type = cmdListType;

    check_hresult(m_device->CreateCommandQueue(&copyQueueDesc, IID_PPV_ARGS(m_copyQueue.put())));

    check_hresult(m_device->CreateCommandAllocator(cmdListType,
                                                   IID_PPV_ARGS(m_cmdAllocator.put())));

    check_hresult(m_device->CreateCommandList(0, cmdListType, m_cmdAllocator.get(), nullptr,
                                              IID_PPV_ARGS(m_cmdList.put())));
    check_hresult(m_cmdList->Close());

    check_hresult(m_device->CreateFence(m_fenceValue, D3D12_FENCE_FLAG_NONE,
                                        IID_PPV_ARGS(m_fence.put())));
    ++m_fenceValue;

    check_hresult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(m_wicFactory.put())));
}

com_ptr<ID3D12Resource> ImageLoader::LoadImage(std::filesystem::path path)
{
    com_ptr<IWICBitmapDecoder> decoder;
    check_hresult(m_wicFactory->CreateDecoderFromFilename(path.wstring().c_str(), nullptr,
                                                          GENERIC_READ,
                                                          WICDecodeMetadataCacheOnLoad,
                                                          decoder.put()));

    com_ptr<IWICBitmapFrameDecode> decoderFrame;
    check_hresult(decoder->GetFrame(0, decoderFrame.put()));

    WICPixelFormatGUID srcFormat;
    check_hresult(decoderFrame->GetPixelFormat(&srcFormat));

    assert(srcFormat == GUID_WICPixelFormat24bppBGR || srcFormat == GUID_WICPixelFormat32bppBGRA);

    com_ptr<IWICFormatConverter> formatConverter;
    check_hresult(m_wicFactory->CreateFormatConverter(formatConverter.put()));

    static WICPixelFormatGUID dstFormat = GUID_WICPixelFormat32bppRGBA;

    BOOL canConvert = false;
    check_hresult(formatConverter->CanConvert(srcFormat, dstFormat, &canConvert));

    assert(canConvert);

    check_hresult(formatConverter->Initialize(decoderFrame.get(), dstFormat,
                                              WICBitmapDitherTypeNone, nullptr, 0.f,
                                              WICBitmapPaletteTypeCustom));

    com_ptr<IWICBitmap> bitmap;
    check_hresult(m_wicFactory->CreateBitmapFromSource(formatConverter.get(), WICBitmapCacheOnLoad,
                                                       bitmap.put()));

    com_ptr<IWICBitmapLock> bitmapLock;
    check_hresult(bitmap->Lock(nullptr, WICBitmapLockRead, bitmapLock.put()));

    uint32_t bitmapBufferSize = 0;
    BYTE* bitmapBuffer = nullptr;

    check_hresult(bitmapLock->GetDataPointer(&bitmapBufferSize, &bitmapBuffer));

    uint32_t bitmapWidth = 0;
    uint32_t bitmapHeight = 0;
    check_hresult(formatConverter->GetSize(&bitmapWidth, &bitmapHeight));

    CD3DX12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM,
                                                                     bitmapWidth, bitmapHeight);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT copySrcLayout{};
    uint64_t uploadBufferSize = 0;
    m_device->GetCopyableFootprints(&textureDesc, 0, 1, 0, &copySrcLayout, nullptr, nullptr,
                                    &uploadBufferSize);

    com_ptr<ID3D12Resource> uploadBuffer;

     {
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_UPLOAD);
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize);

        check_hresult(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                        &bufferDesc,
                                                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                                        IID_PPV_ARGS(uploadBuffer.put())));
    }

    std::byte* uploadPtr = nullptr;
    check_hresult(uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&uploadPtr)));

    std::byte* bitmapPtr = reinterpret_cast<std::byte*>(bitmapBuffer);

    for (size_t i = 0; i < copySrcLayout.Footprint.Height; ++i)
    {
        memcpy(uploadPtr, bitmapPtr, bitmapWidth * 4);

        uploadPtr += copySrcLayout.Footprint.RowPitch;
        bitmapPtr += bitmapWidth * 4;
    }

    uploadBuffer->Unmap(0, nullptr);

    com_ptr<ID3D12Resource> resource;

    {
        CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);
        check_hresult(m_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE,
                                                        &textureDesc, D3D12_RESOURCE_STATE_COMMON,
                                                        nullptr, IID_PPV_ARGS(resource.put())));
    }

    check_hresult(m_cmdAllocator->Reset());
    check_hresult(m_cmdList->Reset(m_cmdAllocator.get(), nullptr));

    D3D12_TEXTURE_COPY_LOCATION copySrc{};
    copySrc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    copySrc.pResource = uploadBuffer.get();
    copySrc.PlacedFootprint = copySrcLayout;

    D3D12_TEXTURE_COPY_LOCATION copyDst;
    copyDst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    copyDst.pResource = resource.get();
    copyDst.SubresourceIndex = 0;

    m_cmdList->CopyTextureRegion(&copyDst, 0, 0, 0, &copySrc, nullptr);

    check_hresult(m_cmdList->Close());

    ID3D12CommandList* cmdLists[] = { m_cmdList.get() };
    m_copyQueue->ExecuteCommandLists(static_cast<uint32_t>(std::size(cmdLists)), cmdLists);

    check_hresult(m_copyQueue->Signal(m_fence.get(), m_fenceValue));

    while (m_fence->GetCompletedValue() < m_fenceValue)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    ++m_fenceValue;

    return resource;
}
