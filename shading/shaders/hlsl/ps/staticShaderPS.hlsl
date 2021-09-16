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
    float4x4 modelMatrix;
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
struct MRT
{
    float4 color          : SV_Target0;
    float4 normal         : SV_Target1;
    float4 position       : SV_Target2;
    float2 velocityBuffer : SV_Target3;
};

#include "../include/utils.hlsl"

MRT main(in float4 position    : SV_POSITION,
         in float3 normal      : NORMALOUT,
         in float2 uv          : UVOUT,
         in float4 prevPos     : PREVPOSOUT,
         in float4 currPos     : CURRPOSOUT,
         in uint   primitiveID : SV_PrimitiveID)
{

    float4 vTexColor = float4(0.0, 0.0, 0.0, 1.0);
    MRT    output;

    int    geometryIndex  = 0;
    int    primitiveIndex = primitiveID;
    int    instanceIndex  = instanceBufferIndex;

    RayTraversalData rayData;
    rayData.worldRayOrigin    = float3(0.0, 0.0, 0.0);
    rayData.closestRayT       = 0.0;
    rayData.worldRayDirection = float3(0.0, 0.0, 0.0);
    rayData.geometryIndex     = geometryIndex;
    rayData.primitiveIndex    = primitiveIndex;
    rayData.instanceIndex     = instanceIndex;
    rayData.barycentrics      = float2(0.0, 0.0);
    rayData.uvIsValid         = true;
    rayData.uv                = uv;
    rayData.normal            = normal;

    float3 albedo;
    float  roughness;
    float  metallic;
    float3 hitPosition;
    float  transmittance;
    float3 tbnNormal;

    ProcessOpaqueTriangle(rayData, albedo, roughness, metallic, tbnNormal, hitPosition, transmittance);

    output.color    = float4(albedo, transmittance);
    output.normal   = float4(-tbnNormal, roughness);
    output.position = float4(position.xyz, metallic);

    float2 currProjPos = currPos.xy;
    float2 prevProjPos = prevPos.xy;

    output.velocityBuffer = float2(currProjPos - prevProjPos);

    return output;
}