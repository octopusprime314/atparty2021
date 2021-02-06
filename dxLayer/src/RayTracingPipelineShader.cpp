#include "RayTracingPipelineShader.h"
#include "D3D12RaytracingHelpers.hpp"
#include "EngineManager.h"
#include "ModelBroker.h"
#include "ShaderTable.h"
#include "DXLayer.h"
#include <random>

RayTracingPipelineShader::RayTracingPipelineShader()
{
    _descriptorsAllocated              = 0;
    auto device                        = DXLayer::instance()->getDevice();
    _descriptorHeapDesc.NumDescriptors = static_cast<UINT>(MaxBLASSRVsForRayTracing);
    _descriptorHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    _descriptorHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    _descriptorHeapDesc.NodeMask       = 0;
    device->CreateDescriptorHeap(&_descriptorHeapDesc, IID_PPV_ARGS(&_descriptorHeap));

    _descriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    _unboundedTextureSrvIndex         = 0;
    _unboundedAttributeBufferSrvIndex = 0;
    _unboundedIndexBufferSrvIndex     = 0;

    createUnboundedTextureSrvDescriptorTable(MaxBLASSRVsForRayTracing);
    createUnboundedAttributeBufferSrvDescriptorTable(MaxBLASSRVsForRayTracing);
    createUnboundedIndexBufferSrvDescriptorTable(MaxBLASSRVsForRayTracing);
}

void RayTracingPipelineShader::init(ComPtr<ID3D12Device> device)
{
    _materialMapping.reserve(InitInstancesForRayTracing);
    _attributeMapping.reserve(InitInstancesForRayTracing);

    constexpr auto transformOffset              = 12; // 3x4
    UINT           instanceTransformSizeInBytes = InitInstancesForRayTracing * transformOffset;

    constexpr auto normalTransformOffset    = 9; // 3x3
    UINT instanceNormalTransformSizeInBytes = InitInstancesForRayTracing * normalTransformOffset;

    _instanceNormalMatrixTransforms       .resize(instanceNormalTransformSizeInBytes);
    _instanceWorldToObjectMatrixTransforms.resize(instanceTransformSizeInBytes);
    _instanceTransforms                   .resize(instanceTransformSizeInBytes);
    _prevInstanceTransforms               .resize(instanceTransformSizeInBytes);

    _instanceIndexToMaterialMappingGPUBuffer  = nullptr;
    _instanceIndexToAttributeMappingGPUBuffer = nullptr;
    _instanceNormalMatrixTransformsGPUBuffer  = nullptr;
    _instanceUniformMaterialMappingGPUBuffer  = nullptr;


    if (EngineManager::getGraphicsLayer() != GraphicsLayer::DX12)
    {
        auto commandList = DXLayer::instance()->getComputeCmdList();
        auto dxLayer     = DXLayer::instance();

        device->QueryInterface(IID_PPV_ARGS(&_dxrDevice));

        // Initialize command list round trip execution to CMD_LIST_NUM
        // Initialize suballocator blocks to 64 KB and limit compaction transient allocation to 16 MB
        RTCompaction::Initialize(_dxrDevice.Get(), CMD_LIST_NUM, 65536, 16777216);

        // Create descriptor heap
        ZeroMemory(&_rtASSrvHeapDesc, sizeof(_rtASSrvHeapDesc));
        _rtASSrvHeapDesc.NumDescriptors = 1;
        _rtASSrvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        _rtASSrvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        _dxrDevice->CreateDescriptorHeap(&_rtASSrvHeapDesc,
                                         IID_PPV_ARGS(_rtASDescriptorHeap.GetAddressOf()));

    }
    _doneAdding = false;
}

inline std::string BlobToUtf8(_In_ IDxcBlob* pBlob)
{
    if (pBlob == nullptr)
    {
        return std::string();
    }
    return std::string((char*)pBlob->GetBufferPointer(), pBlob->GetBufferSize());
}

void RayTracingPipelineShader::allocateUploadBuffer(ID3D12Device* pDevice, void* pData,
                                                    UINT64           datasize,
                                                    ID3D12Resource** ppResource, const wchar_t* resourceName)
{
    auto uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
    auto bufferDesc           = CD3DX12_RESOURCE_DESC::Buffer(datasize);

    pDevice->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                                     D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                     IID_PPV_ARGS(ppResource));
    if (resourceName)
    {
        (*ppResource)->SetName(resourceName);
    }
}

ComPtr<ID3D12DescriptorHeap> RayTracingPipelineShader::getRTASDescHeap()
{
    return _rtASDescriptorHeap;
}

D3D12_GPU_VIRTUAL_ADDRESS RayTracingPipelineShader::getRTASGPUVA()
{
    if (_tlasResultBuffer[_topLevelIndex] == nullptr)
    {
        return 0;
    }
    else
    {
        return _tlasResultBuffer[_topLevelIndex]->GetGPUVirtualAddress();
    }
}

ComPtr<ID3D12DescriptorHeap> RayTracingPipelineShader::getDescHeap()
{
    return _descriptorHeap;
}

RayTracingPipelineShader::TextureMapping& RayTracingPipelineShader::getSceneTextures()
{
    return _texturesMap;
}

void RayTracingPipelineShader::updateAndBindMaterialBuffer(std::map<std::string, UINT> resourceIndexes, bool isCompute)
{
    BYTE* mappedData                           = nullptr;
    _instanceIndexToMaterialMappingGPUBuffer->resource->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
    memcpy(&mappedData[0], _materialMapping.data(), sizeof(UINT) * _materialMapping.size());

    auto                  cmdList           = DXLayer::instance()->getCmdList();
    auto                  resourceBindings  = resourceIndexes;
    ID3D12DescriptorHeap* descriptorHeaps[] = {_descriptorHeap.Get()};
    cmdList->SetDescriptorHeaps(1, descriptorHeaps);
    if (isCompute)
    {
        cmdList->SetComputeRootDescriptorTable(
            resourceBindings["instanceIndexToMaterialMapping"],
            _instanceIndexToMaterialMappingGPUBuffer->gpuDescriptorHandle);
    }
    else
    {
        cmdList->SetGraphicsRootDescriptorTable(
            resourceBindings["instanceIndexToMaterialMapping"],
            _instanceIndexToMaterialMappingGPUBuffer->gpuDescriptorHandle);
    }
}

