#pragma once
#include "Matrix.h"
#include "d3dx12.h"
#include <d3d12.h>
#include <wrl.h>
#include "DXDefines.h"
#include <vector>
#include <d3d12shader.h>

using namespace Microsoft::WRL;

class ConstantBuffer
{

  public:
    ConstantBuffer(ComPtr<ID3D12Device> device, std::vector<D3D12_SHADER_VARIABLE_DESC> constants);
    ~ConstantBuffer();

    void update(ComPtr<ID3D12GraphicsCommandList4> cmdList, int cmdListIndex, void* data, std::string id,
                UINT resourceBinding, UINT sizeInBytes, UINT offsetInBytes, bool isCompute);

  private:
    ComPtr<ID3D12Resource>          _uploadBuffer[CMD_LIST_NUM];
    ComPtr<ID3D12DescriptorHeap>    _cbvHeap;
    D3D12_CONSTANT_BUFFER_VIEW_DESC _cbvDesc[CMD_LIST_NUM];
    UINT                            _cbOffset;
};