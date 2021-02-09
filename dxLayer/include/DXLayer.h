#pragma once

#include "ConstantBuffer.h"
#include "D3d12SDKLayers.h"
#include "DXGI.h"
#include "Entity.h"
#include "PresentTarget.h"
#include "ResourceManager.h"
#include "ResourceBuffer.h"
#include "d3dx12.h"
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <d3d12.h>
#include <vector>
#include <wrl.h>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"
#include "DXDefines.h"
#include <mutex>

#pragma comment(lib, "D3D12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxguid.lib")

using namespace Microsoft::WRL;

class DXLayer
{
  public:
    static void                        initialize(HINSTANCE hInstance, int cmdShow);
    bool                               supportsRayTracing();
    void                               flushCommandList(RenderTexture* renderFrame);
    void                               fenceCommandList();
    ComPtr<ID3D12CommandAllocator>     getCmdAllocator();
    UINT                               getCmdListIndex();
    void                               initCmdLists();
    ComPtr<ID3D12CommandQueue>         getGfxCmdQueue();
    ComPtr<ID3D12CommandQueue>         getComputeCmdQueue();
    ComPtr<ID3D12CommandQueue>         getCopyCmdQueue();
    ComPtr<ID3D12GraphicsCommandList4> getAttributeBufferCopyCmdList();
    ComPtr<ID3D12GraphicsCommandList4> getTextureCopyCmdList();
    ComPtr<ID3D12GraphicsCommandList4> getComputeCmdList();
    ComPtr<ID3D12GraphicsCommandList4> getCmdList();
    ComPtr<ID3D12Device>               getDevice();
    static DXLayer*                    instance();
    void                               addCmdListIndex();
    void                               getTimestamp(int cmdListIndex);
    void                               setTimeStamp();
    bool                               usingAsyncCompute();
    void                               lock();
    void                               unlock();
    void                               queryDeviceError();

  private:
    DXLayer(HINSTANCE hInstance, int cmdShow);
    ~DXLayer();

    const DXGI_FORMAT                  _rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

    ComPtr<ID3D12Device>               _device;
    ComPtr<ID3D12CommandQueue>         _gfxCmdQueue;
    ComPtr<ID3D12CommandQueue>         _computeCmdQueue;
    ComPtr<ID3D12CommandQueue>         _copyCmdQueue;

    bool                               _cmdListFinishedExecution[CMD_LIST_NUM];
                                       
    int                                _gfxNextFenceValue[CMD_LIST_NUM];
    int                                _computeNextFenceValue[CMD_LIST_NUM];
    int                                _copyNextFenceValue[CMD_LIST_NUM];

    ComPtr<ID3D12CommandAllocator>     _gfxCmdAllocator[CMD_LIST_NUM];
    ComPtr<ID3D12CommandAllocator>     _computeCmdAllocator[CMD_LIST_NUM];
    ComPtr<ID3D12CommandAllocator>     _copyAttributeBufferCmdAllocator[CMD_LIST_NUM];
    ComPtr<ID3D12CommandAllocator>     _copyTextureCmdAllocator[CMD_LIST_NUM];

    ComPtr<ID3D12Fence>                _gfxCmdListFence[CMD_LIST_NUM];
    ComPtr<ID3D12Fence>                _computeCmdListFence[CMD_LIST_NUM];
    ComPtr<ID3D12Fence>                _copyCmdListFence[CMD_LIST_NUM];

    ComPtr<ID3D12GraphicsCommandList4> _gfxCmdLists[CMD_LIST_NUM];
    ComPtr<ID3D12GraphicsCommandList4> _computeCmdLists[CMD_LIST_NUM];
    ComPtr<ID3D12GraphicsCommandList4> _attributeBufferCopyCmdLists[CMD_LIST_NUM];
    ComPtr<ID3D12GraphicsCommandList4> _textureCopyCmdLists[CMD_LIST_NUM];


    bool                               _rayTracingEnabled;
    PresentTarget*                     _presentTarget;
    int                                _cmdListIndex;
    int                                _cmdShow;
    static DXLayer*                    _dxLayer;
    HWND                               _window;
    UINT                               _presentCounter = 0;
    std::mutex                         _mtx;
    std::queue<UINT>                   _pendingCmdListIndices;
    ComPtr<ID3D12QueryHeap>            _queryHeap;
    ComPtr<ID3D12Resource>             _queryResult;
    uint64_t                           _previousTimeStamp;
    uint64_t                           _timeStampFrequency;
    uint64_t                           _timeStampIndex[CMD_LIST_NUM];
    ComPtr<ID3D12DescriptorHeap>       _descHeapForImguiFonts;
    D3D12_DESCRIPTOR_HEAP_DESC         _descHeapDesc;
    std::string                        _perfData;
    bool                               _useAsyncCompute = false;
    bool                               _useAsyncCopyInFrame = false;
    std::queue<uint32_t>               _finishedCmdLists;
    clock_t                            _previousFrameTime;
    clock_t                            _previousCpuTime;
    std::mutex                         _lock;
    int                                _frameIndex;
};