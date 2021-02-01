#include "../include/structs.hlsl"

Buffer<float>                         instanceNormalMatrixTransforms   : register(t0, space0);
StructuredBuffer<CompressedAttribute> vertexBuffer[]                   : register(t1, space2);
Buffer<uint>                          instanceIndexToMaterialMapping   : register(t2, space0);
Buffer<uint>                          instanceIndexToAttributesMapping : register(t3, space0);
StructuredBuffer<UniformMaterial>     uniformMaterials                 : register(t4, space0);
Texture2D                             diffuseTexture[]                 : register(t5, space1);

SamplerState bilinearWrap : register(s0);

cbuffer objectData : register(b0)
{
    uint     instanceBufferIndex;
    float4x4 modelMatrix;
}

cbuffer globalData : register(b1)
{
    float4x4 projTransform;
    float4x4 viewTransform;
    float4x4 inverseView;
    float2   screenSize;
    uint     texturesPerMaterial;
}


#include "../include/math.hlsl"

float3 GetVertex(uint instanceIndex, uint vertexId)
{
    float3 vertex = vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(vertexId).vertex;
    return vertex;
}

float3 GetNormal(uint instanceIndex, uint vertexId)
{
    float3 normal = float3(
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(vertexId).normal[0]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(vertexId).normal[1]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(vertexId).normal[2]));

    return normal;
}

float2 GetUV(uint instanceIndex, uint vertexId)
{
    float2 uv = float2(
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(vertexId).uv[0]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(vertexId).uv[1]));
    return uv;
}

void main(    uint   id          : SV_VERTEXID,
          out float4 outPosition : SV_POSITION,
          out float3 outNormal   : NORMALOUT,
          out float2 outUV       : UVOUT)
{

    float4x4 model        = modelMatrix;
    int      offset       = instanceBufferIndex * 9;

    float3x3 normalMatrix = {
        float3(instanceNormalMatrixTransforms[NonUniformResourceIndex(offset)],
               instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 3)],
               instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 6)]),
        float3(instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 1)],
               instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 4)],
               instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 7)]),
        float3(instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 2)],
               instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 5)],
               instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 8)])};

    int attributeIndex = instanceIndexToAttributesMapping[instanceBufferIndex];

    float3 position = GetVertex(attributeIndex, id);
    float3 normal   = -GetNormal(attributeIndex, id);
    float2 uv       = GetUV(attributeIndex, id);

    float4x4 mv  = mul(model, viewTransform);
    float4x4 mvp = mul(mv, projTransform);
    outPosition  = mul(float4(position, 1.0f), mvp);
    outNormal    = mul(normal, normalMatrix).rgb;
    outUV        = uv;
}