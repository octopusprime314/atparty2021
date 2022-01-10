#include "ResourceBuffer.h"
#include "DXLayer.h"
#include "Logger.h"
#include "ShaderBroker.h"
#include "Texture.h"
#include <iostream>
#include <string>

#define MIP_LEVELS 8

UINT64 ResourceBuffer::_textureMemoryInBytes = 0;

ResourceBuffer::ResourceBuffer(const void* initData, UINT byteSize,
                               ComPtr<ID3D12GraphicsCommandList4>& cmdList,
                               ComPtr<ID3D12Device>&              device)
{

    device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                    D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(byteSize),
                                    D3D12_RESOURCE_STATE_COMMON, nullptr,
                                    IID_PPV_ARGS(_defaultBuffer.GetAddressOf()));

    device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                                    D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(byteSize),
                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                    IID_PPV_ARGS(_uploadBuffer.GetAddressOf()));

    D3D12_SUBRESOURCE_DATA subResourceData = {};
    subResourceData.pData                  = initData;
    subResourceData.RowPitch               = byteSize;
    subResourceData.SlicePitch             = subResourceData.RowPitch;

    auto dxLayer = DXLayer::instance();
    dxLayer->lock();

    cmdList->ResourceBarrier(
        1, &CD3DX12_RESOURCE_BARRIER::Transition(_defaultBuffer.Get(), D3D12_RESOURCE_STATE_COMMON,
                                                 D3D12_RESOURCE_STATE_COPY_DEST));

    UpdateSubresources<1>(cmdList.Get(), _defaultBuffer.Get(), _uploadBuffer.Get(), 0, 0, 1,
                          &subResourceData);

    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                    _defaultBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                    D3D12_RESOURCE_STATE_COMMON));

    dxLayer->unlock();
}

