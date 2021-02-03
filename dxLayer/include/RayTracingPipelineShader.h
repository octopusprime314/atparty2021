#pragma once
#include "Entity.h"
#include "RenderTexture.h"
#include <d3d12.h>
#include "d3dx12.h"
#include "dxc/dxcapi.h"
#include "dxc/dxcapi.use.h"
#include <D3Dcompiler.h>
#include <map>
#include <string>
#include <vector>
#include <wrl.h>
#include "HLSLShader.h"
#include "RTCompaction.h"
#include "DXDefines.h"
#include "Model.h"

using namespace Microsoft::WRL;

struct Viewport
{
    float left;
    float top;
    float right;
    float bottom;
};

struct GpuToCpuBuffers
{
    ComPtr<ID3D12Resource> outputBuffer;
    ComPtr<ID3D12Resource> readbackBuffer;
    unsigned char*         cpuSideData;
};

#define SizeOfInUint32(obj) ((sizeof(obj) - 1) / sizeof(UINT32) + 1)
#define InitInstancesForRayTracing 256
#define MaxInstancesForRayTracing  10000
// 4000 BLAS count using 3 textures each
#define MaxBLASSRVsForRayTracing   4000 * 3
#define TlasAllocationMultiplier   10

#define RandomInsertAndRemoveEntities 0


class RayTracingPipelineShader
{
    using TextureDescriptorHeapMap = std::pair<std::vector<AssetTexture*>, int>;
    using TextureMapping = std::map<Model*, TextureDescriptorHeapMap>;

    using D3DBufferDescriptorHeapMap = std::pair<std::vector<D3DBuffer*>, int>;
    using AttributeMapping = std::map<Model*, D3DBufferDescriptorHeapMap>;

    using IndexBufferMapping = std::map<Model*, D3DBufferDescriptorHeapMap>;

    using UniformMaterialMap     = std::pair<std::vector<UniformMaterial>, int>;
    using UniformMaterialMapping = std::vector<std::pair<Model*, UniformMaterialMap>>;

    using BlasMapping= std::map<Model*, RTCompaction::ASBuffers*>;

    UINT                                                              _descriptorsAllocated;
    D3D12_DESCRIPTOR_HEAP_DESC                                        _descriptorHeapDesc;
    ComPtr<ID3D12DescriptorHeap>                                      _descriptorHeap;
    UINT                                                              _descriptorSize;

    ComPtr<ID3D12Device5>                                             _dxrDevice;
    ComPtr<ID3D12DescriptorHeap>                                      _rtASDescriptorHeap;
    D3D12_DESCRIPTOR_HEAP_DESC                                        _rtASSrvHeapDesc;
    D3D12_SHADER_RESOURCE_VIEW_DESC                                   _rtASSrvDesc;

    std::vector<float>                                                _instanceNormalMatrixTransforms;
    std::vector<float>                                                _instanceWorldToObjectMatrixTransforms;
    std::vector<float>                                                _instanceTransforms;
    std::vector<float>                                                _prevInstanceTransforms;

    AttributeMapping                                                  _vertexBufferMap;
    IndexBufferMapping                                                _indexBufferMap;
    TextureMapping                                                    _texturesMap;
    UniformMaterialMapping                                            _uniformMaterialMap;
    BlasMapping                                                       _blasMap;

    std::vector<UINT>                                                 _attributeMapping;
    std::vector<UINT>                                                 _materialMapping;

    std::queue<UINT>                                                  _reusableMaterialSRVIndices;
    std::queue<UINT>                                                  _reusableAttributeSRVIndices;
    std::queue<UINT>                                                  _reusableIndexBufferSRVIndices;

    ComPtr<ID3D12Resource>                                            _instanceIndexToMaterialMappingUpload[CMD_LIST_NUM];
    D3DBuffer*                                                        _instanceIndexToMaterialMappingGPUBuffer;

    ComPtr<ID3D12Resource>                                            _instanceIndexToAttributeMappingUpload[CMD_LIST_NUM];
    D3DBuffer*                                                        _instanceIndexToAttributeMappingGPUBuffer;

    ComPtr<ID3D12Resource>                                            _instanceUniformMaterialMappingUpload[CMD_LIST_NUM];
    D3DBuffer*                                                        _instanceUniformMaterialMappingGPUBuffer;

    ComPtr<ID3D12Resource>                                            _instanceNormalMatrixTransformsUpload[CMD_LIST_NUM];
    D3DBuffer*                                                        _instanceNormalMatrixTransformsGPUBuffer;

    std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS> _bottomLevelBuildDescs;
    std::vector<Model*>                                               _bottomLevelBuildModels;
    ComPtr<ID3D12Resource>                                            _tlasResultBuffer[CMD_LIST_NUM];
    ComPtr<ID3D12Resource>                                            _tlasScratchBuffer[CMD_LIST_NUM];
    ComPtr<ID3D12Resource>                                            _instanceDescriptionGPUBuffer[CMD_LIST_NUM];


