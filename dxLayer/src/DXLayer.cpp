#include "DXLayer.h"
#include "HLSLShader.h"
#include "IOEventDistributor.h"
#include "Light.h"
#include "ModelBroker.h"
#include "ShaderBroker.h"
#include "EngineManager.h"

DXLayer* DXLayer::_dxLayer = nullptr;

DXLayer::DXLayer(HINSTANCE hInstance, int cmdShow) : _cmdShow(cmdShow), _cmdListIndex(0)
{

    WNDCLASSEX windowClass;

    _rayTracingEnabled     = false;

    ZeroMemory(&windowClass, sizeof(WNDCLASSEX));

    windowClass.cbSize        = sizeof(WNDCLASSEX);
    windowClass.style         = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc   = IOEventDistributor::dxEventLoop;
    windowClass.hInstance     = hInstance;
    windowClass.hCursor       = LoadCursor(NULL, IDC_ARROW);
    windowClass.lpszClassName = "manawar-engine";
    windowClass.hbrBackground = CreateSolidBrush(RGB(0, 0, 0));

    RegisterClassEx(&windowClass);

    int width  = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    // windowed fullscreen
    _window = CreateWindowEx(NULL, windowClass.lpszClassName, windowClass.lpszClassName,
                             WS_OVERLAPPEDWINDOW, 0, 0, width, height, NULL, NULL, hInstance, NULL);

    // borderless fullscreen
    //_window = CreateWindowEx(NULL, windowClass.lpszClassName, windowClass.lpszClassName,
    //               WS_EX_TOPMOST | WS_POPUP,
    //               0, 0, width, height, NULL, NULL, hInstance, NULL);

    RECT rect = {0};
    GetWindowRect(_window, &rect);
    IOEventDistributor::screenPixelWidth  = rect.right - rect.left;
    IOEventDistributor::screenPixelHeight = rect.bottom - rect.top;

#ifdef _DEBUG
    ComPtr<ID3D12Debug> debug;
    D3D12GetDebugInterface(IID_PPV_ARGS(debug.GetAddressOf()));
    debug->EnableDebugLayer();
#endif

    D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(_device.GetAddressOf()));

    D3D12_COMMAND_QUEUE_DESC cqDesc;
    ZeroMemory(&cqDesc, sizeof(cqDesc));

    
    // Direct Command queue
    cqDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    _device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(_gfxCmdQueue.GetAddressOf()));

    // Compute Command queue
     cqDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    _device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(_computeCmdQueue.GetAddressOf()));

    // Copy Command queue
    cqDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    _device->CreateCommandQueue(&cqDesc, IID_PPV_ARGS(_copyCmdQueue.GetAddressOf()));

    // Initialize queues
    for (int i = 0; i < CMD_LIST_NUM; i++)
    {
        // Gfx stuff
        _device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                        IID_PPV_ARGS(_gfxCmdAllocator[i].GetAddressOf()));

        _device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _gfxCmdAllocator[i].Get(), nullptr,
                                   IID_PPV_ARGS(_gfxCmdLists[i].GetAddressOf()));
        _gfxCmdLists[i]->Close();

        _gfxCmdAllocator[i]->Reset();
        _gfxCmdLists[i]->Reset(_gfxCmdAllocator[i].Get(), nullptr);
        _gfxCmdLists[i]->Close();

        _cmdListFinishedExecution[i] = true;

        _gfxNextFenceValue[i] = 1;
        _timeStampIndex[i] = 0;

        _device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                             IID_PPV_ARGS(_gfxCmdListFence[i].GetAddressOf()));

        // Compute stuff
        _device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COMPUTE,
                                        IID_PPV_ARGS(_computeCmdAllocator[i].GetAddressOf()));

        _device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COMPUTE, _computeCmdAllocator[i].Get(), nullptr,
                                   IID_PPV_ARGS(_computeCmdLists[i].GetAddressOf()));
        _computeCmdLists[i]->Close();

        _computeCmdAllocator[i]->Reset();
        _computeCmdLists[i]->Reset(_computeCmdAllocator[i].Get(), nullptr);
        _computeCmdLists[i]->Close();

        _computeNextFenceValue[i] = 1;

        _device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                             IID_PPV_ARGS(_computeCmdListFence[i].GetAddressOf()));

        // Copy stuff
        _device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY,
                                        IID_PPV_ARGS(_copyAttributeBufferCmdAllocator[i].GetAddressOf()));

        _device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY,
                                   _copyAttributeBufferCmdAllocator[i].Get(), nullptr,
                                   IID_PPV_ARGS(_attributeBufferCopyCmdLists[i].GetAddressOf()));
        _attributeBufferCopyCmdLists[i]->Close();

        _copyAttributeBufferCmdAllocator[i]->Reset();
        _attributeBufferCopyCmdLists[i]->Reset(_copyAttributeBufferCmdAllocator[i].Get(), nullptr);
        _attributeBufferCopyCmdLists[i]->Close();

        _device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY,
                                        IID_PPV_ARGS(_copyTextureCmdAllocator[i].GetAddressOf()));

        _device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_COPY,
                                   _copyTextureCmdAllocator[i].Get(), nullptr,
                                   IID_PPV_ARGS(_textureCopyCmdLists[i].GetAddressOf()));
        _textureCopyCmdLists[i]->Close();

        _copyTextureCmdAllocator[i]->Reset();
        _textureCopyCmdLists[i]->Reset(_copyTextureCmdAllocator[i].Get(), nullptr);
        _textureCopyCmdLists[i]->Close();

        _copyNextFenceValue[i] = 1;

        _device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                             IID_PPV_ARGS(_copyCmdListFence[i].GetAddressOf()));

        _finishedCmdLists.push(i);
    }

    _gfxCmdAllocator[0]->Reset();
    _gfxCmdLists[0]->Reset(_gfxCmdAllocator[0].Get(), nullptr);

    _computeCmdAllocator[0]->Reset();
    _computeCmdLists[0]->Reset(_computeCmdAllocator[0].Get(), nullptr);

    _copyAttributeBufferCmdAllocator[0]->Reset();
    _attributeBufferCopyCmdLists[0]->Reset(_copyAttributeBufferCmdAllocator[0].Get(), nullptr);
    _copyTextureCmdAllocator[0]->Reset();
    _textureCopyCmdLists[0]->Reset(_copyTextureCmdAllocator[0].Get(), nullptr);

    // Describe and create a heap for timestamp queries
    D3D12_QUERY_HEAP_DESC queryHeapDesc = {};
    queryHeapDesc.Count                 = TIME_QUERY_COUNT;
    queryHeapDesc.Type                  = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    _device->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&_queryHeap));

    auto readbackHeapProperties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK);
    auto timeQueryDataSize      = sizeof(uint64_t);
    auto bufferDesc             = CD3DX12_RESOURCE_DESC::Buffer(timeQueryDataSize * TIME_QUERY_COUNT);

    _device->CreateCommittedResource(&readbackHeapProperties,
                                     D3D12_HEAP_FLAG_NONE,
                                     &bufferDesc,
                                     D3D12_RESOURCE_STATE_COPY_DEST,
                                     nullptr,
                                     IID_PPV_ARGS(&_queryResult));

    _gfxCmdQueue->GetTimestampFrequency(&_timeStampFrequency);

    _presentTarget =
        new PresentTarget(_device, _rtvFormat, _gfxCmdQueue, IOEventDistributor::screenPixelWidth,
                          IOEventDistributor::screenPixelHeight, _window);

    // Test DXR support
    ComPtr<ID3D12Device5>              dxrDevice;
    ComPtr<ID3D12GraphicsCommandList4> dxrCommandList;

    if (_device->QueryInterface(IID_PPV_ARGS(&dxrDevice)) == S_OK &&
        _gfxCmdLists[0]->QueryInterface(IID_PPV_ARGS(&dxrCommandList)) == S_OK)
    {
        _rayTracingEnabled = true;
    }

    ShowCursor(false);

    // show the window
    ShowWindow(_window, _cmdShow);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    // io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    
    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsClassic();
    
    // Descriptor heaps
    ZeroMemory(&_descHeapDesc, sizeof(_descHeapDesc));
    _descHeapDesc.NumDescriptors = 1;
    _descHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    _descHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    _device->CreateDescriptorHeap(&_descHeapDesc,
                                  IID_PPV_ARGS(&_descHeapForImguiFonts));

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(_window);
    ImGui_ImplDX12_Init(_device.Get(), CMD_LIST_NUM, DXGI_FORMAT_R8G8B8A8_UNORM, _descHeapForImguiFonts.Get(),
                        _descHeapForImguiFonts->GetCPUDescriptorHandleForHeapStart(),
                        _descHeapForImguiFonts->GetGPUDescriptorHandleForHeapStart());

    _previousFrameTime = clock();

    _frameIndex = 0;
}

