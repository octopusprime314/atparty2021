#include "RTCompaction.h"

namespace RTCompaction
{

    constexpr uint32_t SizeOfCompactionDescriptor           = sizeof(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE_DESC);
    constexpr uint32_t CompactionSizeSuballocationBlockSize = 65536;

    BufferSuballocator*    _scratchPool                  = nullptr;
    BufferSuballocator*    _resultPool                   = nullptr;
    BufferSuballocator*    _compactionPool               = nullptr;
    BufferSuballocator*    _compactionSizeGpuPool        = nullptr;
    BufferSuballocator*    _compactionSizeCpuPool        = nullptr;
    std::string            _buildLogger                  = "";
    uint32_t               _commandListLatency           = 0;
    uint64_t               _commandListIndex             = 0;
    uint32_t               _maxTransientCompactionMemory = 0;
    uint32_t               _suballocationBlockSize       = 0;
    uint32_t               _currentCompactionMemorySize  = 0;
    uint32_t               _totalUncompactedMemory       = 0;
    uint32_t               _totalCompactedMemory         = 0;
    std::queue<ASBuffers*> _asBufferCompactionQueue;
    std::queue<ASBuffers*> _asBufferCompleteQueue;
    std::queue<ASBuffers*> _asBufferReleaseQueue;

    void NextFrame(ID3D12Device5* const              device,
                   ID3D12GraphicsCommandList4* const commandList)
    {
        //Clear out previous command lists compaction logging
        _buildLogger.clear();

        _buildLogger.append(
            "Uncompacted memory: "                               + std::to_string((_totalUncompactedMemory                        ) / 1000000.0f) + " MB\n"
            "Compacted memory: "                                 + std::to_string((_totalCompactedMemory                          ) / 1000000.0f) + " MB\n"
            "Memory saved from compaction: "                     + std::to_string((_totalUncompactedMemory - _totalCompactedMemory) / 1000000.0f) + " MB\n"
            "Uncompacted suballocator memory: "                  + std::to_string((_resultPool->GetSuballocatorSize()             ) / 1000000.0f) + " MB\n"
            "Compacted suballocator memory: "                    + std::to_string((_compactionPool->GetSuballocatorSize()         ) / 1000000.0f) + " MB\n"
            "Uncompacted suballocation alignment memory saved: " + std::to_string((_resultPool->GetAlignmentSavingSize()          ) / 1000000.0f) + " MB\n"
            "Compacted suballocation alignment memory saved: "   + std::to_string((_compactionPool->GetAlignmentSavingSize()      ) / 1000000.0f) + " MB\n"
            "Compacted unused freed suballocation memory: "      + std::to_string((_compactionPool->GetFreeSuballocationsSize()   ) / 1000000.0f) + " MB\n");

        // Release queue indicates acceleration structure is completely removed
        while (_asBufferReleaseQueue.empty() == false)
        {
            if (_asBufferReleaseQueue.front()->frameIndexRequest + (_commandListLatency - 1) < _commandListIndex)
            {
                ReleaseAccelerationStructures(&_asBufferReleaseQueue.front(), 1);
                _asBufferReleaseQueue.pop();
            }
            else
            {
                break;
            }
        }

        // Complete queue indicates cleanup for acceleration structures
        while (_asBufferCompleteQueue.empty() == false)
        {
            if (_asBufferCompleteQueue.front()->frameIndexRequest + (_commandListLatency - 1) < _commandListIndex)
            {
                PostBuildRelease(&_asBufferCompleteQueue.front(), 1);
                _asBufferCompleteQueue.pop();
            }
            else
            {
                break;
            }
        }

        // Compaction queue indicates compaction needs to be done
        // Reset the current compaction memory overhead
        _currentCompactionMemorySize = 0;
        while (_asBufferCompactionQueue.empty() == false)
        {
            // Only do compaction on the confirmed completion of the original
            // build execution.
            if ((_asBufferCompactionQueue.front()->frameIndexRequest + (_commandListLatency - 1) < _commandListIndex) &&
                (_asBufferCompactionQueue.front()->requestedCompaction == true))
            {
                const bool successfulCompaction = CopyCompaction(device,
                                                                 commandList,
                                                                 &_asBufferCompactionQueue.front(),
                                                                 1);
                if (successfulCompaction == false)
                {
                    break;
                }

                // Tag initial frame index request for deletion
                _asBufferCompactionQueue.front()->frameIndexRequest = _commandListIndex;
                _asBufferCompleteQueue.push(_asBufferCompactionQueue.front());
                _asBufferCompactionQueue.pop();
            }
            else
            {
                break;
            }
        }

        _commandListIndex++;
    }

