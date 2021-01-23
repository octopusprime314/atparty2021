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
    _transmissionMapping.reserve(InitInstancesForRayTracing);

    auto commandList = DXLayer::instance()->getComputeCmdList();
    auto dxLayer     = DXLayer::instance();

    device->QueryInterface(IID_PPV_ARGS(&_dxrDevice));

    // Initialize command list round trip execution to CMD_LIST_NUM
    // Initialize suballocator blocks to 64 KB and limit compaction transient allocation to 16 MB
    RTCompaction::Initialize(_dxrDevice.Get(), CMD_LIST_NUM, 65536, 16777216);

    constexpr auto transformOffset              = 12; // 3x4
    UINT           instanceTransformSizeInBytes = InitInstancesForRayTracing * transformOffset;

    constexpr auto normalTransformOffset    = 9; // 3x3
    UINT instanceNormalTransformSizeInBytes = InitInstancesForRayTracing * normalTransformOffset;
    _instanceNormalMatrixTransforms.resize(instanceNormalTransformSizeInBytes);
    
    _instanceWorldToObjectMatrixTransforms.resize(instanceTransformSizeInBytes);
    _instanceTransforms                   .resize(instanceTransformSizeInBytes);
    _prevInstanceTransforms               .resize(instanceTransformSizeInBytes);

    // Create descriptor heap
    ZeroMemory(&_rtASSrvHeapDesc, sizeof(_rtASSrvHeapDesc));
    _rtASSrvHeapDesc.NumDescriptors = 1;
    _rtASSrvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    _rtASSrvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    _dxrDevice->CreateDescriptorHeap(&_rtASSrvHeapDesc,
                                     IID_PPV_ARGS(_rtASDescriptorHeap.GetAddressOf()));

    _instanceIndexToMaterialMappingGPUBuffer  = nullptr;
    _instanceIndexToAttributeMappingGPUBuffer = nullptr;
    _instanceNormalMatrixTransformsGPUBuffer  = nullptr;
    _instanceTransmissionMappingGPUBuffer     = nullptr;

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
    return _topLevelAccelerationStructure[_topLevelIndex]->GetGPUVirtualAddress();
}

ComPtr<ID3D12DescriptorHeap> RayTracingPipelineShader::getDescHeap()
{
    return _descriptorHeap;
}

std::map<Model*, std::vector<AssetTexture*>>& RayTracingPipelineShader::getSceneTextures()
{
    return _modelTextures;
}


void RayTracingPipelineShader::updateAndBindMaterialBuffer(std::map<std::string, UINT> resourceIndexes)
{
    BYTE* mappedData                           = nullptr;
    _instanceIndexToMaterialMappingGPUBuffer->resource->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
    memcpy(&mappedData[0], _materialMapping.data(), sizeof(UINT) * _materialMapping.size());

    auto                  cmdList           = DXLayer::instance()->getCmdList();
    auto                  resourceBindings  = resourceIndexes;
    ID3D12DescriptorHeap* descriptorHeaps[] = {_descriptorHeap.Get()};
    cmdList->SetDescriptorHeaps(1, descriptorHeaps);
    cmdList->SetComputeRootDescriptorTable(
        resourceBindings["instanceIndexToMaterialMapping"],
        _instanceIndexToMaterialMappingGPUBuffer->gpuDescriptorHandle);
}

void RayTracingPipelineShader::updateAndBindAttributeBuffer(std::map<std::string, UINT> resourceIndexes)
{
    BYTE* mappedData                           = nullptr;
    _instanceIndexToAttributeMappingGPUBuffer->resource->Map(0, nullptr,
                                                             reinterpret_cast<void**>(&mappedData));
    memcpy(&mappedData[0], _attributeMapping.data(), sizeof(UINT) * _attributeMapping.size());

    auto                  cmdList           = DXLayer::instance()->getCmdList();
    auto                  resourceBindings  = resourceIndexes;
    ID3D12DescriptorHeap* descriptorHeaps[] = {_descriptorHeap.Get()};
    cmdList->SetDescriptorHeaps(1, descriptorHeaps);
    cmdList->SetComputeRootDescriptorTable(
        resourceBindings["instanceIndexToAttributesMapping"],
        _instanceIndexToAttributeMappingGPUBuffer->gpuDescriptorHandle);
}

