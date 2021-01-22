#pragma once
#include "BufferSuballocator.h"
#include <queue>
#include <iostream>
#include <string>

// The design of this library is to allow developers to use compaction and suballocation of
// acceleration structure buffers to reduce the memory footprint.  Compaction is proven to reduce the total memory
// footprint by more than a half.  Suballocation is proven to reduce memory as well by tightly packing
// acceleration structure buffers that are less than 64 KB.

// The intended use of this library is to batch up all of the acceleration structure build inputs and pass them to
// BuildAccelerationStructures which in turn will perform all the suballocation memory requests and build details
// including compaction.  Post build info is abstracted away by the library in order to do compaction under the hood.
// Following BuildAccelerationStructures execution on the GPU the call to CopyCompaction will receive an array of the
// ASBuffers provided by BuildAccelerationStructures and they will be compacted if requested.  ASBuffers will be modified
// with a valid compaction buffer GPUVA.  Once CopyCompaction has been executed on the GPU the request to release all
// unused resources with be performed by calling PostBuildRelease and passing in the ASBuffers array.

namespace RTCompaction
{

    struct ASBuffers
    {
        Suballocation scratchGpuMemory;
        Suballocation resultGpuMemory;
        Suballocation compactionGpuMemory;
        Suballocation compactionSizeGpuMemory;
        Suballocation compactionSizeCpuMemory;
        bool          isCompacted;
        bool          requestedCompaction;
        uint64_t      frameIndexRequest;

        D3D12_GPU_VIRTUAL_ADDRESS GetASBuffer()
        {
            return isCompacted ? compactionGpuMemory.GetGPUVA() :
                                 resultGpuMemory.GetGPUVA();
        }
    };

    // Suballocation buffers
    extern BufferSuballocator* _scratchPool;
    extern BufferSuballocator* _resultPool;
    extern BufferSuballocator* _compactionPool;
    extern BufferSuballocator* _compactionSizeGpuPool;
    extern BufferSuballocator* _compactionSizeCpuPool;

    // Logger that gets cleared every call to NextFrame
    extern std::string _buildLogger;
    extern uint32_t    _uncompactedMemory;
    extern uint32_t    _compactedMemory;

    // Indicates to the library the command list latency for
    // finished execution of an acceleration structure build on the
    // original commmand buffer.  Typically an engine will have double
    // or triple buffered command list execution cadence and so
    // commandBufferLatency prevents compaction until the command buffer
    // responsible for the original build is reused for the compaction.
    extern uint32_t _commandListLatency;
    extern uint64_t _commandListIndex;

    // The maximum amount of resident memory that can be used for compaction copies
    // to reduce the overall memory occupancy of the result and compaction buffer
    // both being transiently in memory.
    extern uint32_t _maxTransientCompactionMemory;

    // Every suballocator block gets allocated with a configurable size
    extern uint32_t _suballocationBlockSize;

    // Limits the amount of transient compaction buffer memory
    extern uint32_t _currentCompactionMemorySize;

    // Queues used to manage compaction events
    extern std::queue<ASBuffers*> _asBufferCompactionQueue;
    extern std::queue<ASBuffers*> _asBufferCompleteQueue;
    extern std::queue<ASBuffers*> _asBufferReleaseQueue;

    // Initializes all of the suballocators used to tightly pack the acceleration structure buffers
    // as well as the command buffer latency used to indicate compaction can be done
    // Suballocator block size is also an optional field
    extern void       Initialize(ID3D12Device5* const device,
                                 uint32_t             commandListLatency,
                                 uint32_t             suballocatorBlockSize,
                                 uint32_t             maxTransientCompactionMemory);

    // Used to indicate when compaction and release steps can be performed under the hood
    extern void       NextFrame(ID3D12Device5* const device,
                                ID3D12GraphicsCommandList4* const commandList);

    // CopyCompaction takes in a list of ASBuffers and compacts the ones that requested it
    // Returns false if one of the compaction copies did not happen due to memory overhead limit
    extern bool       CopyCompaction(ID3D12Device5* const              device,
                                     ID3D12GraphicsCommandList4* const commandList,
                                     ASBuffers**                       buffers,
                                     const uint32_t                    compactionCount);

    // PostBuildRelease releases suballocations that are no longer required based on build type
    extern void       PostBuildRelease(ASBuffers**    buffers,
                                       const uint32_t buildCount);

    // RemoveAccelerationStructures takes in an array of ASBuffers and appends to release queue
    extern void       RemoveAccelerationStructures(ASBuffers**    buffers,
                                                   const uint32_t removeCount);

    // ReleaseAccelerationStructures takes in an array of ASBuffers and releases all resources
    extern void       ReleaseAccelerationStructures(ASBuffers** buffers,
                                                    const uint32_t removeCount);

    // BuildAccelerationStructures takes in an array of build inputs and compacts each one if requested
    // then returns an array of ASBuffers
    extern ASBuffers* BuildAccelerationStructures(ID3D12Device5* const                                        device,
                                                  ID3D12GraphicsCommandList4* const                           commandList,
                                                  const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS* bottomLevelInputs,
                                                  const uint32_t                                              buildCount);

    // Returns current command lists build and compaction stats
    extern std::string GetLog();

}