ResourceBuffer::ResourceBuffer(const void* initData, UINT byteSize, UINT width, UINT height, UINT rowPitch, DXGI_FORMAT textureFormat,
                               ComPtr<ID3D12GraphicsCommandList4>& cmdList,
                               ComPtr<ID3D12Device>& device, std::string name)
{

    D3D12_SUBRESOURCE_FOOTPRINT pitchedDesc;
    UINT                        alignedWidthInBytes = 0;

    // BC7 only allows resolutions divisible by 4 and use only one mip
    // Until this is ubiquitously supported on all HW vendors do one mip
    // D3D12_FEATURE_DATA_D3D12_OPTIONS8::UnalignedBlockTexturesSupported
    bool onlyOneMip    = false;

    // Preserve initial height to do the single mip copy correctly
    int initialMipWidth  = width;
    int initialMipHeight = height;

    if ((width % 4 != 0 || height % 4 != 0))
    {
        width += (4 - width % 4);
        height += (4 - height % 4);
        onlyOneMip = true;
    }

    alignedWidthInBytes = width * sizeof(DWORD);
    pitchedDesc.Width    = width;
    pitchedDesc.Height   = height;
    pitchedDesc.Depth    = 1;
    UINT alignment256    = ((alignedWidthInBytes + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) &
                         ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1));
    pitchedDesc.RowPitch = alignment256;
    pitchedDesc.Format   = textureFormat;

    UINT mipLevels = MIP_LEVELS;
    if (log2(width) < 8 || log2(height) < 8)
    {
        mipLevels = min(log2(width), log2(height));
    }
    if (onlyOneMip)
    {
        mipLevels = 1;
    }
    
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts[MIP_LEVELS];
    UINT                               rows[MIP_LEVELS];
    UINT64                             rowByteSize[MIP_LEVELS];
    UINT64                             totalBytes = 0;
    device->GetCopyableFootprints(&CD3DX12_RESOURCE_DESC::Tex2D(pitchedDesc.Format, width, height,
                                                                1, mipLevels, 1, 0,
                                                                D3D12_RESOURCE_FLAG_NONE),
                                                                0, mipLevels, 0, layouts, rows, rowByteSize, &totalBytes);

    // Upload texture data to uploadbuffer
    device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                                    D3D12_HEAP_FLAG_NONE, &CD3DX12_RESOURCE_DESC::Buffer(totalBytes),
                                    D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                    IID_PPV_ARGS(_uploadBuffer.GetAddressOf()));

    auto resourceFlags = D3D12_RESOURCE_FLAG_NONE;
    if (pitchedDesc.Format != DXGI_FORMAT_BC7_UNORM)
    {
        resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Tex2D(pitchedDesc.Format, width, height, 1, mipLevels, 1, 0,
                                          D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&_defaultBuffer));

        mipLevels = 1;
    }
    else
    {
        resourceFlags = D3D12_RESOURCE_FLAG_NONE;
        device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Tex2D(pitchedDesc.Format, width, height, 1, mipLevels, 1, 0,
                                          D3D12_RESOURCE_FLAG_NONE),
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&_defaultBuffer));
    }

    _textureMemoryInBytes += device->GetResourceAllocationInfo(
        0,
        1,
        &CD3DX12_RESOURCE_DESC::Tex2D(pitchedDesc.Format, width, height, 1, mipLevels, 1, 0, resourceFlags)).SizeInBytes;

    OutputDebugString(("Texture memory in MB: " + std::to_string((static_cast<double>(_textureMemoryInBytes) / 1000000.0)) + "\n").c_str());

    auto dxLayer = DXLayer::instance();
    dxLayer->lock();

    cmdList->ResourceBarrier(
        1, &CD3DX12_RESOURCE_BARRIER::Transition(_defaultBuffer.Get(), D3D12_RESOURCE_STATE_COMMON,
                                                 D3D12_RESOURCE_STATE_COPY_DEST));

    D3D12_SUBRESOURCE_DATA data[8];
    void*                  voidPointer = const_cast<void*>(initData);
    UINT                   runningOffset = 0;
    if (pitchedDesc.Format == DXGI_FORMAT_BC7_UNORM)
    {
        for (int i = 0; i < mipLevels; i++)
        {
            int currentOffset = max(1, ((layouts[i].Footprint.Width + 3) / 4)) *
                                max(1, ((layouts[i].Footprint.Height + 3) / 4)) * 16;

            unsigned char* offsetPointer = reinterpret_cast<unsigned char*>(voidPointer) + runningOffset;

            data[i].pData      = offsetPointer;
            // BC7 has 4x4 block texel compression (16 bytes) therefore
            // each row on cpu memory size is the width * 4 bytes.
            data[i].RowPitch   = layouts[i].Footprint.Width * 4;
            data[i].SlicePitch = 0;

            runningOffset += currentOffset;
        }

        UpdateSubresources(cmdList.Get(),
                           _defaultBuffer.Get(),
                           _uploadBuffer.Get(),
                           0,
                           mipLevels,
                           totalBytes,
                           &layouts[0],
                           &rows[0],
                           &rowByteSize[0],
                           &data[0]);
    }
    else
    {
        UINT8* mappedData = nullptr;
        _uploadBuffer->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
        memcpy(mappedData, initData, byteSize);
        _uploadBuffer->Unmap(0, nullptr);
        mappedData = nullptr;

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT placedTexture2D;
        placedTexture2D.Footprint = pitchedDesc;
        auto alignment512 =
            ((reinterpret_cast<UINT64>(mappedData) + D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1) &
             ~(D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT - 1));

        placedTexture2D.Offset = alignment512 - reinterpret_cast<UINT64>(mappedData);
        cmdList->CopyTextureRegion(
            &CD3DX12_TEXTURE_COPY_LOCATION(_defaultBuffer.Get(), 0), 0, 0, 0,
            &CD3DX12_TEXTURE_COPY_LOCATION(_uploadBuffer.Get(), placedTexture2D), nullptr);
    }

    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                    _defaultBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST,
                                    D3D12_RESOURCE_STATE_COMMON));

    int len;
    int slength  = (int)name.length() + 1;
    len          = MultiByteToWideChar(CP_ACP, 0, name.c_str(), slength, 0, 0);
    wchar_t* buf = new wchar_t[len];
    MultiByteToWideChar(CP_ACP, 0, name.c_str(), slength, buf, len);
    std::wstring r(buf);
    delete[] buf;

    LPCWSTR sw = r.c_str();
    _defaultBuffer->SetName(sw);
    dxLayer->unlock();
}