void RayTracingPipelineShader::updateAndBindTransmissionBuffer(std::map<std::string, UINT> resourceIndexes)
{
    _transmissionMapping.clear();
    for(auto transmissionVector : _transmissionMap)
    {
        for (auto transmission : transmissionVector.second)
        {
            _transmissionMapping.push_back(transmission);
        }
    }

    BYTE* mappedData                           = nullptr;
    _instanceTransmissionMappingGPUBuffer->resource->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));
    memcpy(&mappedData[0], _transmissionMapping.data(), sizeof(UINT) * _transmissionMapping.size());

    auto                  cmdList           = DXLayer::instance()->getCmdList();
    auto                  resourceBindings  = resourceIndexes;
    ID3D12DescriptorHeap* descriptorHeaps[] = {_descriptorHeap.Get()};
    cmdList->SetDescriptorHeaps(1, descriptorHeaps);
    cmdList->SetComputeRootDescriptorTable(
        resourceBindings["instanceTransmissionMapping"],
        _instanceTransmissionMappingGPUBuffer->gpuDescriptorHandle);
}

void RayTracingPipelineShader::updateAndBindNormalMatrixBuffer(std::map<std::string, UINT> resourceIndexes)
{
    BYTE* mappedData                           = nullptr;
    _instanceNormalMatrixTransformsGPUBuffer->resource->Map(0, nullptr,
                                                             reinterpret_cast<void**>(&mappedData));
    memcpy(&mappedData[0], _instanceNormalMatrixTransforms.data(), sizeof(float) * _instanceNormalMatrixTransforms.size());

    auto                  cmdList           = DXLayer::instance()->getCmdList();
    auto                  resourceBindings  = resourceIndexes;
    ID3D12DescriptorHeap* descriptorHeaps[] = {_descriptorHeap.Get()};
    cmdList->SetDescriptorHeaps(1, descriptorHeaps);
    cmdList->SetComputeRootDescriptorTable(
        resourceBindings["instanceNormalMatrixTransforms"],
        _instanceNormalMatrixTransformsGPUBuffer->gpuDescriptorHandle);
}

