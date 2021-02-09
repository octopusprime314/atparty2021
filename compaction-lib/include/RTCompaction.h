#pragma once
#include "BufferSuballocator.h"

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

    // Initializes all of the suballocators used to tightly pack the acceleration structure buffers
    // as well as the command buffer latency used to indicate compaction can be done
    // Suballocator block size is also an optional field
    void       Initialize(ID3D12Device5* const device,
                          uint32_t             commandListLatency,
                          uint32_t             suballocatorBlockSize,
                          uint32_t             maxTransientCompactionMemory);

    // Used to indicate when compaction and release steps can be performed under the hood
    void       NextFrame(ID3D12Device5* const device,
                         ID3D12GraphicsCommandList4* const commandList);

    // RemoveAccelerationStructures takes in an array of ASBuffers and appends to release queue
    void       RemoveAccelerationStructures(ASBuffers**    buffers,
                                            const uint32_t removeCount);

    // BuildAccelerationStructures takes in an array of build inputs and compacts each one if requested
    // then returns an array of ASBuffers
    ASBuffers* BuildAccelerationStructures(ID3D12Device5* const                                        device,
                                           ID3D12GraphicsCommandList4* const                           commandList,
                                           const D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS* bottomLevelInputs,
                                           const uint32_t                                              buildCount);

    // Returns current command lists build and compaction stats
    const char* GetLog();
}