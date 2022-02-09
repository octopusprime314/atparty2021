#include "../include/structs.hlsl"

StructuredBuffer<CompressedAttribute> inputVertex : register(t0);
Buffer<float>                         joints          : register(t1);
Buffer<float>                         weights          : register(t2);
RWBuffer<float>                       deformedVertices : register(u0);

cbuffer globalData : register(b0)
{
    float4x4 bones[150]; // 150 bones is the maximum bone count
}

[numthreads(64, 1, 1)]
void main(uint3 threadId : SV_DispatchThreadID)
{

    uint boneIndex = threadId.x * 4;

    float4x4 animationTransform = mul(weights[boneIndex    ], bones[int(joints[boneIndex    ])]) +
                                  mul(weights[boneIndex + 1], bones[int(joints[boneIndex + 1])]) +
                                  mul(weights[boneIndex + 2], bones[int(joints[boneIndex + 2])]) +
                                  mul(weights[boneIndex + 3], bones[int(joints[boneIndex + 3])]);
    
    float3 deformVert            = mul(float4(inputVertex[threadId.x].vertex.xyz, 1.0), animationTransform).xyz;

    uint index = threadId.x * 3;

    deformedVertices[index]     = deformVert.x;
    deformedVertices[index + 1] = deformVert.y;
    deformedVertices[index + 2] = deformVert.z;
}