#include "ConstantBuffer.h"

struct ObjectConstants
{
    Matrix mvp;
};

UINT getConstantBufferByteSize(UINT byteSize) { return (byteSize + 255) & ~255; }

ConstantBuffer::ConstantBuffer(ComPtr<ID3D12Device>                    device,
                               std::vector<D3D12_SHADER_VARIABLE_DESC> constants)
{
    UINT bytes = 0;
    for (auto constant : constants)
    {
        bytes += constant.Size;
    }

    UINT elementsByteSize = getConstantBufferByteSize(bytes);

    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = CMD_LIST_NUM;
    cbvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvHeapDesc.NodeMask       = 0;
    device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&_cbvHeap));

    auto descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    for (int i = 0; i < CMD_LIST_NUM; i++)
    {
        device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(elementsByteSize), D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr, IID_PPV_ARGS(&_uploadBuffer[i]));

        _cbvDesc[i].BufferLocation = _uploadBuffer[i]->GetGPUVirtualAddress();
        _cbvDesc[i].SizeInBytes    = getConstantBufferByteSize(bytes);

        auto cpuDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(_cbvHeap->GetCPUDescriptorHandleForHeapStart(), i, descriptorSize);
        device->CreateConstantBufferView(&_cbvDesc[i], cpuDescriptor);
    }
}

ConstantBuffer::~ConstantBuffer() {}

void ConstantBuffer::update(ComPtr<ID3D12GraphicsCommandList4> cmdList,
                            int cmdListIndex, void* data, std::string id,
                            UINT resourceBinding, UINT sizeInBytes, UINT offsetInBytes,
                            bool isCompute)
{
    if (id.compare("objectData") == 0)
    {
        if (isCompute)
        {
            cmdList->SetComputeRoot32BitConstants(0, sizeInBytes / 4, data, offsetInBytes / 4);
        }
        else
        {
            cmdList->SetGraphicsRoot32BitConstants(0, sizeInBytes / 4, data, offsetInBytes / 4);
        }
    }
    else
    {
        BYTE* mappedData = nullptr;
        auto result = _uploadBuffer[cmdListIndex]->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));

        if (result != S_OK)
        {
            return;
        }
        memcpy(&mappedData[offsetInBytes], data, sizeInBytes);

        _uploadBuffer[cmdListIndex]->Unmap(0, nullptr);
        mappedData = nullptr;

        ID3D12DescriptorHeap* descriptorHeaps[] = {_cbvHeap.Get()};
        cmdList->SetDescriptorHeaps(1, descriptorHeaps);

        if (isCompute)
        {
            cmdList->SetComputeRootConstantBufferView(resourceBinding,
                                                      _uploadBuffer[cmdListIndex]->GetGPUVirtualAddress());
        }
        else
        {
            cmdList->SetGraphicsRootConstantBufferView(resourceBinding,
                                                       _uploadBuffer[cmdListIndex]->GetGPUVirtualAddress());
        }
    }
}