void RayTracingPipelineShader::updateAndBindAttributeBuffer(std::map<std::string, UINT> resourceIndexes, bool isCompute)
{
    BYTE* mappedData                           = nullptr;
    _instanceIndexToAttributeMappingGPUBuffer->resource->Map(0, nullptr,
                                                             reinterpret_cast<void**>(&mappedData));
    memcpy(&mappedData[0], _attributeMapping.data(), sizeof(UINT) * _attributeMapping.size());

    auto                  cmdList           = DXLayer::instance()->getCmdList();
    auto                  resourceBindings  = resourceIndexes;
    ID3D12DescriptorHeap* descriptorHeaps[] = {_descriptorHeap.Get()};
    cmdList->SetDescriptorHeaps(1, descriptorHeaps);
    if (isCompute)
    {
        cmdList->SetComputeRootDescriptorTable(
            resourceBindings["instanceIndexToAttributesMapping"],
            _instanceIndexToAttributeMappingGPUBuffer->gpuDescriptorHandle);
    }
    else
    {
        cmdList->SetGraphicsRootDescriptorTable(
            resourceBindings["instanceIndexToAttributesMapping"],
            _instanceIndexToAttributeMappingGPUBuffer->gpuDescriptorHandle);
    }
}

void RayTracingPipelineShader::updateAndBindUniformMaterialBuffer(std::map<std::string, UINT> resourceIndexes, bool isCompute)
{
    BYTE* mappedData                           = nullptr;
    _instanceUniformMaterialMappingGPUBuffer->resource->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));

    std::vector<UniformMaterial> uniformMaterialBuffer;
    for(auto& uniformMaterials : _uniformMaterialMap)
    {
        if (uniformMaterials.second.size() == 0)
        {
            uniformMaterialBuffer.push_back(UniformMaterial());
        }
        else
        {
            for (auto& uniformMaterial : uniformMaterials.second)
            {
                uniformMaterialBuffer.push_back(uniformMaterial);
            }
        }
    }

    memcpy(&mappedData[0], uniformMaterialBuffer.data(), sizeof(UniformMaterial) * uniformMaterialBuffer.size());

    auto                  cmdList           = DXLayer::instance()->getCmdList();
    auto                  resourceBindings  = resourceIndexes;
    ID3D12DescriptorHeap* descriptorHeaps[] = {_descriptorHeap.Get()};
    cmdList->SetDescriptorHeaps(1, descriptorHeaps);
    if (isCompute)
    {
        cmdList->SetComputeRootDescriptorTable(
            resourceBindings["uniformMaterials"],
            _instanceUniformMaterialMappingGPUBuffer->gpuDescriptorHandle);
    }
    else
    {
        cmdList->SetGraphicsRootDescriptorTable(
            resourceBindings["uniformMaterials"],
            _instanceUniformMaterialMappingGPUBuffer->gpuDescriptorHandle);
    }
}

void RayTracingPipelineShader::updateAndBindNormalMatrixBuffer(std::map<std::string, UINT> resourceIndexes, bool isCompute)
{
    BYTE* mappedData                           = nullptr;
    _instanceNormalMatrixTransformsGPUBuffer->resource->Map(0, nullptr,
                                                             reinterpret_cast<void**>(&mappedData));
    memcpy(&mappedData[0], _instanceNormalMatrixTransforms.data(), sizeof(float) * _instanceNormalMatrixTransforms.size());

    auto                  cmdList           = DXLayer::instance()->getCmdList();
    auto                  resourceBindings  = resourceIndexes;
    ID3D12DescriptorHeap* descriptorHeaps[] = {_descriptorHeap.Get()};
    cmdList->SetDescriptorHeaps(1, descriptorHeaps);
    if (isCompute)
    {
        cmdList->SetComputeRootDescriptorTable(
            resourceBindings["instanceNormalMatrixTransforms"],
            _instanceNormalMatrixTransformsGPUBuffer->gpuDescriptorHandle);
    }
    else
    {
        cmdList->SetGraphicsRootDescriptorTable(
            resourceBindings["instanceNormalMatrixTransforms"],
            _instanceNormalMatrixTransformsGPUBuffer->gpuDescriptorHandle);
    }
}