    ComPtr<ID3D12DescriptorHeap>                                      _unboundedTextureSrvDescriptorHeap;
    UINT                                                              _unboundedTextureSrvIndex;
    ComPtr<ID3D12DescriptorHeap>                                      _unboundedAttributeBufferSrvDescriptorHeap;
    UINT                                                              _unboundedAttributeBufferSrvIndex;
    ComPtr<ID3D12DescriptorHeap>                                      _unboundedIndexBufferSrvDescriptorHeap;
    UINT                                                              _unboundedIndexBufferSrvIndex;

    bool                                                              _doneAdding;
    bool                                                              _useCompaction        = true;
    int                                                               _topLevelIndex        = 0;
    int                                                               _instanceMappingIndex = 0;
    
    UINT _allocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor,
                             UINT descriptorIndexToUse = UINT_MAX);
    void _updateTLASData(int tlasCount,
                         std::vector<D3D12_RAYTRACING_INSTANCE_DESC>& instanceDescriptionCPUBuffer);
    void _updateInstanceData();
    void _updateGeometryData();

  public:
    RayTracingPipelineShader();
    void                                          init(ComPtr<ID3D12Device> device);
    ComPtr<ID3D12DescriptorHeap>                  getRTASDescHeap();
    D3D12_GPU_VIRTUAL_ADDRESS                     getRTASGPUVA();
    ComPtr<ID3D12DescriptorHeap>                  getDescHeap();
    TextureMapping&                               getSceneTextures();
    AttributeMapping&                             getVertexBuffers();
    IndexBufferMapping&                           getIndexBuffers();
    float*                                        getInstanceNormalTransforms();
    float*                                        getWorldToObjectTransforms();
    float*                                        getPrevInstanceTransforms();
    int                                           getBLASCount();
    void                                          updateAndBindMaterialBuffer(std::map<std::string, UINT> resourceIndexes, bool isCompute);
    void                                          updateAndBindAttributeBuffer(std::map<std::string, UINT> resourceIndexes, bool isCompute);
    void                                          updateAndBindUniformMaterialBuffer(std::map<std::string, UINT> resourceIndexes, bool isCompute);
    void                                          updateAndBindNormalMatrixBuffer(std::map<std::string, UINT> resourceIndexes, bool isCompute);
    void                                          buildAccelerationStructures();
    UINT                                          createBufferSRV(D3DBuffer* buffer, UINT numElements, UINT elementSize, DXGI_FORMAT format);
    void                                          buildGeometry(Entity* entity);
    void                                          createUnboundedTextureSrvDescriptorTable(UINT descriptorTableEntries);
    void                                          createUnboundedAttributeBufferSrvDescriptorTable(UINT descriptorTableEntries);
    void                                          createUnboundedIndexBufferSrvDescriptorTable(UINT descriptorTableEntries);
    UINT                                          addSRVToUnboundedTextureDescriptorTable(Texture* texture);
    UINT                                          addSRVToUnboundedAttributeBufferDescriptorTable(D3DBuffer* vertexBuffer,
                                                                                                  UINT       vertexCount,
                                                                                                  UINT       offset);
    UINT                                          addSRVToUnboundedIndexBufferDescriptorTable(D3DBuffer* indexBuffer,
                                                                                              UINT       indexCount,
                                                                                              UINT       offset);

    void removeSRVToUnboundedTextureDescriptorTable(UINT descriptorHeapIndex);
    void removeSRVToUnboundedAttributeBufferDescriptorTable(UINT descriptorHeapIndex);
    void removeSRVToUnboundedIndexBufferDescriptorTable(UINT descriptorHeapIndex);

    void resetUnboundedTextureDescriptorTable();
    void resetUnboundedAttributeBufferDescriptorTable();
    void resetUnboundedIndexBufferDescriptorTable();

    void updateTextureUnbounded(int descriptorTableIndex,
                                int textureUnit,
                                Texture* texture,
                                int unboundedIndex,
                                bool isCompute = false,
                                bool isUAV = false);

    void updateStructuredAttributeBufferUnbounded(int                          descriptorTableIndex,
                                                  ComPtr<ID3D12DescriptorHeap> bufferDescriptorHeap,
                                                  bool                         isCompute = false);
    void updateStructuredIndexBufferUnbounded(int                          descriptorTableIndex,
                                              ComPtr<ID3D12DescriptorHeap> bufferDescriptorHeap,
                                              bool                         isCompute = false);

    void allocateUploadBuffer(ID3D12Device*    pDevice,
                              void*            pData,
                              UINT64           datasize,
                              ID3D12Resource** ppResource,
                              const wchar_t*   resourceName = nullptr);
};