ResourceBuffer::ResourceBuffer(const void* initData, UINT count, UINT byteSize, UINT width,
                               UINT height, UINT rowPitch, DXGI_FORMAT textureFormat,
                               ComPtr<ID3D12GraphicsCommandList4>& cmdList,
                               ComPtr<ID3D12Device>& device, std::string name)
{
    UINT cubeFaces = 6;

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layouts[MIP_LEVELS];
    UINT                               rows[MIP_LEVELS];
    UINT64                             rowByteSize[MIP_LEVELS];
    UINT64                             totalBytes = 0;
    device->GetCopyableFootprints(&CD3DX12_RESOURCE_DESC::Tex2D(textureFormat, width,
                                                                height, cubeFaces, 1, 1, 0,
                                                                D3D12_RESOURCE_FLAG_NONE),
                                  0, cubeFaces, 0, layouts, rows, rowByteSize, &totalBytes);

    // Upload texture data to uploadbuffer
    device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(totalBytes), D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(_uploadBuffer.GetAddressOf()));

    auto resourceFlags = D3D12_RESOURCE_FLAG_NONE;
    device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(textureFormat, width, height, cubeFaces, 1, 1, 0,
                                        D3D12_RESOURCE_FLAG_NONE),
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&_defaultBuffer));

    _textureMemoryInBytes += device->GetResourceAllocationInfo(0, 1,
                                                             &CD3DX12_RESOURCE_DESC::Tex2D(
                                                                 textureFormat, width,
                                                                      height, cubeFaces, 1, 1, 0,
                                                                      resourceFlags)).SizeInBytes;

    OutputDebugString(("Texture memory in MB: " +
                       std::to_string((static_cast<double>(_textureMemoryInBytes) / 1000000.0)) +
                       "\n")
                          .c_str());

    auto dxLayer = DXLayer::instance();
    dxLayer->lock();

    cmdList->ResourceBarrier(
        1, &CD3DX12_RESOURCE_BARRIER::Transition(_defaultBuffer.Get(), D3D12_RESOURCE_STATE_COMMON,
                                                 D3D12_RESOURCE_STATE_COPY_DEST));

    D3D12_SUBRESOURCE_DATA data[8];
    void*                  voidPointer   = const_cast<void*>(initData);
    UINT                   runningOffset = 0;

    for (int i = 0; i < cubeFaces; i++)
    {
        int currentOffset = max(1, ((layouts[i].Footprint.Width + 3) / 4)) *
                            max(1, ((layouts[i].Footprint.Height + 3) / 4)) * 16;

        unsigned char* offsetPointer =
            reinterpret_cast<unsigned char*>(voidPointer) + runningOffset;

        data[i].pData = offsetPointer;
        // BC7 has 4x4 block texel compression (16 bytes) therefore
        // each row on cpu memory size is the width * 4 bytes.
        data[i].RowPitch   = layouts[i].Footprint.Width * 4;
        data[i].SlicePitch = 0;

        runningOffset += currentOffset;
    }

    UpdateSubresources(cmdList.Get(), _defaultBuffer.Get(), _uploadBuffer.Get(), 0, cubeFaces,
                        totalBytes, &layouts[0], &rows[0], &rowByteSize[0], &data[0]);

    cmdList->ResourceBarrier(
        1, &CD3DX12_RESOURCE_BARRIER::Transition(
               _defaultBuffer.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON));

    int len;
    int slength  = (int)name.length() + 1;
    len          = MultiByteToWideChar(CP_ACP, 0, name.c_str(), slength, 0, 0);
    wchar_t* buf = new wchar_t[len];
    MultiByteToWideChar(CP_ACP, 0, name.c_str(), slength, buf, len);
    std::wstring r(buf);
    delete[] buf;

    LPCWSTR sw = r.c_str();
    _defaultBuffer->SetName(sw);
    dxLayer->unlock();
}

ResourceBuffer::ResourceBuffer(D3D12_CLEAR_VALUE clearValue, UINT width, UINT height,
                               ComPtr<ID3D12GraphicsCommandList4>& cmdList,
                               ComPtr<ID3D12Device>& device, std::string name)
{
    // Depth/stencil Buffer
    if (clearValue.Format == DXGI_FORMAT_D32_FLOAT)
    {
        device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Tex2D(clearValue.Format, width, height, 1, 1, 1, 0,
                                          D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
            D3D12_RESOURCE_STATE_COMMON, &clearValue, IID_PPV_ARGS(_defaultBuffer.GetAddressOf()));

    }
    else if (clearValue.Format == DXGI_FORMAT_R32_TYPELESS)
    {
        clearValue.Format = DXGI_FORMAT_D32_FLOAT;
        device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32_TYPELESS, width, height, 1, 1, 1, 0,
                                          D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
            D3D12_RESOURCE_STATE_COMMON, &clearValue, IID_PPV_ARGS(_defaultBuffer.GetAddressOf()));
    }
    else if (clearValue.Format == DXGI_FORMAT_R32G32B32_FLOAT)
    {
        clearValue.Format = DXGI_FORMAT_R32G32B32_FLOAT;
        device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(width * 3 * 4, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(_defaultBuffer.GetAddressOf()));
    }
    else
    {
        device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT), D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Tex2D(clearValue.Format, width, height, 1, 1, 1, 0,
                                          D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                                              D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
            D3D12_RESOURCE_STATE_COMMON, &clearValue, IID_PPV_ARGS(_defaultBuffer.GetAddressOf()));
    }

    int len;
    int slength  = (int)name.length() + 1;
    len          = MultiByteToWideChar(CP_ACP, 0, name.c_str(), slength, 0, 0);
    wchar_t* buf = new wchar_t[len];
    MultiByteToWideChar(CP_ACP, 0, name.c_str(), slength, buf, len);
    std::wstring r(buf);
    delete[] buf;

    LPCWSTR      sw    = r.c_str();
    _defaultBuffer->SetName(sw);
}

D3D12_GPU_VIRTUAL_ADDRESS ResourceBuffer::getGPUAddress()
{
    return _defaultBuffer->GetGPUVirtualAddress();
}