void RayTracingPipelineShader::buildBLAS(Entity* entity)
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

        int indexCount  = 0;
        int vertexCount = 0;
        if (i + 1 == vertexAndBufferStrides.size())
        {
            indexCount = (indexDesc.Width / indexTypeSize) - vertexAndBufferStrides[i].second;
            vertexCount = (vertexDesc.Width / sizeof(CompressedAttribute)) - vertexAndBufferStrides[i].first;
        }
        else
        {
            indexCount = vertexAndBufferStrides[i + 1].second - vertexAndBufferStrides[i].second;
            vertexCount = vertexAndBufferStrides[i + 1].first - vertexAndBufferStrides[i].first;
        }

        auto indexGPUAddress = (*entity->getFrustumVAO())[0]
                                   ->getIndexResource()
                                   ->getResource()
                                   ->GetGPUVirtualAddress() +
                               (vertexAndBufferStrides[i].second * indexTypeSize);

        auto vertexGPUAddress = (*entity->getFrustumVAO())[0]
                                    ->getVertexResource()
                                    ->getResource()
                                    ->GetGPUVirtualAddress() + 
                                 (vertexAndBufferStrides[i].first * sizeof(CompressedAttribute));

        int staticGeomIndex = staticGeometryDesc->size();
        staticGeometryDesc->push_back(D3D12_RAYTRACING_GEOMETRY_DESC());

        (*staticGeometryDesc)[staticGeomIndex].Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        (*staticGeometryDesc)[staticGeomIndex].Triangles.IndexBuffer = indexGPUAddress;
        (*staticGeometryDesc)[staticGeomIndex].Triangles.IndexCount = static_cast<UINT>(indexCount);

        (*staticGeometryDesc)[staticGeomIndex].Triangles.IndexFormat = indexFormat;

        (*staticGeometryDesc)[staticGeomIndex].Triangles.Transform3x4 = 0;
        (*staticGeometryDesc)[staticGeomIndex].Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
        (*staticGeometryDesc)[staticGeomIndex].Triangles.VertexCount  = static_cast<UINT>(vertexCount);
        (*staticGeometryDesc)[staticGeomIndex].Triangles.VertexBuffer.StartAddress = vertexGPUAddress;
        (*staticGeometryDesc)[staticGeomIndex].Triangles.VertexBuffer.StrideInBytes = sizeof(CompressedAttribute);

        Model* bufferModel         = entity->getModel();
        _indexBuffer[bufferModel] .push_back(new D3DBuffer());
        _vertexBuffer[bufferModel].push_back(new D3DBuffer());

        _indexBuffer[bufferModel].back()->indexBufferFormat = indexFormat;

        _vertexBuffer[bufferModel].back()->count =
            (*staticGeometryDesc)[staticGeomIndex].Triangles.VertexCount;
        _indexBuffer[bufferModel].back()->count =
            (*staticGeometryDesc)[staticGeomIndex].Triangles.IndexCount;
        _indexBuffer[bufferModel].back()->resource =
            (*entity->getFrustumVAO())[0]->getIndexResource()->getResource();
        _vertexBuffer[bufferModel].back()->resource =
            (*entity->getFrustumVAO())[0]->getVertexResource()->getResource();
        _vertexBuffer[bufferModel].back()->nameId = entity->getModel()->getName();

        _vertexBuffer[bufferModel].back()->offset = vertexAndBufferStrides[i].first;
        _indexBuffer[bufferModel].back()->offset  = vertexAndBufferStrides[i].second;

        auto materialTransmittance = entity->getModel()->getMaterial(i).transmittance;

        if (materialTransmittance < 1.0f)
        {
            (*staticGeometryDesc)[staticGeomIndex].Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
        }
        else
        {
            (*staticGeometryDesc)[staticGeomIndex].Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
        }

        auto mapLocation = _attributeMap.find(entity->getModel());
        if (mapLocation == _attributeMap.end())
        {
            _attributeMap[entity->getModel()] = 0;

            int offset = 0;
            for (auto& attMap : _attributeMap)
            {
                attMap.second = offset;
                offset += (*attMap.first->getVAO())[0]->getVertexAndIndexBufferStrides().size();
            }
        }

        _transmissionMap[entity->getModel()].push_back(entity->getModel()->getMaterialNames()[i].transmittance);

        if (_materialMap.find(entity->getModel()) == _materialMap.end())
        {
            auto materialNames = entity->getModel()->getMaterialNames();

            for (auto textureNames : materialNames)
            {
                AssetTexture* texture = textureBroker->getTexture(textureNames.albedo);
                _modelTextures[bufferModel].push_back(texture);
                texture = textureBroker->getTexture(textureNames.normal);
                _modelTextures[bufferModel].push_back(texture);
                texture = textureBroker->getTexture(textureNames.roughnessMetallic);
                _modelTextures[bufferModel].push_back(texture);
            }

            _materialMap[bufferModel] = 0;

            int offset = 0;
            for (auto& matMap : _materialMap)
            {
                matMap.second = offset;
                offset += matMap.first->getMaterialNames().size() * Material::TexturesPerMaterial;
            }
        }
    }

    bottomLevelInputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    bottomLevelInputs.NumDescs       = static_cast<UINT>(staticGeometryDesc->size());
    bottomLevelInputs.pGeometryDescs = staticGeometryDesc->data();

    _bottomLevelBuildDescs.push_back(bottomLevelInputs);
    _bottomLevelBuildModels.push_back(entity->getModel());
}