void RayTracingPipelineShader::buildGeometry(Entity* entity)
{
    auto textureBroker = TextureBroker::instance();

    // Get required sizes for an acceleration structure.
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;

    if (_useCompaction)
    {
        buildFlags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottomLevelInputs = {};
    bottomLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

    if (entity->isAnimated() == false)
    {
        bottomLevelInputs.Flags =
            buildFlags | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    }
    else
    {
        bottomLevelInputs.Flags =
            buildFlags | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE |
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    }

    auto vertexAndBufferStrides = (*entity->getModel()->getVAO())[0]->getVertexAndIndexBufferStrides();

    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>* staticGeometryDesc = new std::vector<D3D12_RAYTRACING_GEOMETRY_DESC>();

    int geometryIndex = 0;

    for (int i = 0; i < vertexAndBufferStrides.size(); i++)
    {
        auto indexDesc =
            (*entity->getFrustumVAO())[0]->getIndexResource()->getResource()->GetDesc();
        auto vertexDesc =
            (*entity->getFrustumVAO())[0]->getVertexResource()->getResource()->GetDesc();

        auto indexFormat = entity->getModel()->getRenderBuffers()->is32BitIndices() ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        auto indexTypeSize = 0;
        if (indexFormat == DXGI_FORMAT_R32_UINT)
        {
            indexTypeSize = 4;
        }
        else
        {
            indexTypeSize = 2;
        }

        int indexCount  = vertexAndBufferStrides[i].second;
        int vertexCount = vertexAndBufferStrides[i].first;

        int indexOffset  = 0;
        int vertexOffset = 0;

        if (i > 0)
        {
            indexCount  = vertexAndBufferStrides[i].second - vertexAndBufferStrides[i - 1].second;
            vertexCount = vertexAndBufferStrides[i].first - vertexAndBufferStrides[i - 1].first;
        }

        auto indexGPUAddress = (*entity->getFrustumVAO())[0]
                                   ->getIndexResource()
                                   ->getResource()
                                   ->GetGPUVirtualAddress() +
                               (indexOffset * indexTypeSize);

        auto vertexGPUAddress = (*entity->getFrustumVAO())[0]
                                    ->getVertexResource()
                                    ->getResource()
                                    ->GetGPUVirtualAddress() + 
                                 vertexOffset;

        
        auto materialTransmittance = entity->getModel()->getMaterial(i).uniformMaterial.transmittance;

        Model* bufferModel = entity->getModel();

        // Initialize the map values
        _vertexBufferMap[bufferModel] = RayTracingPipelineShader::D3DBufferDescriptorHeapMap(std::vector<D3DBuffer*>(), 0);
        _indexBufferMap[bufferModel] = RayTracingPipelineShader::D3DBufferDescriptorHeapMap(std::vector<D3DBuffer*>(), 0);
        
        _indexBufferMap[bufferModel].first.push_back(new D3DBuffer());
        _vertexBufferMap[bufferModel].first.push_back(new D3DBuffer());

        auto indexBuffer  = _indexBufferMap[bufferModel].first.back();
        auto vertexBuffer = _vertexBufferMap[bufferModel].first.back();

        indexBuffer->indexBufferFormat = indexFormat;
        vertexBuffer->count            = static_cast<UINT>(vertexCount);
        indexBuffer->count             = static_cast<UINT>(indexCount);
        indexBuffer->resource          = (*entity->getFrustumVAO())[0]->getIndexResource()->getResource();

        vertexBuffer->resource = (*entity->getFrustumVAO())[0]->getVertexResource()->getResource();
        vertexBuffer->nameId   = entity->getModel()->getName();

        vertexBuffer->offset = vertexOffset;
        indexBuffer->offset  = indexOffset;

        UINT vertexBufferDescriptorIndex = addSRVToUnboundedAttributeBufferDescriptorTable(
            vertexBuffer, vertexBuffer->count, vertexBuffer->offset);

        UINT indexBufferDescriptorIndex = addSRVToUnboundedIndexBufferDescriptorTable(
            indexBuffer, indexBuffer->count, indexBuffer->offset);

        _vertexBufferMap[bufferModel].second = vertexBufferDescriptorIndex;
        _indexBufferMap[bufferModel].second  = indexBufferDescriptorIndex;

        // lock in the same index for attribute buffers
        _uniformMaterialMap[vertexBufferDescriptorIndex] = std::vector<UniformMaterial>();
        _uniformMaterialMap[vertexBufferDescriptorIndex].push_back(bufferModel->getMaterialNames()[i].uniformMaterial);

        auto materialNames = entity->getModel()->getMaterialNames();

        UINT baseModelDescriptorIndex = -1;
        for (auto textureNames : materialNames)
        {
            // Initialize the map values
            _texturesMap[bufferModel] = RayTracingPipelineShader::TextureDescriptorHeapMap(std::vector<AssetTexture*>(), 0);

            // Grab the first descriptor heap index into the material's resources
            AssetTexture* texture = textureBroker->getTexture(textureNames.albedo);
            UINT descriptorIndex = addSRVToUnboundedTextureDescriptorTable(texture);

            // For models with more than one material only insert the base offset of all material
            // resources
            if (baseModelDescriptorIndex == -1)
            {
                _texturesMap[bufferModel].second = descriptorIndex;
            }

            // Build each SRV into the descriptor heap
            _texturesMap[bufferModel].first.push_back(texture);
            texture = textureBroker->getTexture(textureNames.normal);
            addSRVToUnboundedTextureDescriptorTable(texture);
            _texturesMap[bufferModel].first.push_back(texture);
            texture = textureBroker->getTexture(textureNames.roughnessMetallic);
            addSRVToUnboundedTextureDescriptorTable(texture);
            _texturesMap[bufferModel].first.push_back(texture);
        }

        if (EngineManager::getGraphicsLayer() != GraphicsLayer::DX12)
        {
            int staticGeomIndex = staticGeometryDesc->size();
            staticGeometryDesc->push_back(D3D12_RAYTRACING_GEOMETRY_DESC());

            if (materialTransmittance > 0.0f)
            {
                (*staticGeometryDesc)[staticGeomIndex].Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
            }
            else
            {
                (*staticGeometryDesc)[staticGeomIndex].Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
            }

            (*staticGeometryDesc)[staticGeomIndex].Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
            (*staticGeometryDesc)[staticGeomIndex].Triangles.IndexBuffer = indexGPUAddress;
            (*staticGeometryDesc)[staticGeomIndex].Triangles.IndexCount = static_cast<UINT>(indexCount);

            (*staticGeometryDesc)[staticGeomIndex].Triangles.IndexFormat = indexFormat;

            (*staticGeometryDesc)[staticGeomIndex].Triangles.Transform3x4 = 0;
            (*staticGeometryDesc)[staticGeomIndex].Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
            (*staticGeometryDesc)[staticGeomIndex].Triangles.VertexCount  = static_cast<UINT>(vertexCount);
            (*staticGeometryDesc)[staticGeomIndex].Triangles.VertexBuffer.StartAddress = vertexGPUAddress;
            (*staticGeometryDesc)[staticGeomIndex].Triangles.VertexBuffer.StrideInBytes = sizeof(CompressedAttribute);
        }
    }

    if (EngineManager::getGraphicsLayer() != GraphicsLayer::DX12)
    {
        bottomLevelInputs.Type     = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
        bottomLevelInputs.NumDescs = static_cast<UINT>(staticGeometryDesc->size());
        bottomLevelInputs.pGeometryDescs = staticGeometryDesc->data();

        _bottomLevelBuildDescs.push_back(bottomLevelInputs);
        _bottomLevelBuildModels.push_back(entity->getModel());
    }
}

void RayTracingPipelineShader::_updateTLASData(int                                          tlasCount,
                                               std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescriptionCPUBuffer)
{
    if (tlasCount == 0)
    {
        return;
    }
    auto dxLayer = DXLayer::instance();
    auto commandList = dxLayer->usingAsyncCompute() ? DXLayer::instance()->getComputeCmdList()
                                                    : DXLayer::instance()->getCmdList();

    CD3DX12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC    topLevelBuildDesc = {};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& topLevelInputs = topLevelBuildDesc.Inputs;
    topLevelInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    topLevelInputs.Flags    = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    topLevelInputs.NumDescs = static_cast<UINT>(tlasCount);
    topLevelInputs.pGeometryDescs = nullptr;
    topLevelInputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};

    _dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs,
                                                               &topLevelPrebuildInfo);
    bool newTopLevelAllocation = false;
    if ((_tlasScratchBuffer[_topLevelIndex] == nullptr) || 
        (_tlasResultBuffer[_topLevelIndex] == nullptr) ||
        (_instanceDescriptionGPUBuffer[_topLevelIndex] == nullptr))
    {
        newTopLevelAllocation = true;
    }
    else if ((topLevelPrebuildInfo.ScratchDataSizeInBytes >_tlasScratchBuffer[_topLevelIndex]->GetDesc().Width) ||
             (topLevelPrebuildInfo.ResultDataMaxSizeInBytes > _tlasResultBuffer[_topLevelIndex]->GetDesc().Width)||
             (_instanceDescriptionGPUBuffer[_topLevelIndex]->GetDesc().Width < (sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * tlasCount)))
    {
        newTopLevelAllocation = true;
        _topLevelIndex++;

        _topLevelIndex %= CMD_LIST_NUM;
    }

    if (newTopLevelAllocation)
    {
        auto tlasScratchBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(topLevelPrebuildInfo.ScratchDataSizeInBytes * TlasAllocationMultiplier,
                                                                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        _dxrDevice->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
                                            &tlasScratchBufferDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr,
                                            IID_PPV_ARGS(&_tlasScratchBuffer[_topLevelIndex]));

        auto tlasBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(topLevelPrebuildInfo.ResultDataMaxSizeInBytes * TlasAllocationMultiplier,
                                                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        _dxrDevice->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
                                            &tlasBufferDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                                            nullptr, IID_PPV_ARGS(&_tlasResultBuffer[_topLevelIndex]));

        // Create view of SRV for shader access
        CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(_rtASDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        ZeroMemory(&_rtASSrvDesc, sizeof(_rtASSrvDesc));
        _rtASSrvDesc.Shader4ComponentMapping                  = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        _rtASSrvDesc.ViewDimension                            = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        _rtASSrvDesc.RaytracingAccelerationStructure.Location = _tlasResultBuffer[_topLevelIndex]->GetGPUVirtualAddress();
        _dxrDevice->CreateShaderResourceView(nullptr, &_rtASSrvDesc, hDescriptor);

        allocateUploadBuffer(_dxrDevice.Get(), nullptr,
                                sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * tlasCount *
                                    TlasAllocationMultiplier,
                                &_instanceDescriptionGPUBuffer[_topLevelIndex], L"instanceDescriptionGPUBuffer");
    }

    BYTE*         mappedData = nullptr;
    CD3DX12_RANGE readRange(0, 0);
    _instanceDescriptionGPUBuffer[_topLevelIndex]->Map(0, &readRange, reinterpret_cast<void**>(&mappedData));

    memcpy(&mappedData[0], instanceDescriptionCPUBuffer.data(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * tlasCount);

    // Top Level Acceleration Structure desc
    topLevelBuildDesc.DestAccelerationStructureData =
        _tlasResultBuffer[_topLevelIndex]->GetGPUVirtualAddress();
    topLevelBuildDesc.ScratchAccelerationStructureData =
        _tlasScratchBuffer[_topLevelIndex]->GetGPUVirtualAddress();
    topLevelBuildDesc.Inputs.InstanceDescs = _instanceDescriptionGPUBuffer[_topLevelIndex]->GetGPUVirtualAddress();

    commandList->BuildRaytracingAccelerationStructure(&topLevelBuildDesc, 0, nullptr);
    commandList->ResourceBarrier(
        1, &CD3DX12_RESOURCE_BARRIER::UAV(_tlasResultBuffer[_topLevelIndex].Get()));
}

void RayTracingPipelineShader::_updateInstanceData()
{
    auto entityList = EngineManager::instance()->getEntityList();

    bool newInstanceMappingAllocation = false;
    if ((_instanceIndexToMaterialMappingGPUBuffer == nullptr) ||
        (_instanceIndexToAttributeMappingGPUBuffer == nullptr) ||
        (_instanceNormalMatrixTransformsGPUBuffer == nullptr) ||
        (_instanceUniformMaterialMappingGPUBuffer == nullptr))
    {
        _instanceIndexToMaterialMappingGPUBuffer  = new D3DBuffer();
        _instanceIndexToAttributeMappingGPUBuffer = new D3DBuffer();
        _instanceNormalMatrixTransformsGPUBuffer  = new D3DBuffer();
        _instanceUniformMaterialMappingGPUBuffer  = new D3DBuffer();

        newInstanceMappingAllocation = true;
    }
    else if (_instanceIndexToMaterialMappingGPUBuffer->count < entityList->size())
    {
        _instanceMappingIndex++;
        _instanceMappingIndex %= CMD_LIST_NUM;

        newInstanceMappingAllocation = true;
    }

    if (newInstanceMappingAllocation)
    {
        auto newInstanceSize = (_instanceIndexToMaterialMappingGPUBuffer->count == 0)
                                   ? entityList->size()
                                   : _instanceIndexToMaterialMappingGPUBuffer->count * TlasAllocationMultiplier;

        constexpr auto transformOffset                    = 12; // 3x4
        UINT           instanceTransformSizeInBytes       = newInstanceSize * transformOffset;
        constexpr auto normalTransformOffset              = 9; // 3x3
        UINT           instanceNormalTransformSizeInBytes = newInstanceSize * normalTransformOffset;

        _instanceNormalMatrixTransforms.resize(instanceNormalTransformSizeInBytes);
        _instanceWorldToObjectMatrixTransforms.resize(instanceTransformSizeInBytes);
        _instanceTransforms.resize(instanceTransformSizeInBytes);
        _prevInstanceTransforms.resize(instanceTransformSizeInBytes);

        allocateUploadBuffer(DXLayer::instance()->getDevice().Get(), nullptr,
                             sizeof(UINT) * newInstanceSize,
                             &_instanceIndexToMaterialMappingUpload[_instanceMappingIndex],
                             L"instanceIndexToMaterial");

        _instanceIndexToMaterialMappingGPUBuffer->resource =
            _instanceIndexToMaterialMappingUpload[_instanceMappingIndex];
        _instanceIndexToMaterialMappingGPUBuffer->count = newInstanceSize;

        createBufferSRV(_instanceIndexToMaterialMappingGPUBuffer, newInstanceSize, 0, DXGI_FORMAT_R32_UINT);

        allocateUploadBuffer(DXLayer::instance()->getDevice().Get(), nullptr,
                             sizeof(UINT) * newInstanceSize,
                             &_instanceIndexToAttributeMappingUpload[_instanceMappingIndex],
                             L"instanceIndexToAttribute");

        _instanceIndexToAttributeMappingGPUBuffer->resource =
            _instanceIndexToAttributeMappingUpload[_instanceMappingIndex];
        _instanceIndexToAttributeMappingGPUBuffer->count = newInstanceSize;

        createBufferSRV(_instanceIndexToAttributeMappingGPUBuffer, newInstanceSize, 0, DXGI_FORMAT_R32_UINT);

        allocateUploadBuffer(DXLayer::instance()->getDevice().Get(), nullptr,
                             sizeof(UniformMaterial) * newInstanceSize,
                             &_instanceUniformMaterialMappingUpload[_instanceMappingIndex],
                             L"uniformMaterials");

        _instanceUniformMaterialMappingGPUBuffer->resource = _instanceUniformMaterialMappingUpload[_instanceMappingIndex];
        _instanceUniformMaterialMappingGPUBuffer->count = newInstanceSize;

        createBufferSRV(_instanceUniformMaterialMappingGPUBuffer, newInstanceSize, sizeof(UniformMaterial), DXGI_FORMAT_UNKNOWN);

        allocateUploadBuffer(
            DXLayer::instance()->getDevice().Get(), nullptr, sizeof(float) * 9 * newInstanceSize,
            &_instanceNormalMatrixTransformsUpload[_instanceMappingIndex], L"instanceNormals");

        _instanceNormalMatrixTransformsGPUBuffer->resource =
            _instanceNormalMatrixTransformsUpload[_instanceMappingIndex];
        _instanceNormalMatrixTransformsGPUBuffer->count = newInstanceSize;

        createBufferSRV(_instanceNormalMatrixTransformsGPUBuffer, 9 * newInstanceSize, 0, DXGI_FORMAT_R32_FLOAT);
    }
}

void RayTracingPipelineShader::_updateGeometryData()
{
    auto    viewEventDistributor = EngineManager::instance()->getViewManager();
    auto    entityList           = EngineManager::instance()->getEntityList();

    auto    cameraView = viewEventDistributor->getView();
    Vector4 cameraPos  = viewEventDistributor->getCameraPos();
    cameraPos.getFlatBuffer()[2] = -cameraPos.getFlatBuffer()[2];

    auto    dxLayer              = DXLayer::instance();
    auto    commandList          = dxLayer->usingAsyncCompute() ? DXLayer::instance()->getComputeCmdList()
                                                                : DXLayer::instance()->getCmdList();

    // random floats between -1.0 - 1.0
    std::random_device               rd;
    std::mt19937                     generator(rd());
    std::uniform_real_distribution<> randomFloats(-1.0, 1.0);

    if (RandomInsertAndRemoveEntities)
    {
        if (entityList->size() < MaxInstancesForRayTracing)
        {
            int  modelCount = ModelBroker::instance()->getModelNames().size();
            auto modelNames = ModelBroker::instance()->getModelNames();

            const float radiusRange     = 50.0;
            float       modelScaleRange = 50.0;
            const float addEntityRange  = 10.0;

            SceneEntity sceneEntity;
            int         entitiesToAdd = (((randomFloats(generator) + 1.0) / 2.0) * addEntityRange);
            if (entityList->size() == 0 && entitiesToAdd == 0)
            {
                entitiesToAdd++;
            }

            for (int i = 0; i < entitiesToAdd; i++)
            {
                int randomCollection  = ((randomFloats(generator) + 1.0) / 2.0) * modelCount;
                sceneEntity.modelname = modelNames[randomCollection];
                sceneEntity.name      = "";

                modelScaleRange = 50.0;
                if (sceneEntity.modelname.find("DUNGEON") != std::string::npos)
                {
                    modelScaleRange = 0.25;
                }
                if (sceneEntity.modelname.find("LANTERN") != std::string::npos)
                {
                    modelScaleRange = 1.5;
                }
                if (sceneEntity.modelname.find("AVACADO") != std::string::npos)
                {
                    modelScaleRange = 500.0;
                }

                Vector4 randomLocation(randomFloats(generator) * radiusRange * 10.0,
                                       randomFloats(generator) * radiusRange * 10.0,
                                       randomFloats(generator) * radiusRange * 10.0);

                randomLocation = randomLocation - cameraPos;

                Vector4 randomRotation(randomFloats(generator) * 360.0, randomFloats(generator) * 360.0,
                                       randomFloats(generator) * 360.0);

                float   scaleForAll = ((randomFloats(generator) + 1.0) / 2.0) * modelScaleRange;
                Vector4 randomScale(scaleForAll, scaleForAll, scaleForAll);

                sceneEntity.position = randomLocation;
                sceneEntity.rotation = randomRotation;
                sceneEntity.scale =
                    Vector4(modelScaleRange, modelScaleRange, modelScaleRange) + randomScale;

                if (sceneEntity.modelname.find("DUNGEON") != std::string::npos ||
                    sceneEntity.modelname.find("LANTERN") != std::string::npos)
                {
                    modelScaleRange = 50.0;
                }

                EngineManager::instance()->addEntity(sceneEntity);
            }
        }
        else
        {
            _doneAdding = true;
        }
    }

    float cometTailRadius = 20000;
    bool  newGeometryBuilds   = false;

    std::map<Model*, int> modelCountsInEntities;
    for (auto entity = entityList->begin(); entity != entityList->end();)
    {
        Vector4 entityPosition = ((*entity)->getWorldSpaceTransform() * Vector4(0, 0, 0, 1));
        if (RandomInsertAndRemoveEntities)
        {
            if ((*entity)->getHasEntered() == false)
            {
                Vector4 rotation(randomFloats(generator) * 360.0, randomFloats(generator) * 360.0,
                                 randomFloats(generator) * 360.0);

                (*entity)->entranceWaypoint(Vector4(entityPosition.getx(),
                                                    entityPosition.gety() - 500.0,
                                                    entityPosition.getz()),
                                            rotation, 4000);
            }
        }

        // Does a vertex buffer exist for this blas
        bool isNewGeometry = _vertexBufferMap.find((*entity)->getModel()) == _vertexBufferMap.end();

        if (isNewGeometry)
        {
            buildGeometry((*entity));
            newGeometryBuilds = true;
        }

        if (RandomInsertAndRemoveEntities)
        {
            modelCountsInEntities[(*entity)->getModel()]++;

            auto distance = (cameraPos + entityPosition).getMagnitude();
            if (distance > cometTailRadius)
            {
                modelCountsInEntities[(*entity)->getModel()]--;
                auto tempEntity = *entity;
                entity = entityList->erase(entity);
                delete tempEntity;
            }
            else
            {
                ++entity;
            }
        }
        else
        {
            ++entity;
        }
    }

    std::map<Model*, int> modelsToRemove;
    for (auto model : modelCountsInEntities)
    {
        if (model.second == 0)
        {
            modelsToRemove[model.first] = 1;
        }
    }

    bool blasRemoved = false;
    for (auto model : modelCountsInEntities)
    {
        if (model.second == 0 && _blasMap.find(model.first) != _blasMap.end())
        {

            removeSRVToUnboundedTextureDescriptorTable(_texturesMap[model.first].second);
            removeSRVToUnboundedAttributeBufferDescriptorTable(_vertexBufferMap[model.first].second);
            removeSRVToUnboundedIndexBufferDescriptorTable(_indexBufferMap[model.first].second);

            // Clear out material slot and use later
            auto attributeSlot = _vertexBufferMap.find(model.first)->second.second;
            _uniformMaterialMap[attributeSlot].clear();

            _vertexBufferMap.erase(_vertexBufferMap.find(model.first));
            _indexBufferMap.erase(_indexBufferMap.find(model.first));
            _texturesMap.erase(_texturesMap.find(model.first));
            // Deallocate the memory first
            RTCompaction::RemoveAccelerationStructures(&_blasMap[model.first], 1);
            // Remove the blas entry from the list
            _blasMap.erase(model.first);

            blasRemoved = true;
        }
    }

    if (EngineManager::getGraphicsLayer() != GraphicsLayer::DX12)
    {
        if (newGeometryBuilds)
        {
            RTCompaction::ASBuffers* buffers = RTCompaction::BuildAccelerationStructures(
                _dxrDevice.Get(), commandList.Get(), _bottomLevelBuildDescs.data(),
                _bottomLevelBuildDescs.size());

            for (int asBufferIndex = 0; asBufferIndex < _bottomLevelBuildModels.size(); asBufferIndex++)
            {
                _blasMap[_bottomLevelBuildModels[asBufferIndex]] = &buffers[asBufferIndex];
            }

            commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(nullptr));
        }
    }
}