void DXLayer::initialize(HINSTANCE hInstance, int cmdShow)
{

    if (_dxLayer == nullptr)
    {
        _dxLayer = new DXLayer(hInstance, cmdShow);
    }
}

DXLayer* DXLayer::instance() { return _dxLayer; }

DXLayer::~DXLayer() {}

bool DXLayer::supportsRayTracing() { return _rayTracingEnabled; }
void DXLayer::addCmdListIndex() { _pendingCmdListIndices.push(_cmdListIndex); }

void DXLayer::lock() { _lock.lock(); }
void DXLayer::unlock() { _lock.unlock(); }

void DXLayer::initCmdLists()
{
    _previousCpuTime = clock();

    // Sleep loop randomly selecting an available command list
    bool foundCommandList = false;
    while (foundCommandList == false)
    {
        for (int i = 0; i < CMD_LIST_NUM; i++)
        {
            if (_cmdListFinishedExecution[i])
            {
                _cmdListIndex = i;
                foundCommandList = true;
                break;
            }
        }
        if (foundCommandList == false)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(0));
        }
    }

    _cmdListFinishedExecution[_cmdListIndex] = false;
    // Open command list
    _gfxCmdAllocator[_cmdListIndex]->Reset();
    _gfxCmdLists[_cmdListIndex]->Reset(_gfxCmdAllocator[_cmdListIndex].Get(), nullptr);

    if (_useAsyncCompute)
    {
        _computeCmdAllocator[_cmdListIndex]->Reset();
        _computeCmdLists[_cmdListIndex]->Reset(_computeCmdAllocator[_cmdListIndex].Get(), nullptr);
    }

    if (_useAsyncCopyInFrame)
    {
        _copyAttributeBufferCmdAllocator[_cmdListIndex]->Reset();
        _attributeBufferCopyCmdLists[_cmdListIndex]->Reset(_copyAttributeBufferCmdAllocator[_cmdListIndex].Get(), nullptr);
        _copyTextureCmdAllocator[_cmdListIndex]->Reset();
        _textureCopyCmdLists[_cmdListIndex]->Reset(_copyTextureCmdAllocator[_cmdListIndex].Get(), nullptr);
    }
}

