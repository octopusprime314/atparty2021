#pragma once

#include "d3dx12.h"
#include <d3d12.h>
#include <wrl.h>
#include <mutex>

using namespace Microsoft::WRL;
class Texture;

class ResourceBuffer
{

    ComPtr<ID3D12Resource> _defaultBuffer;
    ComPtr<ID3D12Resource> _uploadBuffer;

    static UINT64          _textureMemoryInBytes;

  public:
    // Buffer
    ResourceBuffer(const void* initData, UINT byteSize, ComPtr<ID3D12GraphicsCommandList4>& cmdList,
                   ComPtr<ID3D12Device>& device);

    // Texture2D
    ResourceBuffer(const void* initData, UINT byteSize, UINT width, UINT height, UINT rowPitch, DXGI_FORMAT textureFormat,
                   ComPtr<ID3D12GraphicsCommandList4>& cmdList, ComPtr<ID3D12Device>& device, std::string name = "");

    // TextureCube
    ResourceBuffer(const void* initData, UINT count, UINT byteSize, UINT width, UINT height,
                   UINT rowPitch, DXGI_FORMAT textureFormat, ComPtr<ID3D12GraphicsCommandList4>& cmdList,
                   ComPtr<ID3D12Device>& device, std::string name);

    // Render Target and Depth/Stencil Texture2D
    ResourceBuffer(D3D12_CLEAR_VALUE clearValue, UINT width, UINT height,
                   ComPtr<ID3D12GraphicsCommandList4>& cmdList, ComPtr<ID3D12Device>& device, std::string name = "");

    void uploadNewData(const void* initData, UINT byteSize,
                       ComPtr<ID3D12GraphicsCommandList4>& cmdList);

    void                      buildMipLevels(Texture* texture);
    D3D12_GPU_VIRTUAL_ADDRESS getGPUAddress();
    D3D12_RESOURCE_DESC       getDescriptor();
    ComPtr<ID3D12Resource>    getResource();
    ComPtr<ID3D12Resource>    getUploadResource();
};