void RayTracingPipelineShader::buildAccelerationStructures()
{
    auto entityList           = EngineManager::instance()->getEntityList();
    auto dxLayer              = DXLayer::instance();
    auto commandList          = dxLayer->usingAsyncCompute() ? DXLayer::instance()->getComputeCmdList()
                                                             : DXLayer::instance()->getCmdList();

    if (EngineManager::getGraphicsLayer() != GraphicsLayer::DX12)
    {
        // Increment next frame and let the library internally manage compaction and releasing memory
        RTCompaction::NextFrame(_dxrDevice.Get(), commandList.Get());
    }

    constexpr int transformOffset          = sizeof(float) * 12;
    constexpr int normalTransformOffset    = sizeof(float) * 9;

    _attributeMapping.clear();
    _materialMapping.clear();
    _bottomLevelBuildDescs.clear();
    _bottomLevelBuildModels.clear();

    _updateGeometryData();

    _updateInstanceData();

    // Copy over all the previous instance transforms for motion vectors
    memcpy(_prevInstanceTransforms.data(), _instanceTransforms.data(), sizeof(float) * 12 * entityList->size());

    // Create an instance desc for the dynamic and static bottom levels
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescriptionCPUBuffer(entityList->size());

    int instanceDescIndex = 0;
    for (auto entity : *entityList)
    {
        auto worldSpaceTransform = entity->getWorldSpaceTransform();
        memcpy(&_instanceTransforms[instanceDescIndex * (transformOffset / sizeof(float))],
                worldSpaceTransform.getFlatBuffer(), sizeof(float) * 12);

        auto worldToObjectMatrix = entity->getWorldSpaceTransform().inverse();
            memcpy(&_instanceWorldToObjectMatrixTransforms[instanceDescIndex * (transformOffset / sizeof(float))],
                worldToObjectMatrix.getFlatBuffer(), sizeof(float) * 12);

        // Update normal transforms which is for all geometry in both
        auto normalMatrix = worldToObjectMatrix.transpose();
        auto offset       = instanceDescIndex * (normalTransformOffset / sizeof(float));

        // Pointer math works in offset of the type
        memcpy(&_instanceNormalMatrixTransforms[offset + 0], normalMatrix.getFlatBuffer() + 0, normalTransformOffset / 3);
        memcpy(&_instanceNormalMatrixTransforms[offset + 3], normalMatrix.getFlatBuffer() + 4, normalTransformOffset / 3);
        memcpy(&_instanceNormalMatrixTransforms[offset + 6], normalMatrix.getFlatBuffer() + 8, normalTransformOffset / 3);


        if (EngineManager::getGraphicsLayer() != GraphicsLayer::DX12)
        {
            instanceDescriptionCPUBuffer.push_back(D3D12_RAYTRACING_INSTANCE_DESC());
            memcpy(&instanceDescriptionCPUBuffer[instanceDescIndex].Transform,
                    &_instanceTransforms[instanceDescIndex * (transformOffset / sizeof(float))],
                    sizeof(float) * 12);

            // do not overwrite geometry flags for bottom levels, this caused the non opaque and
            // opaqueness of bottom levels to be random
            instanceDescriptionCPUBuffer[instanceDescIndex].Flags                               = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            instanceDescriptionCPUBuffer[instanceDescIndex].InstanceMask                        = 1;
            instanceDescriptionCPUBuffer[instanceDescIndex].InstanceID                          = 0;
            instanceDescriptionCPUBuffer[instanceDescIndex].InstanceContributionToHitGroupIndex = 0;
            instanceDescriptionCPUBuffer[instanceDescIndex].AccelerationStructure               = _blasMap[entity->getModel()]->GetASBuffer();
        }

        instanceDescIndex++;

        _materialMapping.push_back(_texturesMap[entity->getModel()].second);
        _attributeMapping.push_back(_vertexBufferMap[entity->getModel()].second);
    }

    if (EngineManager::getGraphicsLayer() != GraphicsLayer::DX12)
    {
        _updateTLASData(instanceDescIndex, instanceDescriptionCPUBuffer);
    }
}