    void Initialize(ID3D12Device5* const device,
                    uint32_t             commandListLatency,
                    uint32_t             suballocatorBlockSize,
                    uint32_t             maxTransientCompactionMemory)
    {
        _maxTransientCompactionMemory = maxTransientCompactionMemory;
        _commandListLatency           = commandListLatency;

        _scratchPool = new BufferSuballocator(device,
                                              suballocatorBlockSize,
                                              D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                              D3D12_HEAP_TYPE_DEFAULT);
                
        _resultPool  = new BufferSuballocator(device,
                                              suballocatorBlockSize,
                                              D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                                              D3D12_HEAP_TYPE_DEFAULT);
        
        _compactionPool = new BufferSuballocator(device,
                                                 suballocatorBlockSize,
                                                 D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                                                 D3D12_HEAP_TYPE_DEFAULT);

        _compactionSizeGpuPool = new BufferSuballocator(device,
                                                        CompactionSizeSuballocationBlockSize,
                                                        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                                        D3D12_HEAP_TYPE_DEFAULT);

        _compactionSizeCpuPool = new BufferSuballocator(device,
                                                        CompactionSizeSuballocationBlockSize,
                                                        D3D12_RESOURCE_STATE_COPY_DEST,
                                                        D3D12_HEAP_TYPE_READBACK);
    }

