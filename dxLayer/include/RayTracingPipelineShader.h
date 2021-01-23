#pragma once
#include "Entity.h"
#include "RenderTexture.h"
#include "d3d12.h"
#include "d3d12_1.h"
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

class RayTracingPipelineShader
{
    UINT _allocateDescriptor(D3D12_CPU_DESCRIPTOR_HANDLE* cpuDescriptor,
                             UINT                         descriptorIndexToUse = UINT_MAX);

    void _updateTLASData(int tlasCount);
    void _updateInstanceData();
    void _updateBlasData();

    std::vector<D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS> _bottomLevelBuildDescs;
    std::vector<Model*>                                               _bottomLevelBuildModels;
    // Top level needs to be buffered in the case of growing allocations
    ComPtr<ID3D12Resource>                                            _topLevelAccelerationStructure[CMD_LIST_NUM];
    ComPtr<ID3D12Resource>                                            _tlScratchResource[CMD_LIST_NUM];
    ComPtr<ID3D12Resource>                                            _instanceDescs[CMD_LIST_NUM];
    UINT                                                              _descriptorsAllocated;
    D3D12_DESCRIPTOR_HEAP_DESC                                        _descriptorHeapDesc;
    ComPtr<ID3D12DescriptorHeap>                                      _descriptorHeap;
    UINT                                                              _descriptorSize;
    std::vector<D3D12_RAYTRACING_INSTANCE_DESC>                       _instanceDesc;
    dxc::DxcDllSupport                                                _dllSupport;
    ComPtr<ID3D12Device5>                                             _dxrDevice;
    D3D12_UNORDERED_ACCESS_VIEW_DESC                                  _uavDesc;
    D3D12_SHADER_RESOURCE_VIEW_DESC                                   _uvStructuredBuffer;
    ComPtr<ID3D12DescriptorHeap>                                      _uvStructuredBufferDescHeap;
    bool                                                              _initUvStructuredBuffer = false;
    ComPtr<ID3D12DescriptorHeap>                                      _rtASDescriptorHeap;
    D3D12_DESCRIPTOR_HEAP_DESC                                        _rtASSrvHeapDesc;
    D3D12_SHADER_RESOURCE_VIEW_DESC                                   _rtASSrvDesc;
    std::vector<float>                                                _instanceNormalMatrixTransforms;
    std::vector<float>                                                _instanceWorldToObjectMatrixTransforms;
    std::vector<float>                                                _instanceTransforms;
    std::vector<float>                                                _prevInstanceTransforms;
    bool                                                              _setTLASRootSRV = true;
    bool                                                              _useCompaction = true;
    int                                                               _topLevelIndex = 0;
    int                                                               _instanceMappingIndex = 0;

    std::map<Model*, std::vector<D3DBuffer*>>                         _vertexBuffer;
    std::map<Model*, std::vector<D3DBuffer*>>                         _indexBuffer;
    std::map<Model*, std::vector<AssetTexture*>>                      _modelTextures;
    std::map<Model*, RTCompaction::ASBuffers*>                        _blas;

    std::map<Model*, UINT>                                            _attributeMap;
    std::vector<UINT>                                                 _attributeMapping;

    std::map<Model*, std::vector<float>>                              _transmissionMap;
    std::vector<float>                                                _transmissionMapping;

    std::map<Model*, UINT>                                            _materialMap;
    std::vector<UINT>                                                 _materialMapping;

    ComPtr<ID3D12Resource>                                            _instanceIndexToMaterialMappingUpload[CMD_LIST_NUM];
    D3DBuffer*                                                        _instanceIndexToMaterialMappingGPUBuffer;

    ComPtr<ID3D12Resource>                                            _instanceIndexToAttributeMappingUpload[CMD_LIST_NUM];
    D3DBuffer*                                                        _instanceIndexToAttributeMappingGPUBuffer;