void RayTracingPipelineShader::buildAccelerationStructures()
{
    auto viewEventDistributor = EngineManager::instance()->getViewManager();
    auto entityList    = EngineManager::instance()->getEntityList();
    auto textureBroker = TextureBroker::instance();
    auto dxLayer       = DXLayer::instance();
    auto commandList   = dxLayer->usingAsyncCompute() ? DXLayer::instance()->getComputeCmdList()
                                                      : DXLayer::instance()->getCmdList();

    auto device = dxLayer->getDevice();
    device->QueryInterface(IID_PPV_ARGS(&_dxrDevice));

    // Increment next frame and let the library internally manage compaction and releasing memory
    RTCompaction::NextFrame(_dxrDevice.Get(), commandList.Get());

    constexpr int transformOffset          = sizeof(float) * 12;
    constexpr int normalTransformOffset    = sizeof(float) * 9;
    constexpr int tlasAllocationMultiplier = 10;
    Vector4       cameraPos                = viewEventDistributor->getCameraPos();

    _attributeMapping.clear();
    _materialMapping.clear();
    _bottomLevelBuildDescs.clear();
    _bottomLevelBuildModels.clear();

    // random floats between -1.0 - 1.0
    std::random_device               rd;
    std::mt19937                     generator(rd());
    std::uniform_real_distribution<> randomFloats(-1.0, 1.0);

    if (entityList->size() < MaxInstancesForRayTracing)
    //if (entityList->size() < 0)
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

    float cometTailRadius = 20000;
    bool  newBlasBuilds   = false;

    std::map<Model*, int> modelCountsInEntities;
    for (auto entity = entityList->begin(); entity != entityList->end();)
    {
        bool    removed        = false;
        Vector4 entityPosition = (*entity)->getWorldSpaceTransform() * Vector4(0, 0, 0, 1);

        entityPosition = -entityPosition;

        if ((*entity)->getHasEntered() == false)
        {
            Vector4 rotation(randomFloats(generator) * 360.0, randomFloats(generator) * 360.0,
                             randomFloats(generator) * 360.0);

            (*entity)->entranceWaypoint(Vector4(-entityPosition.getx(),
                                                -entityPosition.gety() - 500.0,
                                                -entityPosition.getz()),
                                        rotation, 4000);

            // Does a vertex buffer exist for this blas
            bool isNewBLAS = _vertexBuffer.find((*entity)->getModel()) == _vertexBuffer.end();

            if (isNewBLAS)
            {
                buildBLAS((*entity));
                newBlasBuilds = true;
            }
        }
        else
        {
            // Does a vertex buffer exist for this blas
            bool isNewBLAS = _vertexBuffer.find((*entity)->getModel()) == _vertexBuffer.end();

            if (isNewBLAS)
            {
                buildBLAS((*entity));
                newBlasBuilds = true;
            }
        }

        modelCountsInEntities[(*entity)->getModel()]++;

        auto distance = (cameraPos - entityPosition).getMagnitude();
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
        if (model.second == 0 && _blas[model.first] != nullptr)
        {
            _vertexBuffer.erase(_vertexBuffer.find(model.first));
            _indexBuffer.erase(_indexBuffer.find(model.first));
            _modelTextures.erase(_modelTextures.find(model.first));
            // Deallocate the memory first
            RTCompaction::RemoveAccelerationStructures(&_blas[model.first], 1);
            // Remove the blas entry from the list
            _blas.erase(model.first);
            _attributeMap.erase(model.first);
            _transmissionMap.erase(model.first);
            _materialMap.erase(model.first);
            blasRemoved = true;
        }
    }

    if (blasRemoved)
    {
        // Recalibrate attribute buffers
        int offset = 0;
        for (auto& attMap : _attributeMap)
        {
            attMap.second = offset;
            offset += (*attMap.first->getVAO())[0]->getVertexAndIndexBufferStrides().size();
        }

        offset = 0;
        for (auto& materialMap : _materialMap)
        {
            materialMap.second = offset;
            offset += materialMap.first->getMaterialNames().size() * Material::TexturesPerMaterial;
        }
    }

    bool newInstanceMappingAllocation = false;
    if ((_instanceIndexToMaterialMappingGPUBuffer == nullptr) ||
        (_instanceIndexToAttributeMappingGPUBuffer == nullptr) ||
        (_instanceNormalMatrixTransformsGPUBuffer == nullptr) ||
        (_instanceTransmissionMappingGPUBuffer    == nullptr))
    {
        _instanceIndexToMaterialMappingGPUBuffer  = new D3DBuffer();
        _instanceIndexToAttributeMappingGPUBuffer = new D3DBuffer();
        _instanceNormalMatrixTransformsGPUBuffer  = new D3DBuffer();
        _instanceTransmissionMappingGPUBuffer     = new D3DBuffer();

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
                                   : _instanceIndexToMaterialMappingGPUBuffer->count * tlasAllocationMultiplier;

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

        createBufferSRV(_instanceIndexToMaterialMappingGPUBuffer, newInstanceSize, sizeof(UINT));

        allocateUploadBuffer(DXLayer::instance()->getDevice().Get(), nullptr,
                             sizeof(UINT) * newInstanceSize,
                             &_instanceIndexToAttributeMappingUpload[_instanceMappingIndex],
                             L"instanceIndexToAttribute");

        _instanceIndexToAttributeMappingGPUBuffer->resource =
            _instanceIndexToAttributeMappingUpload[_instanceMappingIndex];
        _instanceIndexToAttributeMappingGPUBuffer->count = newInstanceSize;

        createBufferSRV(_instanceIndexToAttributeMappingGPUBuffer, newInstanceSize, sizeof(UINT));

        allocateUploadBuffer(DXLayer::instance()->getDevice().Get(), nullptr,
                             sizeof(float) * newInstanceSize,
                             &_instanceTransmissionMappingUpload[_instanceMappingIndex],
                             L"instanceTransmission");

        _instanceTransmissionMappingGPUBuffer->resource =
             _instanceTransmissionMappingUpload[_instanceMappingIndex];
        _instanceTransmissionMappingGPUBuffer->count = newInstanceSize;

        createBufferSRV(_instanceTransmissionMappingGPUBuffer, newInstanceSize, sizeof(float));

        allocateUploadBuffer(
            DXLayer::instance()->getDevice().Get(), nullptr, sizeof(float) * 9 * newInstanceSize,
            &_instanceNormalMatrixTransformsUpload[_instanceMappingIndex], L"instanceNormals");

        _instanceNormalMatrixTransformsGPUBuffer->resource =
            _instanceNormalMatrixTransformsUpload[_instanceMappingIndex];
        _instanceNormalMatrixTransformsGPUBuffer->count = newInstanceSize;

        createBufferSRV(_instanceNormalMatrixTransformsGPUBuffer, 9 * newInstanceSize,
                        sizeof(UINT));
    }


    if (newBlasBuilds)
    {
        RTCompaction::ASBuffers* buffers = RTCompaction::BuildAccelerationStructures(
            _dxrDevice.Get(), commandList.Get(), _bottomLevelBuildDescs.data(),
            _bottomLevelBuildDescs.size());

        for (int asBufferIndex = 0; asBufferIndex < _bottomLevelBuildModels.size(); asBufferIndex++)
        {
            _blas[_bottomLevelBuildModels[asBufferIndex]] = &buffers[asBufferIndex];
        }

        commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(nullptr));
    }

    // Copy over all the previous instance transforms for motion vectors
    memcpy(_prevInstanceTransforms.data(), _instanceTransforms.data(), sizeof(float) * 12 * entityList->size());

    // Create an instance desc for the dynamic and static bottom levels
    _instanceDesc.resize(0);

    int instanceDescIndex = 0;
    for (auto entity : *entityList)
    {
        bool isValidBlas = _blas.find(entity->getModel()) != _blas.end();

        if (isValidBlas)
        {
            _instanceDesc.push_back(D3D12_RAYTRACING_INSTANCE_DESC());

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
            memcpy(&_instanceDesc[instanceDescIndex].Transform, &_instanceTransforms[instanceDescIndex * (transformOffset / sizeof(float))], sizeof(float) * 12);

            // do not overwrite geometry flags for bottom levels, this caused the non opaque and
            // opaqueness of bottom levels to be random
            _instanceDesc[instanceDescIndex].Flags                               = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
            _instanceDesc[instanceDescIndex].InstanceMask                        = 1;
            _instanceDesc[instanceDescIndex].InstanceID                          = 0;
            _instanceDesc[instanceDescIndex].InstanceContributionToHitGroupIndex = 0;
            _instanceDesc[instanceDescIndex].AccelerationStructure               = _blas[entity->getModel()]->GetASBuffer();

            instanceDescIndex++;

            _materialMapping.push_back(_materialMap[entity->getModel()]);
            _attributeMapping.push_back(_attributeMap[entity->getModel()]);
        }
    }

    CD3DX12_HEAP_PROPERTIES uploadHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC    topLevelBuildDesc = {};
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS& topLevelInputs = topLevelBuildDesc.Inputs;
    topLevelInputs.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
    topLevelInputs.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    topLevelInputs.NumDescs       = static_cast<UINT>(instanceDescIndex);
    topLevelInputs.pGeometryDescs = nullptr;
    topLevelInputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO topLevelPrebuildInfo = {};

    _dxrDevice->GetRaytracingAccelerationStructurePrebuildInfo(&topLevelInputs,
                                                               &topLevelPrebuildInfo);

    bool newTopLevelAllocation = false;
    if ((_tlScratchResource[_topLevelIndex] == nullptr) || 
        (_topLevelAccelerationStructure[_topLevelIndex] == nullptr) ||
        (_instanceDescs[_topLevelIndex] == nullptr))
    {
        newTopLevelAllocation = true;
    }
    else if ((topLevelPrebuildInfo.ScratchDataSizeInBytes >_tlScratchResource[_topLevelIndex]->GetDesc().Width) ||
             (topLevelPrebuildInfo.ResultDataMaxSizeInBytes > _topLevelAccelerationStructure[_topLevelIndex]->GetDesc().Width)||
             (_instanceDescs[_topLevelIndex]->GetDesc().Width < (sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instanceDescIndex)))
    {
        newTopLevelAllocation = true;
        _topLevelIndex++;

        _topLevelIndex %= CMD_LIST_NUM;
    }

    if (newTopLevelAllocation)
    {
        auto tlasScratchBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(topLevelPrebuildInfo.ScratchDataSizeInBytes * tlasAllocationMultiplier,
                                                                   D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        _dxrDevice->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
                                            &tlasScratchBufferDesc, D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                            nullptr, IID_PPV_ARGS(&_tlScratchResource[_topLevelIndex]));

        auto tlasBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(topLevelPrebuildInfo.ResultDataMaxSizeInBytes * tlasAllocationMultiplier,
                                                            D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

        _dxrDevice->CreateCommittedResource(&uploadHeapProperties, D3D12_HEAP_FLAG_NONE,
                                            &tlasBufferDesc, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                                            nullptr, IID_PPV_ARGS(&_topLevelAccelerationStructure[_topLevelIndex]));

        // Create view of SRV for shader access
        CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(_rtASDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
        ZeroMemory(&_rtASSrvDesc, sizeof(_rtASSrvDesc));
        _rtASSrvDesc.Shader4ComponentMapping                  = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        _rtASSrvDesc.ViewDimension                            = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        _rtASSrvDesc.RaytracingAccelerationStructure.Location = _topLevelAccelerationStructure[_topLevelIndex]->GetGPUVirtualAddress();
        _dxrDevice->CreateShaderResourceView(nullptr, &_rtASSrvDesc, hDescriptor);

        allocateUploadBuffer(_dxrDevice.Get(), nullptr,
                             sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instanceDescIndex * tlasAllocationMultiplier,
                             &_instanceDescs[_topLevelIndex], L"InstanceDescs");
    }

    BYTE* mappedData = nullptr;
    CD3DX12_RANGE readRange(0, 0);
    _instanceDescs[_topLevelIndex]->Map(0, &readRange, reinterpret_cast<void**>(&mappedData));

    memcpy(&mappedData[0], _instanceDesc.data(), sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * instanceDescIndex);

    // Top Level Acceleration Structure desc
    topLevelBuildDesc.DestAccelerationStructureData    = _topLevelAccelerationStructure[_topLevelIndex]->GetGPUVirtualAddress();
    topLevelBuildDesc.ScratchAccelerationStructureData = _tlScratchResource[_topLevelIndex]->GetGPUVirtualAddress();
    topLevelBuildDesc.Inputs.InstanceDescs             = _instanceDescs[_topLevelIndex]->GetGPUVirtualAddress();

    commandList->BuildRaytracingAccelerationStructure(&topLevelBuildDesc, 0, nullptr);
    commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(_topLevelAccelerationStructure[_topLevelIndex].Get()));
}
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