void RayTracingPipelineShader::resetUnboundedTextureDescriptorTable()                    { _unboundedTextureSrvIndex = 0; }
void RayTracingPipelineShader::resetUnboundedAttributeBufferDescriptorTable()            { _unboundedAttributeBufferSrvIndex = 0; }
void RayTracingPipelineShader::resetUnboundedIndexBufferDescriptorTable()                { _unboundedIndexBufferSrvIndex = 0; }
RayTracingPipelineShader::AttributeMapping& RayTracingPipelineShader::getVertexBuffers() { return _vertexBufferMap; }
RayTracingPipelineShader::IndexBufferMapping& RayTracingPipelineShader::getIndexBuffers(){ return _indexBufferMap; }
float* RayTracingPipelineShader::getInstanceNormalTransforms()                           { return _instanceNormalMatrixTransforms.data(); }
float* RayTracingPipelineShader::getWorldToObjectTransforms()                            { return _instanceWorldToObjectMatrixTransforms.data(); }
float* RayTracingPipelineShader::getPrevInstanceTransforms()                             { return _prevInstanceTransforms.data(); }
int    RayTracingPipelineShader::getBLASCount()                                          { return _blasMap.size(); }

void RayTracingPipelineShader::createUnboundedTextureSrvDescriptorTable(UINT descriptorTableEntries)
{
    auto device = DXLayer::instance()->getDevice();

    // Create descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc;
    ZeroMemory(&srvHeapDesc, sizeof(srvHeapDesc));
    srvHeapDesc.NumDescriptors = descriptorTableEntries; // descriptorTableEntries 2D textures
    srvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&srvHeapDesc,
                                 IID_PPV_ARGS(_unboundedTextureSrvDescriptorHeap.GetAddressOf()));
}