    ComPtr<ID3D12Resource>                                            _instanceTransmissionMappingUpload[CMD_LIST_NUM];
    D3DBuffer*                                                        _instanceTransmissionMappingGPUBuffer;

    ComPtr<ID3D12Resource>                                            _instanceNormalMatrixTransformsUpload[CMD_LIST_NUM];
    D3DBuffer*                                                        _instanceNormalMatrixTransformsGPUBuffer;

    ComPtr<ID3D12DescriptorHeap>                                      _unboundedTextureSrvDescriptorHeap;
    UINT                                                              _unboundedTextureSrvIndex;
    ComPtr<ID3D12DescriptorHeap>                                      _unboundedAttributeBufferSrvDescriptorHeap;
    UINT                                                              _unboundedAttributeBufferSrvIndex;
    ComPtr<ID3D12DescriptorHeap>                                      _unboundedIndexBufferSrvDescriptorHeap;
    UINT                                                              _unboundedIndexBufferSrvIndex;

    bool                                                              _doneAdding;

  public:
    RayTracingPipelineShader();
    void                                          init(ComPtr<ID3D12Device> device);
    ComPtr<ID3D12DescriptorHeap>                  getRTASDescHeap();
    D3D12_GPU_VIRTUAL_ADDRESS                     getRTASGPUVA();
    ComPtr<ID3D12DescriptorHeap>                  getDescHeap();
    std::map<Model*, std::vector<AssetTexture*>>& getSceneTextures();
    std::vector<UINT>&                            getMaterialMapping() { return _materialMapping; }
    std::vector<UINT>&                            getAttributeMapping() { return _attributeMapping; }
    D3DBuffer*                                    getMaterialMappingBuffer() { return _instanceIndexToMaterialMappingGPUBuffer; }
    void                                          updateAndBindMaterialBuffer(std::map<std::string, UINT> resourceIndexes);
    void                                          updateAndBindAttributeBuffer(std::map<std::string, UINT> resourceIndexes);
    void                                          updateAndBindTransmissionBuffer(std::map<std::string, UINT> resourceIndexes);
    void                                          updateAndBindNormalMatrixBuffer(std::map<std::string, UINT> resourceIndexes);
    std::map<Model*, std::vector<D3DBuffer*>>&    getVertexBuffers() { return _vertexBuffer; }
    std::map<Model*, std::vector<D3DBuffer*>>&    getIndexBuffers() { return _indexBuffer; }
    float*                                        getInstanceNormalTransforms() { return _instanceNormalMatrixTransforms.data(); }
    float*                                        getWorldToObjectTransforms() { return _instanceWorldToObjectMatrixTransforms.data(); }
    float*                                        getPrevInstanceTransforms() { return _prevInstanceTransforms.data(); }
    int                                           getBLASCount() { return _blas.size(); }
    void                                          buildAccelerationStructures();
    UINT                                          createBufferSRV(D3DBuffer* buffer, UINT numElements, UINT elementSize);
    void                                          buildBLAS(Entity* entity);

    void createUnboundedTextureSrvDescriptorTable(UINT descriptorTableEntries);
    void createUnboundedAttributeBufferSrvDescriptorTable(UINT descriptorTableEntries);
    void createUnboundedIndexBufferSrvDescriptorTable(UINT descriptorTableEntries);

    void addSRVToUnboundedTextureDescriptorTable(Texture* texture);
    void addSRVToUnboundedAttributeBufferDescriptorTable(D3DBuffer* vertexBuffer, UINT vertexCount, UINT offset);
    void addSRVToUnboundedIndexBufferDescriptorTable(D3DBuffer* indexBuffer, UINT indexCount, UINT offset);

    void resetUnboundedTextureDescriptorTable()         { _unboundedTextureSrvIndex = 0; }
    void resetUnboundedAttributeBufferDescriptorTable() { _unboundedAttributeBufferSrvIndex = 0; }
    void resetUnboundedIndexBufferDescriptorTable()     { _unboundedIndexBufferSrvIndex = 0; }

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