void RayTracingPipelineShader::addSRVToUnboundedTextureDescriptorTable(Texture* texture)
{
    auto device = DXLayer::instance()->getDevice();
    auto descriptorSize =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Create view of SRV for shader access
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(
        _unboundedTextureSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        _unboundedTextureSrvIndex, descriptorSize);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

    auto textureDescriptor = texture->getResource()->getDescriptor();

    srvDesc.Shader4ComponentMapping       = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format                        = textureDescriptor.Format;
    srvDesc.ViewDimension                 = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip     = 0;
    srvDesc.Texture2D.MipLevels           = textureDescriptor.MipLevels;
    srvDesc.Texture2D.ResourceMinLODClamp = 0;
    device->CreateShaderResourceView(texture->getResource()->getResource().Get(), &srvDesc,
                                     hDescriptor);

    _unboundedTextureSrvIndex++;
}

void RayTracingPipelineShader::addSRVToUnboundedAttributeBufferDescriptorTable(D3DBuffer* vertexBuffer,
                                                                               UINT vertexCount,
                                                                               UINT offset)
{
    auto device = DXLayer::instance()->getDevice();
    auto descriptorSize =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Create view of SRV for shader access
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(
        _unboundedAttributeBufferSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        _unboundedAttributeBufferSrvIndex, descriptorSize);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

    srvDesc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format                     = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement        = offset;
    srvDesc.Buffer.NumElements         = vertexCount;
    srvDesc.Buffer.StructureByteStride = sizeof(CompressedAttribute);
    srvDesc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;

    device->CreateShaderResourceView(vertexBuffer->resource.Get(), &srvDesc, hDescriptor);

    _unboundedAttributeBufferSrvIndex++;
}