bool DXLayer::usingAsyncCompute()
{
    return _useAsyncCompute;
}

void DXLayer::flushCommandList(RenderTexture* renderFrame)
{

    UINT prevCmdListIndex = _pendingCmdListIndices.front();
    _pendingCmdListIndices.pop();

    //{
    //    // Submit the copy command list
    //    //_textureCopyCmdLists[0]->Close();
    //    _copyCmdQueue->ExecuteCommandLists(
    //        1, CommandListCast(_textureCopyCmdLists[0].GetAddressOf()));
    //    
    //    int fenceValue = _copyNextFenceValue[0]++;
    //    _copyCmdQueue->Signal(_copyCmdListFence[0].Get(), fenceValue);
    //    
    //    // Wait for just-submitted command list to finish
    //    HANDLE fenceWriteEventECL = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    //    _copyCmdListFence[0]->SetEventOnCompletion(fenceValue, fenceWriteEventECL);
    //    WaitForSingleObject(fenceWriteEventECL, INFINITE);
    //}

    clock_t currTime   = clock();
    clock_t timePassed = currTime - _previousCpuTime;
    _previousCpuTime   = clock();

    double cpuMilliSecondsPerFrame = ((double)timePassed / (double)CLOCKS_PER_SEC) * 1000.0;

    //_perfData += ("CPU command list " + std::to_string(prevCmdListIndex) + " generation time: " + std::to_string(cpuMilliSecondsPerFrame) + "mS\n");

    D3D12_RESOURCE_BARRIER barrierDesc;
    ZeroMemory(&barrierDesc, sizeof(barrierDesc));

    barrierDesc.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc.Transition.pResource   = renderFrame->getResource()->getResource().Get();
    barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrierDesc.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    auto rtvDescriptorSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    _gfxCmdLists[prevCmdListIndex]->ResourceBarrier(1, &barrierDesc);

    // Viewport
    D3D12_VIEWPORT viewPort = {0.0f,
                               0.0f,
                               static_cast<float>(IOEventDistributor::screenPixelWidth),
                               static_cast<float>(IOEventDistributor::screenPixelHeight),
                               0.0f,
                               1.0f};
    // Scissor rectangle
    D3D12_RECT rectScissor = {0, 0, static_cast<LONG>(IOEventDistributor::screenPixelWidth),
                              static_cast<LONG>(IOEventDistributor::screenPixelHeight)};

    _gfxCmdLists[prevCmdListIndex]->RSSetViewports(1, &viewPort);
    _gfxCmdLists[prevCmdListIndex]->RSSetScissorRects(1, &rectScissor);

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(renderFrame->getCPUHandle());
    _gfxCmdLists[prevCmdListIndex]->OMSetRenderTargets(1, &rtvHandle, false, nullptr);
    _gfxCmdLists[prevCmdListIndex]->SetDescriptorHeaps(1, _descHeapForImguiFonts.GetAddressOf());

    _mtx.lock();

    // Start the Dear ImGui frame
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    ImGui::SetNextWindowSize(ImVec2(500, 500));

    ImGui::Begin("Perf window");

    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                1000.0f / ImGui::GetIO().Framerate,
                ImGui::GetIO().Framerate);

    ImGui::Text(_perfData.c_str());

    ImGui::Text(RTCompaction::GetLog().c_str());

    auto entityList = EngineManager::instance()->getEntityList();
    RayTracingPipelineShader* rtPipeline = EngineManager::getRTPipeline();

    ImGui::Text(("TLAS count: " + std::to_string(entityList->size())).c_str());
    ImGui::Text(("BLAS count: " + std::to_string(rtPipeline->getBLASCount())).c_str());

    ImGui::End();

    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), _gfxCmdLists[prevCmdListIndex].Get());

    _mtx.unlock();

    ZeroMemory(&barrierDesc, sizeof(barrierDesc));

    barrierDesc.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc.Transition.pResource   = renderFrame->getResource()->getResource().Get();
    barrierDesc.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrierDesc.Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    _gfxCmdLists[prevCmdListIndex]->ResourceBarrier(1, &barrierDesc);

    if (_useAsyncCompute)
    {
        // Submit the async compute command list for acceleration structure build
        _computeCmdLists[prevCmdListIndex]->Close();
        _computeCmdQueue->ExecuteCommandLists(1, CommandListCast(_computeCmdLists[prevCmdListIndex].GetAddressOf()));

        int fenceValue = _computeNextFenceValue[prevCmdListIndex]++;
        _computeCmdQueue->Signal(_computeCmdListFence[prevCmdListIndex].Get(), fenceValue);

        HANDLE fenceWriteEventECL = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        // Wait for just-submitted command list to finish
        _computeCmdListFence[prevCmdListIndex]->SetEventOnCompletion(fenceValue, fenceWriteEventECL);
        WaitForSingleObject(fenceWriteEventECL, INFINITE);
    }

    _mtx.lock();
    
    _frameIndex++;

    UINT backBufferIndex = _presentCounter++;

    // Copy Resource method
    D3D12_RESOURCE_BARRIER barrierBatchDesc[2] = {};

    barrierBatchDesc[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierBatchDesc[0].Transition.pResource   = renderFrame->getResource()->getResource().Get();
    barrierBatchDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierBatchDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrierBatchDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;

    barrierBatchDesc[1].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierBatchDesc[1].Transition.pResource   = _presentTarget->getBackBuffer(backBufferIndex % NUM_SWAP_CHAIN_BUFFERS).Get();
    barrierBatchDesc[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierBatchDesc[1].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrierBatchDesc[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_DEST;

    _gfxCmdLists[prevCmdListIndex]->ResourceBarrier(2, &barrierBatchDesc[0]);

    _gfxCmdLists[prevCmdListIndex]->CopyResource(_presentTarget->getBackBuffer(backBufferIndex % NUM_SWAP_CHAIN_BUFFERS).Get(),
                                                 renderFrame->getResource()->getResource().Get());

    barrierBatchDesc[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierBatchDesc[0].Transition.pResource   = renderFrame->getResource()->getResource().Get();
    barrierBatchDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierBatchDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    barrierBatchDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    barrierBatchDesc[1].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierBatchDesc[1].Transition.pResource   = _presentTarget->getBackBuffer(backBufferIndex % NUM_SWAP_CHAIN_BUFFERS).Get();
    barrierBatchDesc[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierBatchDesc[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrierBatchDesc[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_PRESENT;

    _gfxCmdLists[prevCmdListIndex]->ResourceBarrier(2, &barrierBatchDesc[0]);

    getTimestamp(prevCmdListIndex);

    // Submit the current command list
    _gfxCmdLists[prevCmdListIndex]->Close();

    _gfxCmdQueue->ExecuteCommandLists(1, CommandListCast(_gfxCmdLists[prevCmdListIndex].GetAddressOf()));

    _presentTarget->present();

    _mtx.unlock();

    int fenceValue = _gfxNextFenceValue[prevCmdListIndex]++;
    _gfxCmdQueue->Signal(_gfxCmdListFence[prevCmdListIndex].Get(), fenceValue);

    // Wait for just-submitted command list to finish
    HANDLE fenceWriteEventBlit = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    _gfxCmdListFence[prevCmdListIndex]->SetEventOnCompletion(fenceValue, fenceWriteEventBlit);
    WaitForSingleObject(fenceWriteEventBlit, INFINITE);

    _finishedCmdLists.push(prevCmdListIndex);
    _cmdListFinishedExecution[prevCmdListIndex] = true;
}

void DXLayer::setTimeStamp()
{
    _gfxCmdLists[_cmdListIndex]->EndQuery(_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                      (_cmdListIndex * TIME_QUERIES_PER_CMD_LIST) + _timeStampIndex[_cmdListIndex]);
    _timeStampIndex[_cmdListIndex]++;
}

void DXLayer::getTimestamp(int cmdListIndex)
{
    _gfxCmdLists[cmdListIndex]->ResolveQueryData(_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                               cmdListIndex * TIME_QUERIES_PER_CMD_LIST, TIME_QUERIES_PER_CMD_LIST,
                                               _queryResult.Get(), 0);

    BYTE* mappedData = nullptr;
    _queryResult->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));

    double gpuMilliSecondsPerFrame = 0.0;
    _perfData                      = "";

    std::string regimeNames[] = 
    {
        "Acceleration Structure Build", "Primary Rays", "Sun Rays", "Reflection Rays", "Denoiser", "", "", "", ""
    };

    for (int i = 0; i < 8; i++)
    {
        uint64_t timeStamp = 0;
        memcpy(&timeStamp, &mappedData[i*8], 8);

        // To grab first timestamp to start comparing delta time
        if (i > 0 && i < _timeStampIndex[cmdListIndex])
        {
            double inverseTicksPassed = 1.0 / ((double)timeStamp - _previousTimeStamp);
            double frequency          = inverseTicksPassed * (double)_timeStampFrequency;
            double milliSecondsPassed = (1.0 / frequency) * 1000.0f;

            gpuMilliSecondsPerFrame += milliSecondsPassed;

            _perfData += (regimeNames[i-1] + " workload time spent: " + std::to_string(milliSecondsPassed) + "mS\n");
        }

        _previousTimeStamp = timeStamp;
    }
    _perfData += ("Frame GPU time: " + std::to_string(gpuMilliSecondsPerFrame) + "mS\n");

    clock_t currTime               = clock();
    clock_t timePassed             = currTime - _previousFrameTime;
    _previousFrameTime             = currTime;
    double cpuMilliSecondsPerFrame = ((double)timePassed / (double)CLOCKS_PER_SEC) * 1000.0;

    _perfData += ("Frame CPU time: " + std::to_string(cpuMilliSecondsPerFrame) + "mS\n");

    _perfData += ("GPU idle time: " + std::to_string(cpuMilliSecondsPerFrame - gpuMilliSecondsPerFrame) + "mS\n");

    _timeStampIndex[cmdListIndex] = 0;
}

void DXLayer::fenceCommandList()
{
    {
    // Submit the copy command list
    _attributeBufferCopyCmdLists[_cmdListIndex]->Close();
    _copyCmdQueue->ExecuteCommandLists(1, CommandListCast(_attributeBufferCopyCmdLists[_cmdListIndex].GetAddressOf()));

    int fenceValue = _copyNextFenceValue[_cmdListIndex]++;
    _copyCmdQueue->Signal(_copyCmdListFence[_cmdListIndex].Get(), fenceValue);

    // Wait for just-submitted command list to finish
    HANDLE fenceWriteEventECL = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    _copyCmdListFence[_cmdListIndex]->SetEventOnCompletion(fenceValue, fenceWriteEventECL);
    WaitForSingleObject(fenceWriteEventECL, INFINITE);

    // Submit the copy command list
    _textureCopyCmdLists[_cmdListIndex]->Close();
    _copyCmdQueue->ExecuteCommandLists(
        1, CommandListCast(_textureCopyCmdLists[_cmdListIndex].GetAddressOf()));

    fenceValue = _copyNextFenceValue[_cmdListIndex]++;
    _copyCmdQueue->Signal(_copyCmdListFence[_cmdListIndex].Get(), fenceValue);

    // Wait for just-submitted command list to finish
    fenceWriteEventECL = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    _copyCmdListFence[_cmdListIndex]->SetEventOnCompletion(fenceValue, fenceWriteEventECL);
    WaitForSingleObject(fenceWriteEventECL, INFINITE);
    }

    auto modelBroker = ModelBroker::instance();
    auto texBroker   = TextureBroker::instance();
    texBroker->releaseUploadBuffers();

    auto modelMap = modelBroker->getModels();
    for (auto modelKey : modelMap)
    {
        (*modelKey.second->getVAO())[0]->getVertexResource()->getUploadResource()->Release();
        (*modelKey.second->getVAO())[0]->getIndexResource()->getUploadResource()->Release();
    }

    {
    // Submit the graphics command list
    _gfxCmdLists[_cmdListIndex]->Close();
    _gfxCmdQueue->ExecuteCommandLists(1, CommandListCast(_gfxCmdLists[_cmdListIndex].GetAddressOf()));

    int fenceValue = _gfxNextFenceValue[_cmdListIndex]++;
    _gfxCmdQueue->Signal(_gfxCmdListFence[_cmdListIndex].Get(), fenceValue);

    // Wait for just-submitted command list to finish
    HANDLE fenceWriteEventECL = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    _gfxCmdListFence[_cmdListIndex]->SetEventOnCompletion(fenceValue, fenceWriteEventECL);
    WaitForSingleObject(fenceWriteEventECL, INFINITE);
    }

    {
    // Submit the compute command list
    _computeCmdLists[_cmdListIndex]->Close();
    _computeCmdQueue->ExecuteCommandLists(1, CommandListCast(_computeCmdLists[_cmdListIndex].GetAddressOf()));

    int fenceValue = _computeNextFenceValue[_cmdListIndex]++;
    _computeCmdQueue->Signal(_computeCmdListFence[_cmdListIndex].Get(), fenceValue);

    // Wait for just-submitted command list to finish
    HANDLE fenceWriteEventECL = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    _computeCmdListFence[_cmdListIndex]->SetEventOnCompletion(fenceValue, fenceWriteEventECL);
    WaitForSingleObject(fenceWriteEventECL, INFINITE);
    }

}

ComPtr<ID3D12Device>               DXLayer::getDevice()          { return _device; }
ComPtr<ID3D12GraphicsCommandList4> DXLayer::getCmdList()         { return _gfxCmdLists[_cmdListIndex]; }
ComPtr<ID3D12GraphicsCommandList4> DXLayer::getComputeCmdList()  { return _computeCmdLists[_cmdListIndex];}
ComPtr<ID3D12GraphicsCommandList4> DXLayer::getAttributeBufferCopyCmdList() { return _attributeBufferCopyCmdLists[_cmdListIndex];}
ComPtr<ID3D12GraphicsCommandList4> DXLayer::getTextureCopyCmdList() { return _textureCopyCmdLists[_cmdListIndex];}

ComPtr<ID3D12CommandAllocator>     DXLayer::getCmdAllocator()    { return _gfxCmdAllocator[_cmdListIndex]; }
ComPtr<ID3D12CommandQueue>         DXLayer::getGfxCmdQueue()     { return _gfxCmdQueue; }
ComPtr<ID3D12CommandQueue>         DXLayer::getComputeCmdQueue() { return _computeCmdQueue; }
ComPtr<ID3D12CommandQueue>         DXLayer::getCopyCmdQueue()    { return _copyCmdQueue; }
UINT                               DXLayer::getCmdListIndex()    { return _cmdListIndex; }
