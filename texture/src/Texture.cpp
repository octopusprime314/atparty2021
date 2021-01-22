#include "Texture.h"
#include "DXLayer.h"

Texture::Texture() {}

Texture::Texture(std::string name) : _name(name) {}

Texture::Texture(std::string name, uint32_t width, uint32_t height)
    : _name(name), _width(width), _height(height)
{
}

Texture::~Texture() {}

uint32_t Texture::getContext() { return _textureContext; }

uint32_t Texture::getWidth() { return _width; }

uint32_t Texture::getHeight() { return _height; }

std::string& Texture::getName() { return _name; }

uint32_t Texture::getSizeInBytes() { return _sizeInBytes; }

ResourceBuffer*              Texture::getResource() { return _textureBuffer; }
ComPtr<ID3D12DescriptorHeap> Texture::getUAVDescriptor() { return _uavDescriptorHeap; }

void Texture::bindToDXShader(ComPtr<ID3D12GraphicsCommandList4>& cmdList, UINT textureBinding,
                             std::map<std::string, UINT>& resourceBindings, bool isCompute,
                             bool isUAV)
{

    if (_srvDescriptorHeap != nullptr && _samplerDescriptorHeap != nullptr &&
        resourceBindings.find("textureSampler") != resourceBindings.end())
    {

        ID3D12DescriptorHeap* descriptorHeaps[] = {_srvDescriptorHeap.Get(),
                                                   _samplerDescriptorHeap.Get()};
        cmdList->SetDescriptorHeaps(2, descriptorHeaps);

        if (isCompute)
        {
            cmdList->SetComputeRootDescriptorTable(
                textureBinding, _srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

            cmdList->SetComputeRootDescriptorTable(
                resourceBindings["textureSampler"],
                _samplerDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        }
        else
        {
            cmdList->SetGraphicsRootDescriptorTable(
                textureBinding, _srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

            cmdList->SetGraphicsRootDescriptorTable(
                resourceBindings["textureSampler"],
                _samplerDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        }
    }
    // No sampler and probably just a compute shader
    else
    {
        if (isCompute && !isUAV)
        {
            ID3D12DescriptorHeap* descriptorHeaps[] = {_srvDescriptorHeap.Get(),
                                                       _samplerDescriptorHeap.Get()};
            cmdList->SetDescriptorHeaps(1, descriptorHeaps);

            cmdList->SetComputeRootDescriptorTable(
                textureBinding, _srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        }
        else if (isCompute && isUAV)
        {
            ID3D12DescriptorHeap* descriptorHeaps[] = {_uavDescriptorHeap.Get()};
            cmdList->SetDescriptorHeaps(1, descriptorHeaps);

            cmdList->SetComputeRootDescriptorTable(
                textureBinding, _uavDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        }
        // Special compute shader bindings...TODO fix this garbage
        else
        {
            if (textureBinding == 0)
            {
                ID3D12DescriptorHeap* descriptorHeaps[] = {_srvDescriptorHeap.Get()};
                cmdList->SetDescriptorHeaps(1, descriptorHeaps);

                cmdList->SetComputeRootDescriptorTable(
                    textureBinding, _srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
            }
            else if (textureBinding == 1)
            {
                ID3D12DescriptorHeap* descriptorHeaps[] = {_uavDescriptorHeap.Get()};
                cmdList->SetDescriptorHeaps(1, descriptorHeaps);

                cmdList->SetComputeRootDescriptorTable(
                    textureBinding, _uavDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
            }
        }
    }
}