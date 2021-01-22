#include "BufferSuballocator.h"

BufferSuballocator::BufferSuballocator(ID3D12Device*         device,
                                       uint32_t              bufferSizeInBytes,
                                       D3D12_RESOURCE_STATES resourceState,
                                       D3D12_HEAP_TYPE       heapType)
{
    m_suballocationAlignmentMemorySavings = 0;
    m_memoryBlockSize                     = bufferSizeInBytes;
    m_resourceState                       = resourceState;
    m_heapType                            = heapType;
}

SuballocatorBlock BufferSuballocator::CreateSuballocatorBlock(ID3D12Device* device,
                                                              uint32_t      bufferSizeInBytes)
{
    UINT64 blockAllocationInBytes = bufferSizeInBytes;
    if (blockAllocationInBytes == 0)
    {
        blockAllocationInBytes = m_memoryBlockSize;
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension           = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Alignment           = 0;
    desc.Width               = blockAllocationInBytes;
    desc.Height              = 1;
    desc.DepthOrArraySize    = 1;
    desc.MipLevels           = 1;
    desc.Format              = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count    = 1;
    desc.SampleDesc.Quality  = 0;
    desc.Layout              = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    if (m_heapType != D3D12_HEAP_TYPE_READBACK)
    {
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    else
    {
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;
    }

    D3D12_HEAP_PROPERTIES heapProperties;
    heapProperties.Type                 = m_heapType;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CPUPageProperty      = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.CreationNodeMask     = 1;
    heapProperties.VisibleNodeMask      = 1;

    ID3D12Resource* buffer = nullptr;
    device->CreateCommittedResource(&heapProperties,
                                    D3D12_HEAP_FLAG_NONE,
                                    &desc,
                                    m_resourceState,
                                    nullptr,
                                    IID_PPV_ARGS(&buffer));

    SuballocatorBlock suballocatorBlock   = {};
    suballocatorBlock.suballocatingBuffer = buffer;
    suballocatorBlock.currentOffset       = 0;
    suballocatorBlock.memoryBlockSize     = blockAllocationInBytes;
    suballocatorBlock.totalAllocations    = 0;

    return suballocatorBlock;
}

Suballocation BufferSuballocator::CreateSubAllocation(ID3D12Device* device,
                                                      uint32_t      suballocationSizeInBytes,
                                                      uint32_t      memoryAlignmentInBytes)
{
    const uint32_t sizeInBytes = ((suballocationSizeInBytes + (memoryAlignmentInBytes - 1)) &
                                 ~(memoryAlignmentInBytes - 1));
    if (m_blocks.size() == 0)
    {
        UINT64 blockAllocationInBytes = m_memoryBlockSize;
        if (sizeInBytes > blockAllocationInBytes)
        {
            blockAllocationInBytes = sizeInBytes;
        }
        m_blocks.push_back(CreateSuballocatorBlock(device, blockAllocationInBytes));
    }

    Suballocation  subAllocationBuffer = {};
    uint32_t       numSuballocators    = m_blocks.size();

    for (uint32_t subAllocatorIndex = 0; subAllocatorIndex < numSuballocators; subAllocatorIndex++)
    {
        bool                                nextSuballocatorResource     = false;
        SuballocatorBlock&                  suballocatorBlock            = m_blocks[subAllocatorIndex];
        bool                                foundFreeSuballocation       = false;
        std::vector<FreeMemChunk>::iterator minUnusedMemoryIter          = suballocatorBlock.freeList.end();
        std::vector<FreeMemChunk>::iterator freeSuballocationIter        = suballocatorBlock.freeList.begin();
        uint32_t                            minUnusedMemorySuballocation = UINT_MAX;

        while (freeSuballocationIter != suballocatorBlock.freeList.end())
        {
            if (sizeInBytes <= freeSuballocationIter->size)
            {
                // Attempt to find the exact fit and if not fallback to the least wasted unused
                // memory
                if (freeSuballocationIter->size - sizeInBytes == 0)
                {
                    // Keep previous allocation size
                    subAllocationBuffer.alignedBufferSizeInBytes = freeSuballocationIter->size;
                    subAllocationBuffer.parentResourceOffset     = freeSuballocationIter->offset;
                    foundFreeSuballocation                       = true;

                    // Remove from the list
                    suballocatorBlock.freeList.erase(freeSuballocationIter);
                    suballocatorBlock.totalAllocations++;
                    break;
                }
                else
                {
                    const uint32_t unusedMemory = freeSuballocationIter->size - sizeInBytes;
                    if (unusedMemory < minUnusedMemorySuballocation)
                    {
                        minUnusedMemoryIter          = freeSuballocationIter;
                        minUnusedMemorySuballocation = unusedMemory;
                    }
                }
            }
            freeSuballocationIter++;
        }

        // No perfect memory matches
        if (!foundFreeSuballocation)
        {
            // Didn't find a perfect equivalent memory candidate but take the best fit
            if (minUnusedMemoryIter != suballocatorBlock.freeList.end())
            {
                // Keep previous allocation size
                subAllocationBuffer.alignedBufferSizeInBytes = minUnusedMemoryIter->size;
                subAllocationBuffer.parentResourceOffset     = minUnusedMemoryIter->offset;
                foundFreeSuballocation                       = true;

                // Remove from the list
                suballocatorBlock.freeList.erase(minUnusedMemoryIter);
                suballocatorBlock.totalAllocations++;
            }
            else
            {
                const uint32_t offsetInBytes = suballocatorBlock.currentOffset + sizeInBytes;

                if (offsetInBytes <= suballocatorBlock.memoryBlockSize)
                {
                    // Only ever change the memory size if this is a new allocation
                    subAllocationBuffer.alignedBufferSizeInBytes = sizeInBytes;
                    subAllocationBuffer.parentResourceOffset     = suballocatorBlock.currentOffset;
                    suballocatorBlock.currentOffset              = offsetInBytes;
                    suballocatorBlock.totalAllocations++;
                }
                else
                {
                    // If none of the suballocators have a fit then create a new one
                    if (subAllocatorIndex == numSuballocators - 1)
                    {
                        // If suballocation block size is too small then do custom allocation of
                        // individual blocks that match the resources size
                        if ((sizeInBytes > m_memoryBlockSize))
                        {
                            m_blocks.push_back(CreateSuballocatorBlock(device, sizeInBytes));
                        }
                        else
                        {
                            m_blocks.push_back(CreateSuballocatorBlock(device));
                        }
                        numSuballocators++;
                    }
                    nextSuballocatorResource = true;
                }
            }
        }

        if (!nextSuballocatorResource)
        {
            const uint32_t memorySavedBySuballocating = ((subAllocationBuffer.alignedBufferSizeInBytes + (D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT - 1)) &
                                                         ~(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT - 1));
            m_suballocationAlignmentMemorySavings +=
                (memorySavedBySuballocating - subAllocationBuffer.alignedBufferSizeInBytes);

            subAllocationBuffer.parentResource = suballocatorBlock.suballocatingBuffer;
            break;
        }
    }

    return subAllocationBuffer;
}

void BufferSuballocator::FreeSubAllocation(Suballocation& suballocation)
{
    uint32_t blockIndex = 0;
    for (SuballocatorBlock& suballocatorBlock : m_blocks)
    {
        if (suballocatorBlock.suballocatingBuffer == suballocation.parentResource)
        {
            // Release the big chunks that are a single resource
            if (suballocation.alignedBufferSizeInBytes == suballocatorBlock.memoryBlockSize)
            {
                suballocatorBlock.suballocatingBuffer->Release();
                m_blocks.erase(m_blocks.begin() + blockIndex);
                suballocation.parentResource = nullptr;
            }
            else
            {
                struct FreeMemChunk freeSuballocation = {suballocation.parentResourceOffset,
                                                         suballocation.alignedBufferSizeInBytes};
                suballocatorBlock.freeList.push_back(freeSuballocation);

                suballocatorBlock.totalAllocations--;

                // If this suballocation was the final remaining allocation then release the suballocator block
                // but only if there is more than one block
                if (suballocatorBlock.totalAllocations == 0 &&
                    m_blocks.size() > 1)
                {
                    suballocatorBlock.suballocatingBuffer->Release();
                    m_blocks.erase(m_blocks.begin() + blockIndex);
                }

                suballocation.parentResource = nullptr;

            }
            break;
        }

        blockIndex++;
    }
}

std::vector<SuballocatorBlock>& BufferSuballocator::GetSuballocators()
{
    return m_blocks;
}

uint32_t BufferSuballocator::GetSuballocatorSize()
{
    uint32_t residentMemoryInBytes = 0;
    for (SuballocatorBlock& suballocatorBlock : m_blocks)
    {
        residentMemoryInBytes += suballocatorBlock.memoryBlockSize;
    }
    return residentMemoryInBytes;
}

uint32_t BufferSuballocator::GetFreeSuballocationsSize()
{
    uint32_t releasedSuballocationMemoryNotInUse = 0;
    for (SuballocatorBlock& suballocatorBlock : m_blocks)
    {
        for (auto& freeSuballocation : suballocatorBlock.freeList)
        {
            releasedSuballocationMemoryNotInUse += freeSuballocation.size;
        }
    }
    return releasedSuballocationMemoryNotInUse;
}

uint32_t BufferSuballocator::GetAlignmentSavingSize()
{
    return m_suballocationAlignmentMemorySavings;
}