void RayTracingPipelineShader::addSRVToUnboundedIndexBufferDescriptorTable(D3DBuffer* indexBuffer,
                                                                           UINT indexCount,
                                                                           UINT offset)
{
    auto device = DXLayer::instance()->getDevice();
    auto descriptorSize =
        device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Create view of SRV for shader access
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(
        _unboundedIndexBufferSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(),
        _unboundedIndexBufferSrvIndex, descriptorSize);

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};

    srvDesc.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format                     = indexBuffer->indexBufferFormat;
    srvDesc.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Buffer.FirstElement        = offset;
    srvDesc.Buffer.NumElements         = indexCount;
    srvDesc.Buffer.StructureByteStride = 0;
    srvDesc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_NONE;

    device->CreateShaderResourceView(indexBuffer->resource.Get(), &srvDesc, hDescriptor);

    _unboundedIndexBufferSrvIndex++;
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
                                               UINT elementSize)
{

    auto                            device  = DXLayer::instance()->getDevice();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.ViewDimension                   = D3D12_SRV_DIMENSION_BUFFER;
    srvDesc.Shader4ComponentMapping         = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Buffer.NumElements              = numElements;

    if (elementSize == 0)
    {
        srvDesc.Format                     = DXGI_FORMAT_R32_TYPELESS;
        srvDesc.Buffer.Flags               = D3D12_BUFFER_SRV_FLAG_RAW;
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