D3D12_RESOURCE_DESC ResourceBuffer::getDescriptor() { return _defaultBuffer->GetDesc(); }

ComPtr<ID3D12Resource> ResourceBuffer::getResource() { return _defaultBuffer; }
ComPtr<ID3D12Resource> ResourceBuffer::getUploadResource() { return _uploadBuffer; }

void ResourceBuffer::buildMipLevels(Texture* texture)
{

    if (texture->getResource()->getResource()->GetDesc().Format != DXGI_FORMAT_B8G8R8A8_UNORM)
    {
        return;
    }
    // Mip level generation
    auto dxLayer      = DXLayer::instance();
    auto mipGenShader = ShaderBroker::instance()->getShader("mipGen")->getShader();
    auto device       = DXLayer::instance()->getDevice();
    auto cmdList      = DXLayer::instance()->getCmdList();
    // Bind read textures
    ImageData imageInfo = {};
    imageInfo.readOnly  = true;
    imageInfo.format    = 0;

    dxLayer->lock();
    // Bind shader
    mipGenShader->bind();

    // Prepare the shader resource view description for the source texture
    D3D12_SHADER_RESOURCE_VIEW_DESC srcTextureSRVDesc = {};
    srcTextureSRVDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srcTextureSRVDesc.ViewDimension                   = D3D12_SRV_DIMENSION_TEXTURE2D;

    // Prepare the unordered access view description for the destination texture
    D3D12_UNORDERED_ACCESS_VIEW_DESC destTextureUAVDesc = {};
    destTextureUAVDesc.ViewDimension                    = D3D12_UAV_DIMENSION_TEXTURE2D;

    // Create the descriptor heap with layout: source texture - destination texture
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.NumDescriptors             = 2 * MIP_LEVELS;
    heapDesc.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap* descriptorHeap;
    device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&descriptorHeap));
    UINT descriptorSize =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    cmdList->SetDescriptorHeaps(1, &descriptorHeap);

    // CPU handle for the first descriptor on the descriptor heap, used to fill the heap
    CD3DX12_CPU_DESCRIPTOR_HANDLE currentCPUHandle(
        descriptorHeap->GetCPUDescriptorHandleForHeapStart(), 0, descriptorSize);

    // GPU handle for the first descriptor on the descriptor heap, used to initialize the descriptor
    // tables
    CD3DX12_GPU_DESCRIPTOR_HANDLE currentGPUHandle(
        descriptorHeap->GetGPUDescriptorHandleForHeapStart(), 0, descriptorSize);

    for (int i = 0; i < MIP_LEVELS - 1; i++)
    {

        uint32_t dstWidth  = max(texture->getWidth() >> (i + 1), 1);
        uint32_t dstHeight = max(texture->getHeight() >> (i + 1), 1);

        float weights[] = {1.0f / static_cast<float>(dstWidth),
                           1.0f / static_cast<float>(dstHeight)};
        cmdList->SetComputeRoot32BitConstants(0, 2, weights, 0);

        // Create shader resource view for the source texture in the descriptor heap
        srcTextureSRVDesc.Format = texture->getResource()->getResource()->GetDesc().Format;
        srcTextureSRVDesc.Texture2D.MipLevels       = 1;
        srcTextureSRVDesc.Texture2D.MostDetailedMip = i;
        device->CreateShaderResourceView(texture->getResource()->getResource().Get(),
                                         &srcTextureSRVDesc, currentCPUHandle);

        currentCPUHandle.Offset(1, descriptorSize);

        // Create unordered access view for the destination texture in the descriptor heap
        destTextureUAVDesc.Format = texture->getResource()->getResource()->GetDesc().Format;
        destTextureUAVDesc.Texture2D.MipSlice = i + 1;
        device->CreateUnorderedAccessView(texture->getResource()->getResource().Get(), nullptr,
                                          &destTextureUAVDesc, currentCPUHandle);

        currentCPUHandle.Offset(1, descriptorSize);

        // Pass the source and destination texture views to the shader via descriptor tables
        cmdList->SetComputeRootDescriptorTable(1, currentGPUHandle);
        currentGPUHandle.Offset(1, descriptorSize);
        cmdList->SetComputeRootDescriptorTable(2, currentGPUHandle);
        currentGPUHandle.Offset(1, descriptorSize);

        // Dispatch the shader
        mipGenShader->dispatch(static_cast<uint32_t>(ceilf(static_cast<float>(dstWidth) / 8.0f)),
                               static_cast<uint32_t>(ceilf(static_cast<float>(dstHeight) / 8.0f)), 1);

        cmdList->ResourceBarrier(
            1, &CD3DX12_RESOURCE_BARRIER::UAV(texture->getResource()->getResource().Get()));
    }
    mipGenShader->unbind();

    dxLayer->unlock();
}
