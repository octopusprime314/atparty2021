#include "../include/structs.hlsl"

struct BoneData
{
    float4x4 bone;
};

StructuredBuffer<CompressedAttribute> vertexBuffer[] : register(t0, space1);
Buffer<float>                      bones          : register(t1);
Buffer<float>                         joints          : register(t2);
Buffer<float>                         weights          : register(t3);
RWBuffer<float>                       deformedVertices : register(u0);

cbuffer objectData : register(b0)
{
    uint modelIndex;
}

[numthreads(64, 1, 1)] void main(uint3 threadId
                                 : SV_DispatchThreadID) {
    
    uint     boneIndex         = threadId.x * 4;
    

    
    float4x4 animationTransform = 0;
    for (int i = 0; i < 4; i++)
    {
        if (weights[boneIndex + i] > 0.0)
        {
            uint     matrixIndex       = int(joints[boneIndex + i]) * 16;
            float4x4 bone              = {
                float4(bones[matrixIndex],
                       bones[matrixIndex + 4],
                       bones[matrixIndex + 8],
                       bones[matrixIndex + 12]),
                float4(bones[matrixIndex + 1],
                       bones[matrixIndex + 5],
                       bones[matrixIndex + 9],
                       bones[matrixIndex + 13]),
                float4(bones[matrixIndex + 2],
                       bones[matrixIndex + 6],
                       bones[matrixIndex + 10],
                       bones[matrixIndex + 14]),
                float4(bones[matrixIndex + 3],
                       bones[matrixIndex + 7],
                       bones[matrixIndex + 11],
                       bones[matrixIndex + 15])};

            animationTransform += mul(weights[boneIndex + i], bone);
        }
    }
    
    float3 deformVert            = mul(float4(vertexBuffer[modelIndex][threadId.x].vertex.xyz, 1.0), animationTransform).xyz;

    uint index = threadId.x * 3;

    deformedVertices[index]     = deformVert.x;
    deformedVertices[index + 1] = deformVert.y;
    deformedVertices[index + 2] = deformVert.z;
}