void RayTracingPipelineShader::createUnboundedAttributeBufferSrvDescriptorTable(UINT descriptorTableEntries)
{
    auto device = DXLayer::instance()->getDevice();

    // Create descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc;
    ZeroMemory(&srvHeapDesc, sizeof(srvHeapDesc));
    srvHeapDesc.NumDescriptors =
        descriptorTableEntries; // descriptorTableEntries structured buffers
    srvHeapDesc.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&srvHeapDesc,
                                 IID_PPV_ARGS(_unboundedAttributeBufferSrvDescriptorHeap.GetAddressOf()));
}

void RayTracingPipelineShader::createUnboundedIndexBufferSrvDescriptorTable(UINT descriptorTableEntries)
{
    auto device = DXLayer::instance()->getDevice();

    // Create descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc;
    ZeroMemory(&srvHeapDesc, sizeof(srvHeapDesc));
    srvHeapDesc.NumDescriptors =
        descriptorTableEntries; // descriptorTableEntries structured buffers
    srvHeapDesc.Type  = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&srvHeapDesc,
                                 IID_PPV_ARGS(_unboundedIndexBufferSrvDescriptorHeap.GetAddressOf()));
}

UINT RayTracingPipelineShader::addSRVToUnboundedTextureDescriptorTable(Texture* texture)
{
    auto device = DXLayer::instance()->getDevice();
    UINT descriptorHeapIndex = -1;
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor;

    if (_reusableMaterialSRVIndices.empty() == false)
    {
        UINT reusedSrvIndex = _reusableMaterialSRVIndices.front();
        _reusableMaterialSRVIndices.pop();
        // Create view of SRV for shader access
        hDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(_unboundedTextureSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                                                    reusedSrvIndex,
                                                    _descriptorSize);

        descriptorHeapIndex = reusedSrvIndex;
    }
    else
    {
        // Create view of SRV for shader access
        hDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(_unboundedTextureSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
                                                    _unboundedTextureSrvIndex,
                                                    _descriptorSize);

        descriptorHeapIndex = _unboundedTextureSrvIndex;
        _unboundedTextureSrvIndex++;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

    auto textureDescriptor = texture->getResource()->getDescriptor();

    srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format                        = textureDescriptor.Format;
    srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip     = 0;
    srvDesc.Texture2D.MipLevels           = textureDescriptor.MipLevels;
    srvDesc.Texture2D.ResourceMinLODClamp = 0;
    device->CreateShaderResourceView(texture->getResource()->getResource().Get(), &srvDesc, hDescriptor);

    return descriptorHeapIndex;
}

void RayTracingPipelineShader::removeSRVToUnboundedTextureDescriptorTable(UINT descriptorHeapIndex)
{
    _reusableMaterialSRVIndices.push(descriptorHeapIndex);
    _reusableMaterialSRVIndices.push(descriptorHeapIndex + 1);
    _reusableMaterialSRVIndices.push(descriptorHeapIndex + 2);
}

void RayTracingPipelineShader::removeSRVToUnboundedAttributeBufferDescriptorTable(UINT descriptorHeapIndex)
{
    _reusableAttributeSRVIndices.push(descriptorHeapIndex);
}
void RayTracingPipelineShader::removeSRVToUnboundedIndexBufferDescriptorTable(UINT descriptorHeapIndex)
{
    _reusableIndexBufferSRVIndices.push(descriptorHeapIndex);
}

UINT RayTracingPipelineShader::addSRVToUnboundedAttributeBufferDescriptorTable(D3DBuffer* vertexBuffer,
                                                                               UINT vertexCount,
                                                                               UINT offset)
{
    auto                          device              = DXLayer::instance()->getDevice();
    UINT                          descriptorHeapIndex = -1;
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor;

    if (_reusableAttributeSRVIndices.empty() == false)
    {
        UINT reusedSrvIndex = _reusableAttributeSRVIndices.front();
        _reusableAttributeSRVIndices.pop();
        // Create view of SRV for shader access
        hDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            _unboundedAttributeBufferSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
            reusedSrvIndex, _descriptorSize);

        descriptorHeapIndex = reusedSrvIndex;
    }
    else
    {
        // Create view of SRV for shader access
        hDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            _unboundedAttributeBufferSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
            _unboundedAttributeBufferSrvIndex, _descriptorSize);

        descriptorHeapIndex = _unboundedAttributeBufferSrvIndex;
        _unboundedAttributeBufferSrvIndex++;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

    srvDesc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format                     = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement        = offset;
    srvDesc.Buffer.NumElements         = vertexCount;
    srvDesc.Buffer.StructureByteStride = sizeof(CompressedAttribute);
    srvDesc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;
    srvDesc.Buffer.FirstElement        = 0;

    device->CreateShaderResourceView(vertexBuffer->resource.Get(), &srvDesc, hDescriptor);

    return descriptorHeapIndex;
}

