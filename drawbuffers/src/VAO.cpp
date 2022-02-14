#include "VAO.h"
#include "DXLayer.h"
#include "EngineManager.h"
#include "Model.h"
#include "FloatConverter.h"

#include "AnimatedModel.h"

VAO::VAO() {}
VAO::~VAO() {}

VAO::VAO(D3D12_VERTEX_BUFFER_VIEW vbv, D3D12_INDEX_BUFFER_VIEW ibv)
{
    _vbv = vbv;
    _ibv = ibv;
}

void VAO::setVertexContext(uint32_t context) { _vertexBufferContext = context; }

void VAO::setNormalContext(uint32_t context) { _normalBufferContext = context; }

void VAO::setTextureContext(uint32_t context) { _textureBufferContext = context; }

void VAO::setNormalDebugContext(uint32_t context) { _debugNormalBufferContext = context; }

uint32_t VAO::getVertexContext() { return _vertexBufferContext; }

uint32_t VAO::getNormalContext() { return _normalBufferContext; }

uint32_t VAO::getTextureContext() { return _textureBufferContext; }

uint32_t VAO::getNormalDebugContext() { return _debugNormalBufferContext; }

uint32_t VAO::getVertexLength() { return _vertexLength; }

D3D12_INDEX_BUFFER_VIEW VAO::getIndexBuffer() { return _ibv; }

D3D12_VERTEX_BUFFER_VIEW VAO::getVertexBuffer() { return _vbv; }

ResourceBuffer* VAO::getIndexResource() { return _indexBuffer; }

ResourceBuffer* VAO::getVertexResource() { return _vertexBuffer; }

void VAO::addTextureStride(std::pair<std::string, int> stride, int vertexStride, int indexStride)
{
    _textureStride.push_back(stride);
    _vertexAndIndexBufferStride.push_back(std::pair<int, int>(vertexStride, indexStride));
}

TextureMetaData VAO::getTextureStrides() { return _textureStride; }

VIBStrides VAO::getVertexAndIndexBufferStrides() { return _vertexAndIndexBufferStride; }

void VAO::setPrimitiveOffsetId(uint32_t id) { _primitiveOffsetId = id; }

uint32_t VAO::getPrimitiveOffsetId() { return _primitiveOffsetId; }

uint32_t VAO::getVAOContext() { return _vaoContext; }

uint32_t VAO::getVAOShadowContext() { return _vaoShadowContext; }