    ASBuffers* BuildAccelerationStructures(ID3D12Device5* const                                        device,
                                           ID3D12GraphicsCommandList4* const                           commandList,
                                           const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS* bottomLevelInputs,
                                           const uint32_t                                              buildCount)
    {
        // Allocate a batch of acceleration structure buffers that the application can use for building TLAS, etc.
        ASBuffers* buffers             = new ASBuffers[buildCount];
        bool       compactionRequested = false;

        for (uint32_t buildIndex = 0; buildIndex < buildCount; buildIndex++)
        {
            // Request build size information and suballocate the scratch and result buffers
            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
            device->GetRaytracingAccelerationStructurePrebuildInfo(&bottomLevelInputs[buildIndex],
                                                                   &prebuildInfo);

            buffers[buildIndex].scratchGpuMemory = _scratchPool->CreateSubAllocation(device,
                                                                                     prebuildInfo.ScratchDataSizeInBytes,
                                                                                     D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

            buffers[buildIndex].resultGpuMemory  = _resultPool->CreateSubAllocation(device,
                                                                                    prebuildInfo.ResultDataMaxSizeInBytes,
                                                                                    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

            _totalUncompactedMemory += prebuildInfo.ResultDataMaxSizeInBytes;

            // Keeps track of which frame index the build was requested
            buffers[buildIndex].frameIndexRequest = _commandListIndex;

            // Setup build desc
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottomLevelBuildDesc = {};
            bottomLevelBuildDesc.Inputs                                             = bottomLevelInputs[buildIndex];
            bottomLevelBuildDesc.ScratchAccelerationStructureData                   = buffers[buildIndex].scratchGpuMemory.GetGPUVA();
            bottomLevelBuildDesc.DestAccelerationStructureData                      = buffers[buildIndex].resultGpuMemory.GetGPUVA();

            // Only perform compaction of the build inputs that include compaction
            if (bottomLevelInputs[buildIndex].Flags & D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION)
            {
                // Tag as not yet compacted
                buffers[buildIndex].isCompacted         = false;
                buffers[buildIndex].requestedCompaction = true;

                // Suballocate the gpu memory that the builder will use to write the compaction size post build
                buffers[buildIndex].compactionSizeGpuMemory = _compactionSizeGpuPool->CreateSubAllocation(device,
                                                                                                          SizeOfCompactionDescriptor,
                                                                                                          SizeOfCompactionDescriptor);
                // Request to get compaction size post build
                D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_DESC postBuildInfo [] = {
                    buffers[buildIndex].compactionSizeGpuMemory.GetGPUVA(),
                      D3D12_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO_COMPACTED_SIZE };

                commandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc,
                                                                  sizeof(postBuildInfo) / sizeof(postBuildInfo[0]),
                                                                  postBuildInfo);

                // Suballocate the readback memory
                buffers[buildIndex].compactionSizeCpuMemory = _compactionSizeCpuPool->CreateSubAllocation(device,
                                                                                                          SizeOfCompactionDescriptor,
                                                                                                          SizeOfCompactionDescriptor);
                // Place compaction item on the compaction queue
                _asBufferCompactionQueue.push(&buffers[buildIndex]);

                compactionRequested = true;
            }
            else
            {
                // This build doesn't request compaction
                buffers[buildIndex].requestedCompaction = false;
                commandList->BuildRaytracingAccelerationStructure(&bottomLevelBuildDesc,
                                                                  0,
                                                                  nullptr);
                // Place build item on the completion queue
                _asBufferCompleteQueue.push(&buffers[buildIndex]);
            }
        }

        if (compactionRequested)
        {
            // Transition the gpu compaction size suballocator block to copy over to mappable cpu memory
            D3D12_RESOURCE_BARRIER rb = {};
            rb.Transition.pResource   = buffers[0].compactionSizeGpuMemory.parentResource;
            rb.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            rb.Transition.StateAfter  = D3D12_RESOURCE_STATE_COPY_SOURCE;
            rb.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

            commandList->ResourceBarrier(1, &rb);

            // Copy the entire resource to avoid individually copying over each compaction size in strides of 8 bytes
            commandList->CopyResource(buffers[0].compactionSizeCpuMemory.parentResource,
                                      buffers[0].compactionSizeGpuMemory.parentResource);

            // Transition the gpu written compaction size suballocator block back over to unordered for later use
            rb                        = {};
            rb.Transition.pResource   = buffers[0].compactionSizeGpuMemory.parentResource;
            rb.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            rb.Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            rb.Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;

            commandList->ResourceBarrier(1, &rb);
        }

        return buffers;
    }

    bool CopyCompaction(ID3D12Device5* const              device,
                        ID3D12GraphicsCommandList4* const commandList,
                        ASBuffers**                       buffers,
                        const uint32_t                    compactionCount)
    {
        for (uint32_t compactionIndex = 0; compactionIndex < compactionCount; compactionIndex++)
        {
            // Don't compact if not requested or already complete
            if (buffers[compactionIndex]->isCompacted         == false &&
                buffers[compactionIndex]->requestedCompaction == true)
            {
                unsigned char* data           = nullptr;
                uint64_t       compactionSize = 0;
                uint32_t       offset         = buffers[compactionIndex]->compactionSizeCpuMemory.parentResourceOffset;

                // Map the readback gpu memory to system memory to fetch compaction size
                D3D12_RANGE readbackBufferRange{offset, offset + SizeOfCompactionDescriptor};
                buffers[compactionIndex]->compactionSizeCpuMemory.parentResource->Map(0, &readbackBufferRange, (void**)&data);
                memcpy(&compactionSize, &data[offset], SizeOfCompactionDescriptor);
                // D3D12 Unmap warnings.  Debug later please.
                buffers[compactionIndex]->compactionSizeCpuMemory.parentResource->Unmap(0, &readbackBufferRange);

                // If zero compactions have been performed but the transient memory budget isn't big enough
                // still move forward with the compaction
                if ((_currentCompactionMemorySize != 0) &&
                    ((_currentCompactionMemorySize + compactionSize) > _maxTransientCompactionMemory))
                {
                    return false;
                }
                _currentCompactionMemorySize += compactionSize;

                // Suballocate the gpu memory needed for compaction copy
                buffers[compactionIndex]->compactionGpuMemory = _compactionPool->CreateSubAllocation(device,
                                                                                                     compactionSize,
                                                                                                     D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

                _totalCompactedMemory += compactionSize;

                // Copy the result buffer into the compacted buffer
                commandList->CopyRaytracingAccelerationStructure(buffers[compactionIndex]->compactionGpuMemory.GetGPUVA(),
                                                                 buffers[compactionIndex]->resultGpuMemory.GetGPUVA(),
                                                                 D3D12_RAYTRACING_ACCELERATION_STRUCTURE_COPY_MODE_COMPACT);

                // Tag as compaction complete
                buffers[compactionIndex]->isCompacted = true;

#if _DEBUG
                OutputDebugString((
                    "Uncompacted memory: " + std::to_string(buffers[compactionIndex]->resultGpuMemory.alignedBufferSizeInBytes) + "\n"
                    "Compacted memory: "   + std::to_string(compactionSize)                                                     + "\n").c_str());
#endif
            }
        }
        return true;
    }

    void PostBuildRelease(ASBuffers**    buffers,
                          const uint32_t buildCount)
    {
        for (uint32_t buildIndex = 0; buildIndex < buildCount; buildIndex++)
        {
            // Only delete compaction size and result buffers if compaction was done
            if (buffers[buildIndex]->isCompacted == true)
            {
                // Deallocate all the buffers used to create a compaction AS buffer
                _resultPool->FreeSubAllocation(buffers[buildIndex]->resultGpuMemory);
                _compactionSizeGpuPool->FreeSubAllocation(buffers[buildIndex]->compactionSizeGpuMemory);
                _compactionSizeCpuPool->FreeSubAllocation(buffers[buildIndex]->compactionSizeCpuMemory);
            }
            _scratchPool->FreeSubAllocation(buffers[buildIndex]->scratchGpuMemory);
        }
    }

    void RemoveAccelerationStructures(ASBuffers**    buffers,
                                      const uint32_t removeCount)
    {
        _asBufferReleaseQueue.push(*buffers);
    }

    void ReleaseAccelerationStructures(ASBuffers** buffers, const uint32_t removeCount)
    {
        for (uint32_t buildIndex = 0; buildIndex < removeCount; buildIndex++)
        {
            // Deallocate all the buffers used for acceleration structures
            if (buffers[buildIndex]->scratchGpuMemory.parentResource != nullptr)
            {
                _scratchPool->FreeSubAllocation(buffers[buildIndex]->scratchGpuMemory);
            }
            if (buffers[buildIndex]->resultGpuMemory.parentResource != nullptr)
            {
                _resultPool->FreeSubAllocation(buffers[buildIndex]->resultGpuMemory);
            }
            if (buffers[buildIndex]->compactionGpuMemory.parentResource != nullptr)
            {
                _compactionPool->FreeSubAllocation(buffers[buildIndex]->compactionGpuMemory);
            }

            // This prevents compaction from being performed if an acceleration structure
            // gets allocated and then deallocated between round trips
            buffers[buildIndex]->requestedCompaction = false;
        }
    }

    std::string GetLog()
    {
        return _buildLogger;
    }
}