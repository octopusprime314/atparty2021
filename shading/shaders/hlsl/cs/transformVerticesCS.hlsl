#include "../include/structs.hlsl"

StructuredBuffer<CompressedAttribute> vertexBuffer[] : register(t0, space1);
//Buffer<uint>                          indexBuffer[] : register(t1, space2);
Buffer<float>                         joints          : register(t1);
Buffer<float>                         weights          : register(t2);
RWBuffer<float>                       deformedVertices : register(u0);

cbuffer globalData : register(b0)
{
    uint     modelIndex;
    float4x4 bones[150]; // 150 bones is the maximum bone count
    
}

[numthreads(64, 1, 1)] void main(uint3 threadId
                                 : SV_DispatchThreadID) {
    
    uint boneIndex = threadId.x * 4;
    float4x4 animationTransform = 0;
    for (int i = 0; i < 4; i++)
    {
        if (weights[boneIndex + i] > 0.0)
        {
            animationTransform += mul(weights[boneIndex + i], bones[int(joints[boneIndex + i])]);
        }
    }

    //uint vertIndex = uint(indexBuffer[NonUniformResourceIndex(modelIndex)].Load(threadId.x));
    //float3 vert = vertexBuffer[NonUniformResourceIndex(modelIndex)].Load(vertIndex).vertex;
    
    float3 deformVert            = mul(float4(vertexBuffer[modelIndex][threadId.x].vertex.xyz/*vert.xyz*/, 1.0), animationTransform).xyz;

    uint index = threadId.x * 3;

    deformedVertices[index]     = deformVert.x;
    deformedVertices[index + 1] = deformVert.y;
    deformedVertices[index + 2] = deformVert.z;
}