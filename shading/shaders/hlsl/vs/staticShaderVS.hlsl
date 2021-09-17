#include "../include/structs.hlsl"

Buffer<float>                         instanceNormalMatrixTransforms   : register(t0, space0);
StructuredBuffer<CompressedAttribute> vertexBuffer[]                   : register(t1, space2);
Buffer<uint>                          instanceIndexToMaterialMapping   : register(t2, space0);
Buffer<uint>                          instanceIndexToAttributesMapping : register(t3, space0);
StructuredBuffer<UniformMaterial>     uniformMaterials                 : register(t4, space0);
Texture2D                             diffuseTexture[]                 : register(t5, space1);
Buffer<float>                         instanceModelMatrixTransforms    : register(t6, space0);
Buffer<uint>                          indexBuffer[]                    : register(t7, space3);

SamplerState bilinearWrap : register(s0);

cbuffer objectData : register(b0)
{
    uint     instanceBufferIndex;
    //float4x4 prevModelMatrix;
    //float4x4 modelMatrix;
}

cbuffer globalData : register(b1)
{
    float4x4 prevViewTransform;
    float4x4 projTransform;
    float4x4 viewTransform;
    float4x4 inverseView;
    float2   screenSize;
    uint     texturesPerMaterial;
}

#include "../include/utils.hlsl"

void main(    uint   id          : SV_VERTEXID,
          out float4 outPosition : SV_POSITION,
              uint    instanceID : SV_InstanceID,
          out float3 outNormal   : NORMALOUT,
          out float2 outUV       : UVOUT,
          out float4 outPrevPos  : PREVPOSOUT,
          out float4 outCurrPos  : CURRPOSOUT)
{
    int modelMatrixOffset = (instanceBufferIndex + instanceID) * 16;

    float4x4 model  = {float4(instanceModelMatrixTransforms[NonUniformResourceIndex(modelMatrixOffset)],
                              instanceModelMatrixTransforms[NonUniformResourceIndex(modelMatrixOffset + 4)],
                              instanceModelMatrixTransforms[NonUniformResourceIndex(modelMatrixOffset + 8)],
                              instanceModelMatrixTransforms[NonUniformResourceIndex(modelMatrixOffset + 12)]),
                       float4(instanceModelMatrixTransforms[NonUniformResourceIndex(modelMatrixOffset + 1)],
                              instanceModelMatrixTransforms[NonUniformResourceIndex(modelMatrixOffset + 5)],
                              instanceModelMatrixTransforms[NonUniformResourceIndex(modelMatrixOffset + 9)],
                              instanceModelMatrixTransforms[NonUniformResourceIndex(modelMatrixOffset + 13)]),
                       float4(instanceModelMatrixTransforms[NonUniformResourceIndex(modelMatrixOffset + 2)],
                              instanceModelMatrixTransforms[NonUniformResourceIndex(modelMatrixOffset + 6)],
                              instanceModelMatrixTransforms[NonUniformResourceIndex(modelMatrixOffset + 10)],
                              instanceModelMatrixTransforms[NonUniformResourceIndex(modelMatrixOffset + 14)]),
                       float4(instanceModelMatrixTransforms[NonUniformResourceIndex(modelMatrixOffset + 3)],
                              instanceModelMatrixTransforms[NonUniformResourceIndex(modelMatrixOffset + 7)],
                              instanceModelMatrixTransforms[NonUniformResourceIndex(modelMatrixOffset + 11)],
                              instanceModelMatrixTransforms[NonUniformResourceIndex(modelMatrixOffset + 15)])};

    int offset = (instanceBufferIndex + instanceID) * 9;

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

    int attributeIndex = instanceIndexToAttributesMapping[(instanceBufferIndex + instanceID)];

    float3 position = GetVertex(attributeIndex, id);
    float3 normal   = -GetNormal(attributeIndex, id);
    float2 uv       = GetUV(attributeIndex, id);

    float4x4 mv  = mul(model, viewTransform);
    float4x4 mvp = mul(mv, projTransform);
    outPosition  = mul(float4(position, 1.0f), mvp);
    outNormal    = mul(normal, normalMatrix).rgb;
    outUV        = uv;

    float4x4 prevMV  = mul(/*prevModelMatrix*/model, prevViewTransform);
    float4x4 prevMVP = mul(prevMV, projTransform);
    outPrevPos       = mul(float4(position, 1.0f), prevMV);
    outCurrPos       = mul(float4(position, 1.0f), mv);
}