UINT RayTracingPipelineShader::addSRVToUnboundedIndexBufferDescriptorTable(D3DBuffer* indexBuffer,
                                                                           UINT indexCount,
                                                                           UINT offset)
{
    auto                          device              = DXLayer::instance()->getDevice();
    UINT                          descriptorHeapIndex = -1;
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor;

    if (_reusableIndexBufferSRVIndices.empty() == false)
    {
        UINT reusedSrvIndex = _reusableIndexBufferSRVIndices.front();
        _reusableIndexBufferSRVIndices.pop();
        // Create view of SRV for shader access
        hDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            _unboundedIndexBufferSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
            reusedSrvIndex, _descriptorSize);

        descriptorHeapIndex = reusedSrvIndex;
    }
    else
    {
        // Create view of SRV for shader access
        hDescriptor = CD3DX12_CPU_DESCRIPTOR_HANDLE(
            _unboundedIndexBufferSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
            _unboundedIndexBufferSrvIndex, _descriptorSize);

        descriptorHeapIndex = _unboundedIndexBufferSrvIndex;
        _unboundedIndexBufferSrvIndex++;
    }

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

    srvDesc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format                     = indexBuffer->indexBufferFormat;
    srvDesc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement        = offset;
    srvDesc.Buffer.NumElements         = indexCount;
    srvDesc.Buffer.StructureByteStride = 0;
    srvDesc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;

    device->CreateShaderResourceView(indexBuffer->resource.Get(), &srvDesc, hDescriptor);

    return descriptorHeapIndex;
}

