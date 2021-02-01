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
struct MRT
{
    float4 color    : SV_Target0;
    float4 normal   : SV_Target1;
    float4 position : SV_Target2;
};

void ProcessRasterOpaqueTriangle(in  RayTraversalData rayData,
                                 out float3           albedo,
                                 out float            roughness,
                                 out float            metallic,
                                 out float3           normal,
                                 out float3           hitPosition,
                                 out float            transmittance)
{
    hitPosition = rayData.worldRayOrigin + (rayData.closestRayT * rayData.worldRayDirection);

    int    geometryIndex   = rayData.geometryIndex;
    int    primitiveIndex  = rayData.primitiveIndex;
    int    instanceIndex   = rayData.instanceIndex;
    float2 barycentrics    = rayData.barycentrics;

    int materialIndex = instanceIndexToMaterialMapping[instanceIndex] + (geometryIndex * texturesPerMaterial);
    int attributeIndex = instanceIndexToAttributesMapping[instanceIndex] + geometryIndex;

    float2 uvCoord = rayData.uv;

    // FUCK THIS MATRIX DECOMPOSITION BULLSHIT!!!!
    float4x3 cachedTransform        = rayData.objectToWorld;
    float4x4 objectToWorldTransform = {float4(cachedTransform[0].xyz, 0.0),
                                        float4(cachedTransform[1].xyz, 0.0),
                                        float4(cachedTransform[2].xyz, 0.0),
                                        float4(cachedTransform[3].xyz, 1.0)};

    int offset = instanceIndex * 9;
  
    float3x3 instanceNormalMatrixTransform = {
        float3(instanceNormalMatrixTransforms[NonUniformResourceIndex(offset)],
                instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 3)],
                instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 6)]),
        float3(instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 1)],
                instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 4)],
                instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 7)]),
        float3(instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 2)],
                instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 5)],
                instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 8)])};

    float mipLevel = 0;

    albedo = float3(0.0, 0.0, 0.0);
    if (uniformMaterials[attributeIndex].validBits & ColorValidBit)
    {
        albedo = uniformMaterials[attributeIndex].baseColor;
    }
    else
    {
        albedo = pow(diffuseTexture[NonUniformResourceIndex(materialIndex)].SampleLevel(bilinearWrap, uvCoord, mipLevel).xyz, 2.2);
    }

    roughness = 0.0;
    if (uniformMaterials[attributeIndex].validBits & RoughnessValidBit)
    {
        roughness = uniformMaterials[attributeIndex].roughness;
    }
    else
    {
        roughness = diffuseTexture[NonUniformResourceIndex(materialIndex + 2)]
                        .SampleLevel(bilinearWrap, uvCoord, mipLevel)
                        .y;
    }

    metallic = 0.0;
    if (uniformMaterials[attributeIndex].validBits & MetallicValidBit)
    {
        metallic = uniformMaterials[attributeIndex].metallic;
    }

    normal = float3(0.0, 0.0, 0.0);
    //if (uniformMaterials[attributeIndex].validBits & NormalValidBit)
    //{
        normal = -normal;
    //}
    //else
    //{
    //    float3 normalMap = diffuseTexture[NonUniformResourceIndex(materialIndex + 1)]
    //                            .SampleLevel(bilinearWrap, uvCoord, mipLevel)
    //                            .xyz;
    //
    //    // Converts from [0,1] space to [-1,1] space
    //    normalMap = normalMap * 2.0f - 1.0f;
    //
    //    // Compute the normal from loading the triangle vertices
    //    float3x3 tbnMat = GetTBN(barycentrics, attributeIndex, primitiveIndex);
    //
    //    // If there is a failure in getting the TBN matrix then use the computed normal without normal mappings
    //    if (any(isnan(tbnMat[0])))
    //    {
    //        normal = -normalize(mul(tbnMat[2], instanceNormalMatrixTransform));
    //    }
    //    else
    //    {
    //        float3x3 tbnMatNormalTransform = mul(tbnMat, instanceNormalMatrixTransform);
    //
    //        normal = -normalize(mul(normalMap, tbnMatNormalTransform));
    //    }
    //}

    transmittance = uniformMaterials[attributeIndex].transmittance;
}

MRT main(in float4 position    : SV_POSITION,
         in float3 normal      : NORMALOUT,
         in float2 uv          : UVOUT,
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
    
    float3 albedo;
    float  roughness;
    float  metallic;
    float3 hitPosition;
    float  transmittance;
    float3 tbnNormal;
    
    ProcessRasterOpaqueTriangle(rayData, albedo, roughness, metallic, tbnNormal, hitPosition, transmittance);

    output.color    = float4(albedo, transmittance);
    output.normal   = float4(normal, roughness);
    output.position = float4(position.xyz, metallic);

    return output;
}