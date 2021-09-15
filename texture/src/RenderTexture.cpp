#include "RenderTexture.h"
#include "DXLayer.h"
#include "EngineManager.h"

RenderTexture::RenderTexture() {}

RenderTexture::RenderTexture(uint32_t width, uint32_t height, TextureFormat format,
                             std::string objectName)
    : Texture(objectName, width, height)
{
    if (format == TextureFormat::RGBA_UNSIGNED_BYTE || format == TextureFormat::RGBA_FLOAT ||
        format == TextureFormat::R16G16_FLOAT || format == TextureFormat::R16_FLOAT ||
        format == TextureFormat::R8_UINT || format == TextureFormat::R16G16B16A16_FLOAT)
    {
        // Color Buffer

        D3D12_CLEAR_VALUE colorOptimizedClearValue;
        ZeroMemory(&colorOptimizedClearValue, sizeof(colorOptimizedClearValue));

        switch (format)
        {
            case TextureFormat::RGBA_UNSIGNED_BYTE:
            {
                colorOptimizedClearValue.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                break;
            }
            case TextureFormat::RGBA_FLOAT:
            {
                colorOptimizedClearValue.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                break;
            }
            case TextureFormat::R16G16_FLOAT:
            {
                colorOptimizedClearValue.Format = DXGI_FORMAT_R16G16_FLOAT;
                break;
            }
            case TextureFormat::R16G16B16A16_FLOAT:
            {
                colorOptimizedClearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
                break;
            }
            case TextureFormat::R16_FLOAT:
            {
                colorOptimizedClearValue.Format = DXGI_FORMAT_R16_FLOAT;
                break;
            }
            case TextureFormat::R8_UINT:
            {
                colorOptimizedClearValue.Format = DXGI_FORMAT_R8_UINT;
                break;
            }
            default:
            {
                break;
            }
        };

        _format = colorOptimizedClearValue.Format;

        colorOptimizedClearValue.Color[0] = 0.0f;
        colorOptimizedClearValue.Color[1] = 0.0f;
        colorOptimizedClearValue.Color[2] = 0.0f;
        colorOptimizedClearValue.Color[3] = 1.0f;

        auto device  = DXLayer::instance()->getDevice();
        auto cmdList = DXLayer::instance()->getTextureCopyCmdList();

        _textureBuffer =
            new ResourceBuffer(colorOptimizedClearValue, _width, _height, cmdList, device, _name);

        D3D12_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
        ZeroMemory(&renderTargetViewDesc, sizeof(renderTargetViewDesc));
        renderTargetViewDesc.Format        = colorOptimizedClearValue.Format;
        renderTargetViewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;

        // Create descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
        ZeroMemory(&rtvHeapDesc, sizeof(rtvHeapDesc));
        rtvHeapDesc.NumDescriptors = 1; // 1 2D texture
        rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        device->CreateDescriptorHeap(&rtvHeapDesc,
                                        IID_PPV_ARGS(_rtvDescriptorHeap.GetAddressOf()));

        // Backbuffer / render target
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
            _rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        auto rtvDescriptorSize =
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        device->CreateRenderTargetView(_textureBuffer->getResource().Get(),
                                        &renderTargetViewDesc, rtvHandle);

        // Create descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc;
        ZeroMemory(&srvHeapDesc, sizeof(srvHeapDesc));
        srvHeapDesc.NumDescriptors = 1; // 1 2D texture
        srvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        device->CreateDescriptorHeap(&srvHeapDesc,
                                        IID_PPV_ARGS(_srvDescriptorHeap.GetAddressOf()));

        // Create view of SRV for shader access
        CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(
            _srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc           = {};
        auto                            textureDescriptor = _textureBuffer->getDescriptor();
        srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format                        = textureDescriptor.Format;
        srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip     = 0;
        srvDesc.Texture2D.MipLevels           = textureDescriptor.MipLevels;
        srvDesc.Texture2D.ResourceMinLODClamp = 0;
        device->CreateShaderResourceView(_textureBuffer->getResource().Get(), &srvDesc,
                                            hDescriptor);

        // Create descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc;
        ZeroMemory(&uavHeapDesc, sizeof(uavHeapDesc));
        uavHeapDesc.NumDescriptors = 1; // 1 2D texture
        uavHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        uavHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        device->CreateDescriptorHeap(&uavHeapDesc,
                                        IID_PPV_ARGS(_uavDescriptorHeap.GetAddressOf()));

        // Create view of UAV for shader access
        CD3DX12_CPU_DESCRIPTOR_HANDLE hUAVDescriptor(
            _uavDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format                           = textureDescriptor.Format;
        uavDesc.ViewDimension                    = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice               = 0;
        device->CreateUnorderedAccessView(_textureBuffer->getResource().Get(), nullptr,
                                            &uavDesc, hUAVDescriptor);

        // Create cpu only visible descriptor heap for clears of uavs
        ZeroMemory(&uavHeapDesc, sizeof(uavHeapDesc));
        uavHeapDesc.NumDescriptors = 1; // 1 2D texture
        uavHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        uavHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        device->CreateDescriptorHeap(&uavHeapDesc,
                                        IID_PPV_ARGS(_uavClearDescriptorHeap.GetAddressOf()));

        // Create view of UAV for shader access
        hUAVDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            _uavClearDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        uavDesc = {};
        uavDesc.Format                           = textureDescriptor.Format;
        uavDesc.ViewDimension                    = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice               = 0;
        device->CreateUnorderedAccessView(_textureBuffer->getResource().Get(), nullptr,
                                            &uavDesc, hUAVDescriptor);

        // create sampler descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC descHeapSampler = {};
        descHeapSampler.NumDescriptors             = 1;
        descHeapSampler.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        descHeapSampler.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        device->CreateDescriptorHeap(&descHeapSampler,
                                        IID_PPV_ARGS(_samplerDescriptorHeap.GetAddressOf()));

        // create sampler descriptor in the sample descriptor heap
        D3D12_SAMPLER_DESC samplerDesc;
        ZeroMemory(&samplerDesc, sizeof(D3D12_SAMPLER_DESC));
        samplerDesc.Filter     = D3D12_FILTER_ANISOTROPIC; // D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU   = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressV   = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressW   = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.MinLOD     = 0;
        samplerDesc.MaxLOD     = D3D12_FLOAT32_MAX;
        samplerDesc.MipLODBias = 0.0f;
        samplerDesc.MaxAnisotropy  = 1;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        device->CreateSampler(&samplerDesc,
                                _samplerDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

        // Viewport

        _viewPort = {0.0f, 0.0f, static_cast<float>(_width), static_cast<float>(_height),
                        0.0f, 1.0f};

        // Scissor rectangle

        _rectScissor = {0, 0, (LONG)_width, (LONG)_height};
    }
    else if (format == TextureFormat::DEPTH32_FLOAT)
    {
        // Depth Buffer

        D3D12_CLEAR_VALUE depthOptimizedClearValue;
        ZeroMemory(&depthOptimizedClearValue, sizeof(depthOptimizedClearValue));
        depthOptimizedClearValue.Format               = DXGI_FORMAT_R32_TYPELESS;
        depthOptimizedClearValue.DepthStencil.Depth   = 1.0f;
        depthOptimizedClearValue.DepthStencil.Stencil = 0;

        _format = depthOptimizedClearValue.Format;

        auto device  = DXLayer::instance()->getDevice();
        auto cmdList = DXLayer::instance()->getCmdList();

        _textureBuffer =
            new ResourceBuffer(depthOptimizedClearValue, _width, _height, cmdList, device, _name);

        D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc;
        ZeroMemory(&depthStencilViewDesc, sizeof(depthStencilViewDesc));
        depthStencilViewDesc.Format        = DXGI_FORMAT_D32_FLOAT;
        depthStencilViewDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
        depthStencilViewDesc.Flags         = D3D12_DSV_FLAG_NONE;

        // Create descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
        ZeroMemory(&dsvHeapDesc, sizeof(dsvHeapDesc));
        dsvHeapDesc.NumDescriptors = 1; // 1 2D texture
        dsvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        device->CreateDescriptorHeap(&dsvHeapDesc,
                                        IID_PPV_ARGS(_rtvDescriptorHeap.GetAddressOf()));

        // Backbuffer / render target
        CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(
            _rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        auto dsvDescriptorSize =
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

        device->CreateDepthStencilView(_textureBuffer->getResource().Get(),
                                        &depthStencilViewDesc, dsvHandle);

        // Create descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc;
        ZeroMemory(&srvHeapDesc, sizeof(srvHeapDesc));
        srvHeapDesc.NumDescriptors = 1; // 1 2D texture
        srvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        device->CreateDescriptorHeap(&srvHeapDesc,
                                        IID_PPV_ARGS(_srvDescriptorHeap.GetAddressOf()));

        // Create view of SRV for shader access
        CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(
            _srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc           = {};
        auto                            textureDescriptor = _textureBuffer->getDescriptor();
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format = DXGI_FORMAT_R32_FLOAT; // textureDescriptor.Format; Can't use D32_FLOAT
        srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip     = 0;
        srvDesc.Texture2D.MipLevels           = textureDescriptor.MipLevels;
        srvDesc.Texture2D.ResourceMinLODClamp = 0;
        device->CreateShaderResourceView(_textureBuffer->getResource().Get(), &srvDesc,
                                            hDescriptor);

        // create sampler descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC descHeapSampler = {};
        descHeapSampler.NumDescriptors             = 1;
        descHeapSampler.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        descHeapSampler.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        device->CreateDescriptorHeap(&descHeapSampler,
                                        IID_PPV_ARGS(_samplerDescriptorHeap.GetAddressOf()));

        // create sampler descriptor in the sample descriptor heap
        D3D12_SAMPLER_DESC samplerDesc;
        ZeroMemory(&samplerDesc, sizeof(D3D12_SAMPLER_DESC));
        samplerDesc.Filter     = D3D12_FILTER_ANISOTROPIC; // D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU   = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressV   = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressW   = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.MinLOD     = 0;
        samplerDesc.MaxLOD     = D3D12_FLOAT32_MAX;
        samplerDesc.MipLODBias = 0.0f;
        samplerDesc.MaxAnisotropy  = 1;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        device->CreateSampler(&samplerDesc,
                                _samplerDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

        // Viewport

        _viewPort = {0.0f, 0.0f, static_cast<float>(_width), static_cast<float>(_height),
                        0.0f, 1.0f};

        // Scissor rectangle

        _rectScissor = {0, 0, (LONG)_width, (LONG)_height};
    }
    else if (format == TextureFormat::R_FLOAT)
    {

        // Color Buffer

        D3D12_CLEAR_VALUE colorOptimizedClearValue;
        ZeroMemory(&colorOptimizedClearValue, sizeof(colorOptimizedClearValue));

        switch (format)
        {
            case TextureFormat::R_FLOAT:
            {
                colorOptimizedClearValue.Format = DXGI_FORMAT_R32_FLOAT;
                break;
            }
            /*case TextureFormat::RGBA_FLOAT:
            {
                colorOptimizedClearValue.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
                break;
            }*/
            default:
            {
                break;
            }
        };

        _format = colorOptimizedClearValue.Format;

        colorOptimizedClearValue.Color[0] = 0.0f;
        colorOptimizedClearValue.Color[1] = 0.0f;
        colorOptimizedClearValue.Color[2] = 0.0f;
        colorOptimizedClearValue.Color[3] = 1.0f;

        auto device  = DXLayer::instance()->getDevice();
        auto cmdList = DXLayer::instance()->getCmdList();

        _textureBuffer =
            new ResourceBuffer(colorOptimizedClearValue, _width, _height, cmdList, device, _name);

        D3D12_RENDER_TARGET_VIEW_DESC renderTargetViewDesc;
        ZeroMemory(&renderTargetViewDesc, sizeof(renderTargetViewDesc));
        renderTargetViewDesc.Format        = colorOptimizedClearValue.Format;
        renderTargetViewDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;

        // Create descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
        ZeroMemory(&rtvHeapDesc, sizeof(rtvHeapDesc));
        rtvHeapDesc.NumDescriptors = 1; // 1 2D texture
        rtvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        device->CreateDescriptorHeap(&rtvHeapDesc,
                                        IID_PPV_ARGS(_rtvDescriptorHeap.GetAddressOf()));

        // Backbuffer / render target
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
            _rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        auto rtvDescriptorSize =
            device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        device->CreateRenderTargetView(_textureBuffer->getResource().Get(),
                                        &renderTargetViewDesc, rtvHandle);

        // Create descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc;
        ZeroMemory(&srvHeapDesc, sizeof(srvHeapDesc));
        srvHeapDesc.NumDescriptors = 1; // 1 2D texture
        srvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        srvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        device->CreateDescriptorHeap(&srvHeapDesc,
                                        IID_PPV_ARGS(_srvDescriptorHeap.GetAddressOf()));

        // Create view of SRV for shader access
        CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(
            _srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc           = {};
        auto                            textureDescriptor = _textureBuffer->getDescriptor();
        srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Format                        = textureDescriptor.Format;
        srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip     = 0;
        srvDesc.Texture2D.MipLevels           = textureDescriptor.MipLevels;
        srvDesc.Texture2D.ResourceMinLODClamp = 0;
        device->CreateShaderResourceView(_textureBuffer->getResource().Get(), &srvDesc,
                                            hDescriptor);

        // Create descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc;
        ZeroMemory(&uavHeapDesc, sizeof(uavHeapDesc));
        uavHeapDesc.NumDescriptors = 1; // 1 2D texture
        uavHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        uavHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        device->CreateDescriptorHeap(&uavHeapDesc,
                                        IID_PPV_ARGS(_uavDescriptorHeap.GetAddressOf()));

        // Create view of UAV for shader access
        CD3DX12_CPU_DESCRIPTOR_HANDLE hUAVDescriptor(
            _uavDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format                           = textureDescriptor.Format;
        uavDesc.ViewDimension                    = D3D12_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice               = 0;
        device->CreateUnorderedAccessView(_textureBuffer->getResource().Get(), nullptr,
                                            &uavDesc, hUAVDescriptor);

        // create sampler descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC descHeapSampler = {};
        descHeapSampler.NumDescriptors             = 1;
        descHeapSampler.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        descHeapSampler.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        device->CreateDescriptorHeap(&descHeapSampler,
                                        IID_PPV_ARGS(_samplerDescriptorHeap.GetAddressOf()));

        // create sampler descriptor in the sample descriptor heap
        D3D12_SAMPLER_DESC samplerDesc;
        ZeroMemory(&samplerDesc, sizeof(D3D12_SAMPLER_DESC));
        samplerDesc.Filter     = D3D12_FILTER_ANISOTROPIC; // D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        samplerDesc.AddressU   = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressV   = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressW   = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.MinLOD     = 0;
        samplerDesc.MaxLOD     = D3D12_FLOAT32_MAX;
        samplerDesc.MipLODBias = 0.0f;
        samplerDesc.MaxAnisotropy  = 1;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
        device->CreateSampler(&samplerDesc,
                                _samplerDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

        // Viewport

        _viewPort = {0.0f, 0.0f, static_cast<float>(_width), static_cast<float>(_height),
                        0.0f, 1.0f};

        // Scissor rectangle

        _rectScissor = {0, 0, (LONG)_width, (LONG)_height};
    }
    // Generic buffers
    else if (format == TextureFormat::RGB_FLOAT)
    {
        D3D12_CLEAR_VALUE colorOptimizedClearValue;
        ZeroMemory(&colorOptimizedClearValue, sizeof(colorOptimizedClearValue));

        colorOptimizedClearValue.Format = DXGI_FORMAT_R32G32B32_FLOAT;

        _format = colorOptimizedClearValue.Format;

        colorOptimizedClearValue.Color[0] = 0.0f;
        colorOptimizedClearValue.Color[1] = 0.0f;
        colorOptimizedClearValue.Color[2] = 0.0f;

        auto device  = DXLayer::instance()->getDevice();
        auto cmdList = DXLayer::instance()->getCmdList();

        _textureBuffer =
            new ResourceBuffer(colorOptimizedClearValue, _width, _height, cmdList, device, _name);

        // Create descriptor heap
        D3D12_DESCRIPTOR_HEAP_DESC uavHeapDesc;
        ZeroMemory(&uavHeapDesc, sizeof(uavHeapDesc));
        uavHeapDesc.NumDescriptors = 1; // 1 2D texture
        uavHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        uavHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        device->CreateDescriptorHeap(&uavHeapDesc,
                                        IID_PPV_ARGS(_uavDescriptorHeap.GetAddressOf()));

        // Create view of UAV for shader access
        CD3DX12_CPU_DESCRIPTOR_HANDLE hUAVDescriptor(
            _uavDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};

        uavDesc.Format                           = DXGI_FORMAT_R32_TYPELESS;
        uavDesc.ViewDimension                    = D3D12_UAV_DIMENSION_BUFFER;
        uavDesc.Buffer.Flags                     = D3D12_BUFFER_UAV_FLAG_RAW;
        uavDesc.Buffer.FirstElement              = 0;
        uavDesc.Buffer.NumElements               = _width * 3;
        uavDesc.Buffer.StructureByteStride       = 0;
        device->CreateUnorderedAccessView(_textureBuffer->getResource().Get(), nullptr,
                                            &uavDesc, hUAVDescriptor);

        // Viewport

        _viewPort = {0.0f, 0.0f, static_cast<float>(_width), static_cast<float>(_height),
                        0.0f, 1.0f};

        // Scissor rectangle

        _rectScissor = {0, 0, (LONG)_width, (LONG)_height};
    }
}

void RenderTexture::bindTarget(D3D12_RESOURCE_STATES state)
{

    D3D12_RESOURCE_BARRIER barrierDesc;
    ZeroMemory(&barrierDesc, sizeof(barrierDesc));

    barrierDesc.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc.Transition.pResource   = _textureBuffer->getResource().Get();
    barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrierDesc.Transition.StateAfter  = state;

    auto cmdList = DXLayer::instance()->getCmdList();
    cmdList->ResourceBarrier(1, &barrierDesc);
}

void RenderTexture::unbindTarget(D3D12_RESOURCE_STATES state)
{

    D3D12_RESOURCE_BARRIER barrierDesc;
    ZeroMemory(&barrierDesc, sizeof(barrierDesc));

    barrierDesc.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc.Transition.pResource   = _textureBuffer->getResource().Get();
    barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc.Transition.StateBefore = state;
    barrierDesc.Transition.StateAfter  = D3D12_RESOURCE_STATE_COMMON;

    auto cmdList = DXLayer::instance()->getCmdList();
    cmdList->ResourceBarrier(1, &barrierDesc);
}

RenderTexture::~RenderTexture() {}

D3D12_CPU_DESCRIPTOR_HANDLE RenderTexture::getCPUHandle()
{
    return _rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_CPU_DESCRIPTOR_HANDLE RenderTexture::getUAVCPUHandle()
{
    return _uavClearDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
}

D3D12_GPU_DESCRIPTOR_HANDLE RenderTexture::getUAVGPUHandle()
{
    return _uavDescriptorHeap->GetGPUDescriptorHandleForHeapStart();
}

DXGI_FORMAT RenderTexture::getFormat() { return _format; }
