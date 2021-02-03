#include <vector>
#include <d3d12.h>

struct FreeMemChunk
{
    uint32_t offset;
    uint32_t size;
};

struct SuballocatorBlock
{
    ID3D12Resource*           suballocatingBuffer;
    std::vector<FreeMemChunk> freeList;
    uint32_t                  currentOffset;
    uint32_t                  memoryBlockSize;
    uint32_t                  totalAllocations;
};

struct Suballocation
{
    ID3D12Resource*    parentResource;
    uint32_t           parentResourceOffset;
    uint32_t           alignedBufferSizeInBytes;

    D3D12_GPU_VIRTUAL_ADDRESS GetGPUVA()
    {
        return parentResource->GetGPUVirtualAddress() +
               static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(parentResourceOffset);
    }
};

class BufferSuballocator
{
public:

    BufferSuballocator(ID3D12Device*         device,
                       uint32_t              bufferSizeInBytes,
                       D3D12_RESOURCE_STATES resourceState,
                       D3D12_HEAP_TYPE       heapType);

    Suballocation                   CreateSubAllocation(ID3D12Device* device,
                                                        uint32_t      suballocationSizeInBytes,
                                                        uint32_t      memoryAlignmentInBytes);

    void                            FreeSubAllocation(Suballocation& suballocation);
    std::vector<SuballocatorBlock>& GetSuballocators();
    uint32_t                        GetSuballocatorSize();
    uint32_t                        GetFreeSuballocationsSize();
    uint32_t                        GetAlignmentSavingSize();

private:

    SuballocatorBlock              CreateSuballocatorBlock(ID3D12Device* device,
                                                           uint32_t      bufferSizeInBytes = 0);

    uint32_t                       m_suballocationAlignmentMemorySavings;
    uint32_t                       m_memoryBlockSize;
    D3D12_RESOURCE_STATES          m_resourceState;
    D3D12_HEAP_TYPE                m_heapType;
    std::vector<SuballocatorBlock> m_blocks;
};