void VAO::createVAO(RenderBuffers* renderBuffers, ModelClass classId, AnimatedModel* model)
{
    auto      vertices            = renderBuffers->getVertices();
    auto      normals             = renderBuffers->getNormals();
    auto      textures            = renderBuffers->getTextures();
    auto      indices             = renderBuffers->getIndices();
    size_t    triBuffSize         = vertices->size();
    float*    flattenAttribs      = nullptr;
    uint32_t* flatten32BitIndexes = nullptr;
    uint16_t* flatten16BitIndexes = nullptr;

    _vertexLength = static_cast<uint32_t>(vertices->size());
    
    CompressedAttribute* compressedAttributes = nullptr;

    auto copyCommandBuffer = DXLayer::instance()->getAttributeBufferCopyCmdList();

    compressedAttributes = new CompressedAttribute[triBuffSize];

    if (renderBuffers->is32BitIndices())
    {
        flatten32BitIndexes = new uint32_t[indices->size()];

        int i = 0;
        for (auto index : *indices)
        {
            flatten32BitIndexes[i] = index;
            i++;
        }
    }
    else
    {
        flatten16BitIndexes = new uint16_t[indices->size()];

        int i = 0;
        for (auto index : *indices)
        {
            flatten16BitIndexes[i] = index;
            i++;
        }
    }

    for (int i = 0; i < vertices->size(); i++)
    {
        float* flatVert   = (*vertices)[i].getFlatBuffer();
        float* flatNormal = (*normals)[i].getFlatBuffer();
        float* flatUV     = (*textures)[i].getFlatBuffer();

        compressedAttributes[i].vertex[0] = flatVert[0];
        compressedAttributes[i].vertex[1] = flatVert[1];
        compressedAttributes[i].vertex[2] = flatVert[2];

        compressedAttributes[i].normal[0] = floatToHalfFloat(flatNormal[0]);
        compressedAttributes[i].normal[1] = floatToHalfFloat(flatNormal[1]);
        compressedAttributes[i].normal[2] = floatToHalfFloat(flatNormal[2]);

        compressedAttributes[i].uv[0] = floatToHalfFloat(flatUV[0]);
        compressedAttributes[i].uv[1] = floatToHalfFloat(flatUV[1]);

        compressedAttributes[i].padding = floatToHalfFloat(0.0);
    }

    UINT compressedAttributeByteSize = triBuffSize * sizeof(CompressedAttribute);

    UINT sizeOfIndexType    = 0;
    DXGI_FORMAT indexFormat = DXGI_FORMAT_UNKNOWN;
    if (renderBuffers->is32BitIndices())
    {
        indexFormat     = DXGI_FORMAT_R32_UINT;
        sizeOfIndexType = sizeof(uint32_t);
    }
    else
    {
        indexFormat = DXGI_FORMAT_R16_UINT;
        sizeOfIndexType = sizeof(uint16_t);
    }
    auto indexBytes = static_cast<UINT>(indices->size() * sizeOfIndexType);

    _vertexBuffer = new ResourceBuffer(compressedAttributes,
                                        compressedAttributeByteSize,
                                        copyCommandBuffer,
                                        DXLayer::instance()->getDevice());
        
    if (renderBuffers->is32BitIndices())
    {
        _indexBuffer = new ResourceBuffer(flatten32BitIndexes,
                                            indexBytes,
                                            copyCommandBuffer,
                                            DXLayer::instance()->getDevice());
    }
    else
    {
        _indexBuffer = new ResourceBuffer(flatten16BitIndexes,
                                            indexBytes,
                                            copyCommandBuffer,
                                            DXLayer::instance()->getDevice());
    }
    _vbv.BufferLocation = _vertexBuffer->getGPUAddress();
    _vbv.StrideInBytes  = sizeof(CompressedAttribute);
    _vbv.SizeInBytes    = compressedAttributeByteSize;
        
    _ibv.BufferLocation = _indexBuffer->getGPUAddress();
    _ibv.Format         = indexFormat;
    _ibv.SizeInBytes    = indexBytes;

    delete[] compressedAttributes;

    if (classId == ModelClass::AnimatedModelType)
    {
        auto                   boneIndexes         = model->getJoints();
        auto                   boneWeights         = model->getWeights();
        size_t                 boneIndexesBuffSize = boneIndexes->size();
        UINT                   boneSize            = model->getJointMatrices().size();
        auto                   bones               = model->getJointMatrices();
        UINT                   boneFloats    = static_cast<UINT>(boneSize * 16);
        float*                 flattenIndexes      = new float[boneIndexesBuffSize];
        float*                 flattenWeights      = new float[boneIndexesBuffSize];
        float*                 flattenBones        = new float[boneFloats];

        for (int i = 0; i < boneIndexesBuffSize; i++)
        {
            flattenIndexes[i]   = (*boneIndexes)[i];
            flattenWeights[i] = (*boneWeights)[i];
        }

        for (int i = 0; i < boneSize; i++)
        {
            flattenBones[i * 16 + 0] = 1.0;
            flattenBones[i * 16 + 1] = 0.0;
            flattenBones[i * 16 + 2] = 0.0;
            flattenBones[i * 16 + 3] = 0.0;
            flattenBones[i * 16 + 4] = 0.0;
            flattenBones[i * 16 + 5] = 1.0;
            flattenBones[i * 16 + 6] = 0.0;
            flattenBones[i * 16 + 7] = 0.0;
            flattenBones[i * 16 + 8] = 0.0;
            flattenBones[i * 16 + 9] = 0.0;
            flattenBones[i * 16 + 10] = 1.0;
            flattenBones[i * 16 + 11] = 0.0;
            flattenBones[i * 16 + 12] = 0.0;
            flattenBones[i * 16 + 13] = 0.0;
            flattenBones[i * 16 + 14] = 0.0;
            flattenBones[i * 16 + 15] = 1.0;
        }

        UINT byteSize = static_cast<UINT>(boneIndexesBuffSize * sizeof(float));

        copyCommandBuffer->BeginEvent(0, L"Upload joints and weights", sizeof(L"Upload joints and weights"));

        _boneWeightBuffer = new ResourceBuffer(flattenWeights, byteSize, copyCommandBuffer,
                                               DXLayer::instance()->getDevice());

        _boneIndexBuffer = new ResourceBuffer(flattenIndexes, byteSize, copyCommandBuffer,
                                              DXLayer::instance()->getDevice());

        UINT byteSizeForBones = static_cast<UINT>(boneSize * sizeof(float) * 16);
        _bonesBuffer = new ResourceBuffer(flattenBones, byteSizeForBones, copyCommandBuffer,
                                          DXLayer::instance()->getDevice());

        copyCommandBuffer->EndEvent();

        _boneWeightSRV = new D3DBuffer();
        _boneIndexSRV  = new D3DBuffer();
        _bonesSRV  = new D3DBuffer();

        _boneWeightSRV->count    = boneIndexesBuffSize;
        _boneWeightSRV->resource = _boneWeightBuffer->getResource();

        _boneIndexSRV->count    = boneIndexesBuffSize;
        _boneIndexSRV->resource = _boneIndexBuffer->getResource();

        _bonesSRV->count    = byteSizeForBones / sizeof(float);
        _bonesSRV->resource = _bonesBuffer->getResource();

        ResourceManager* resourceManager = EngineManager::getResourceManager();

        UINT descriptorIndexBoneWeights =
            resourceManager->createBufferSRV(_boneWeightSRV, _boneWeightSRV->count, 0, DXGI_FORMAT_R32_FLOAT);
        UINT descriptorIndexBoneIndexes =
            resourceManager->createBufferSRV(_boneIndexSRV, _boneIndexSRV->count, 0, DXGI_FORMAT_R32_FLOAT);

        UINT descriptorIndexBones =
            resourceManager->createBufferSRV(_bonesSRV, _bonesSRV->count, 0, DXGI_FORMAT_R32_FLOAT);
    }
}