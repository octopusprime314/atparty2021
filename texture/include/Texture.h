/*
 * Texture is part of the ReBoot distribution (https://github.com/octopusprime314/ReBoot.git).
 * Copyright (c) 2017 Peter Morley.
 *
 * ReBoot is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * ReBoot is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 *  The Texture class stores openGL related texture data.
 */

#pragma once
#include "ResourceBuffer.h"
#include "d3dx12.h"
#include <d3d12.h>
#include <wrl.h>
#include <vector>
#include <iostream>
#include <string>
#include <map>

enum class TextureFormat
{
    RGBA_UNSIGNED_BYTE,
    RGBA_FLOAT,
    DEPTH32_FLOAT,
    R_FLOAT,
    R_UNSIGNED_BYTE,
    RGB_FLOAT,
    R16G16_FLOAT,
    R16_FLOAT,
    R8_UINT,
};

using namespace Microsoft::WRL;

struct TextureBlock
{
    std::vector<uint8_t>* data;
    uint32_t              width;
    uint32_t              height;
    std::string           name;
    uint32_t              sizeInBytes;
    bool                  alphaValues;
};

class Texture
{

  protected:
    Texture(); // Make the default constructor private which forces coder to allocate a Texture with
               // a string name
    uint32_t                     _textureContext;
    uint32_t                     _width;
    uint32_t                     _rowPitch;
    uint32_t                     _height;
    std::string                  _name;
    uint32_t                     _sizeInBytes;
    ResourceBuffer*              _textureBuffer;
    ComPtr<ID3D12DescriptorHeap> _uavDescriptorHeap;
    ComPtr<ID3D12DescriptorHeap> _uavClearDescriptorHeap;
    ComPtr<ID3D12DescriptorHeap> _srvDescriptorHeap;
    ComPtr<ID3D12DescriptorHeap> _samplerDescriptorHeap;
    D3D12_VIEWPORT               _viewPort;
    D3D12_RECT                   _rectScissor;
    DXGI_FORMAT                  _textureFormat;

  public:
    Texture(std::string name);
    Texture(std::string name,
            uint32_t width,
            uint32_t height);

    ~Texture();
    uint32_t                     getContext();
    uint32_t                     getWidth();
    uint32_t                     getHeight();
    std::string&                 getName();
    uint32_t                     getSizeInBytes();
    ResourceBuffer*              getResource();
    ComPtr<ID3D12DescriptorHeap> getUAVDescriptor();

    void bindToDXShader(ComPtr<ID3D12GraphicsCommandList4>& cmdList,
                        UINT                                textureBinding,
                        std::map<std::string, UINT>&        resourceBindings,
                        bool                                isCompute = false,
                        bool                                isUAV = false);
};