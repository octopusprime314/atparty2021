/*
 * VBO is part of the ReBoot distribution (https://github.com/octopusprime314/ReBoot.git).
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
 *  VBO class. Vertex Buffer Object container
 */

#pragma once
#include "RenderBuffers.h"
#include "ResourceBuffer.h"
#include "Tex2.h"
#include "Vector4.h"
#include "d3dx12.h"
#include <d3d12.h>
#include <vector>
#include <wrl.h>

// Geometry
struct D3DBuffer
{
    ComPtr<ID3D12Resource>      resource;
    D3D12_CPU_DESCRIPTOR_HANDLE cpuDescriptorHandle;
    D3D12_GPU_DESCRIPTOR_HANDLE gpuDescriptorHandle;
    std::string                 nameId;
    UINT                        count;
    UINT                        offset;
    DXGI_FORMAT                 indexBufferFormat;
};

struct CompressedAttribute
{
    float    vertex[3];
    uint16_t normal[3];
    uint16_t uv[2];
    uint16_t padding;
};

using namespace Microsoft::WRL;

enum class GeometryConstruction;
enum class ModelClass;

class AnimatedModel;

using TextureMetaData = std::vector<std::pair<std::string, int>>;
using VIBStrides      = std::vector<std::pair<int, int>>;
class VAO
{
    uint32_t                 _debugNormalBufferContext;
    uint32_t                 _textureBufferContext;
    uint32_t                 _vertexBufferContext;
    uint32_t                 _normalBufferContext;
    uint32_t                 _indexBufferContext;
    uint32_t                 _primitiveOffsetId;
    uint32_t                 _vaoShadowContext;
    uint32_t                 _weightContext;
    TextureMetaData          _textureStride;
    VIBStrides               _vertexAndIndexBufferStride;
    uint32_t                 _indexContext;
    uint32_t                 _vertexLength;
    ResourceBuffer*          _vertexBuffer;
    ResourceBuffer*          _indexBuffer;
    uint32_t                 _vaoContext;
    D3D12_INDEX_BUFFER_VIEW  _ibv;
    D3D12_VERTEX_BUFFER_VIEW _vbv;

    ResourceBuffer*          _boneWeightBuffer;
    ResourceBuffer*          _boneIndexBuffer;
    ResourceBuffer*          _bonesBuffer;
    D3DBuffer*               _boneWeightSRV;
    D3DBuffer*               _boneIndexSRV;
    D3DBuffer*               _bonesSRV;

  public:
    VAO();
    ~VAO();
    VAO(D3D12_VERTEX_BUFFER_VIEW vbv, D3D12_INDEX_BUFFER_VIEW ibv);

    void addTextureStride(std::pair<std::string, int> textureStride, int vertexStride, int indexStride);

    void createVAO(RenderBuffers* renderBuffers, ModelClass classId, AnimatedModel* model);
    void createVAO(RenderBuffers* renderBuffers, int begin, int range);

    void                     setNormalDebugContext(uint32_t context);
    void                     setTextureContext(uint32_t context);
    void                     setVertexContext(uint32_t context);
    void                     setNormalContext(uint32_t context);
    void                     setPrimitiveOffsetId(uint32_t id);
    uint32_t                 getNormalDebugContext();
    uint32_t                 getPrimitiveOffsetId();
    uint32_t                 getVAOShadowContext();
    TextureMetaData          getTextureStrides();
    VIBStrides               getVertexAndIndexBufferStrides();
    uint32_t                 getTextureContext();
    ResourceBuffer*          getVertexResource();
    ResourceBuffer*          getIndexResource();
    uint32_t                 getNormalContext();
    uint32_t                 getVertexContext();
    D3D12_VERTEX_BUFFER_VIEW getVertexBuffer();
    uint32_t                 getVertexLength();
    D3D12_INDEX_BUFFER_VIEW  getIndexBuffer();
    uint32_t                 getVAOContext();
    D3DBuffer*               getBoneWeightSRV(){ return _boneWeightSRV;}
    D3DBuffer*               getBoneIndexSRV() { return _boneIndexSRV; }
    D3DBuffer*               getBonesSRV() { return _bonesSRV; }
    ResourceBuffer*          getBonesResource() { return _bonesBuffer; }
};