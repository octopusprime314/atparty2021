/*
 * Shader is part of the ReBoot distribution (https://github.com/octopusprime314/ReBoot.git).
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
 *  Shader class. Basic shader class that is responsible only for compilation and
 *  storing the gl context of the shader
 */

#pragma once
#include "VAO.h"
#include <iostream>
#include <string>

#include "AssetTexture.h"
#include "d3dx12.h"
#include <d3d12.h>
#include <wrl.h>

using namespace Microsoft::WRL;

class Entity;
class Light;
class ShaderBroker;

struct ImageData
{
    uint32_t readOnly;
    uint32_t format;
    uint32_t mipLevel;
};

const std::string SHADERS_LOCATION = "../shading/shaders/";
const std::string DXR1_1_SHADERS_LOCATION = "../shading/shaders/hlsl/cs/dxr1_1/";

// Simple shader loading class that should be derived from to create more complex shaders
class Shader
{

  protected:
    bool _fileExists(const std::string& name);

  public:
    Shader();
    Shader(const Shader& shader);
    virtual ~Shader();
    virtual void draw(int offset, int instances, int numTriangles)                   = 0;
    virtual void dispatch(int x, int y, int z)                                       = 0;
    virtual void updateData(std::string id, void* data, bool isCompute = false)      = 0;
    virtual void updateData(std::string dataName, int textureUnit, Texture* texture,
                            bool isCompute = false, bool isUAV = false)              = 0;
    virtual void updateData(std::string id, uint32_t textureUnit, Texture* texture,
                            ImageData imageInfo)                                     = 0;
    virtual void updateRTAS(std::string id, ComPtr<ID3D12DescriptorHeap> rtASDescriptorHeap,
                            D3D12_GPU_VIRTUAL_ADDRESS gpuva, bool isCompute = false) = 0;
    virtual void bindAttributes(VAO* vao, bool bindVertexBuffer)                     = 0;
    virtual void unbindAttributes()                                                  = 0;
    virtual void bind()                                                              = 0;
    virtual void unbind()                                                            = 0;
    virtual Vector4 getThreadGroupSize()                                             = 0;

    std::map<std::string, UINT> _resourceIndexes;
};