void RayTracingPipelineShader::updateTextureUnbounded(int descriptorTableIndex, int textureUnit, Texture* texture,
                                                      int unboundedIndex, bool isCompute, bool isUAV)
{
    auto cmdList = DXLayer::instance()->getCmdList();

    auto descriptorSize = DXLayer::instance()->getDevice()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_GPU_DESCRIPTOR_HANDLE descriptorTableOffset = CD3DX12_GPU_DESCRIPTOR_HANDLE(
        _unboundedTextureSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), unboundedIndex,
        descriptorSize);

    if (isCompute && !isUAV)
    {
        ID3D12DescriptorHeap* descriptorHeaps[] = {_unboundedTextureSrvDescriptorHeap.Get()};
        cmdList->SetDescriptorHeaps(1, descriptorHeaps);

        cmdList->SetComputeRootDescriptorTable(descriptorTableIndex, descriptorTableOffset);
    }
    else if (isCompute == false && !isUAV)
    {
        ID3D12DescriptorHeap* descriptorHeaps[] = {_unboundedTextureSrvDescriptorHeap.Get()};
        cmdList->SetDescriptorHeaps(1, descriptorHeaps);

        cmdList->SetGraphicsRootDescriptorTable(descriptorTableIndex, descriptorTableOffset);
    }
}

void RayTracingPipelineShader::updateStructuredAttributeBufferUnbounded(int                          descriptorTableIndex,
                                                                        ComPtr<ID3D12DescriptorHeap> bufferDescriptorHeap,
                                                                        bool                         isCompute)
{
    auto cmdList = DXLayer::instance()->getCmdList();

    auto descriptorSize = DXLayer::instance()->getDevice()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_GPU_DESCRIPTOR_HANDLE descriptorTableOffset = CD3DX12_GPU_DESCRIPTOR_HANDLE(
        _unboundedAttributeBufferSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 0,
        descriptorSize);

    ID3D12DescriptorHeap* descriptorHeaps[] = {_unboundedAttributeBufferSrvDescriptorHeap.Get()};
    cmdList->SetDescriptorHeaps(1, descriptorHeaps);

    if (isCompute)
    {
        cmdList->SetComputeRootDescriptorTable(descriptorTableIndex, descriptorTableOffset);
    }
    else
    {
        cmdList->SetGraphicsRootDescriptorTable(descriptorTableIndex, descriptorTableOffset);
    }
}

void RayTracingPipelineShader::updateStructuredIndexBufferUnbounded(int                          descriptorTableIndex,
                                                                    ComPtr<ID3D12DescriptorHeap> bufferDescriptorHeap,
                                                                    bool                         isCompute)
{
    auto cmdList = DXLayer::instance()->getCmdList();

    auto descriptorSize = DXLayer::instance()->getDevice()->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    CD3DX12_GPU_DESCRIPTOR_HANDLE descriptorTableOffset = CD3DX12_GPU_DESCRIPTOR_HANDLE(
        _unboundedIndexBufferSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), 0,
        descriptorSize);

    ID3D12DescriptorHeap* descriptorHeaps[] = {_unboundedIndexBufferSrvDescriptorHeap.Get()};
    cmdList->SetDescriptorHeaps(1, descriptorHeaps);

    if (isCompute)
    {
        cmdList->SetComputeRootDescriptorTable(descriptorTableIndex, descriptorTableOffset);
    }
    else
    {
        cmdList->SetGraphicsRootDescriptorTable(descriptorTableIndex, descriptorTableOffset);
    }
}

// Allocate a descriptor and return its index.
// If the passed descriptorIndexToUse is valid, it will be used instead of allocating a new one.
UINT RayTracingPipelineShader::_allocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor,
                                                   UINT descriptorIndexToUse)
{
    auto descriptorHeapCpuBase = _descriptorHeap->GetCPUDescriptorHandleForHeapStart();

    if (descriptorIndexToUse >= _descriptorHeap->GetDesc().NumDescriptors)
    {
        descriptorIndexToUse = _descriptorsAllocated++;
    }
    *cpuDescriptor =
        CD3DX12_CPU_DESCRIPTOR_HANDLE(descriptorHeapCpuBase, descriptorIndexToUse, _descriptorSize);
    return descriptorIndexToUse;
}
// Create SRV for a buffer.
UINT RayTracingPipelineShader::createBufferSRV(D3DBuffer* buffer, UINT numElements,
                                               UINT elementSize, DXGI_FORMAT format)
{

    auto                            device  = DXLayer::instance()->getDevice();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.NumElements              = numElements;

    if (elementSize == 0)
    {
        srvDesc.Format                     = format;
        srvDesc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;
        srvDesc.Buffer.StructureByteStride = 0;
    }
    else
    {
        srvDesc.Format                     = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;
        srvDesc.Buffer.StructureByteStride = elementSize;
        srvDesc.Buffer.FirstElement        = 0;
    }
    UINT descriptorIndex = _allocateDescriptor(&buffer->cpuDescriptorHandle);
    device->CreateShaderResourceView(buffer->resource.Get(), &srvDesc, buffer->cpuDescriptorHandle);
    buffer->gpuDescriptorHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(
        _descriptorHeap->GetGPUDescriptorHandleForHeapStart(), descriptorIndex, _descriptorSize);
    return descriptorIndex;
}