#include "../include/structs.hlsl"

RaytracingAccelerationStructure             rtAS                             : register(t0, space0);
Texture2D                                   diffuseTexture[]                 : register(t1, space1);
StructuredBuffer<CompressedAttribute>       vertexBuffer[]                   : register(t2, space2);
Buffer<uint>                                indexBuffer[]                    : register(t3, space3);
Texture2D                                   albedoSRV                        : register(t4, space0);
Texture2D                                   normalSRV                        : register(t5, space0);
Texture2D                                   positionSRV                      : register(t6, space0);
Buffer<uint>                                instanceIndexToMaterialMapping   : register(t7, space0);
Buffer<uint>                                instanceIndexToAttributesMapping : register(t8, space0);
Buffer<float>                               instanceNormalMatrixTransforms   : register(t9, space0);
StructuredBuffer<AlignedHemisphereSample3D> sampleSets                       : register(t10, space0);



// Store lighting values in xyz channels and occlusion value in w channel
RWTexture2D<float4> sunLightUAV                   : register(u0);
RWTexture2D<float4> occlusionUAV                  : register(u1);
RWTexture2D<float2> occlusionHistoryUAV           : register(u2);
RWTexture2D<float4> indirectLightRaysUAV          : register(u3);
RWTexture2D<float4> indirectLightRaysHistoryUAV   : register(u4);
RWTexture2D<float4> debug0UAV                     : register(u5);
RWTexture2D<float4> debug1UAV                     : register(u6);
RWTexture2D<float4> debug2UAV                     : register(u7);
RWTexture2D<float4> pointLightOcclusionUAV        : register(u8);
RWTexture2D<float4> pointLightOcclusionHistoryUAV : register(u9);

SamplerState bilinearWrap : register(s0);

cbuffer globalData : register(b0)
{
    float4x4 inverseView;
    float4   pointLightColors[MAX_LIGHTS];
    float4   pointLightRanges[MAX_LIGHTS / 4];
    float4   pointLightPositions[MAX_LIGHTS];

    float4 sunLightColor;
    float4 sunLightPosition;
    float2 screenSize;
    float  sunLightRadius;
    float  sunLightRange;
    int    numPointLights;
    int    reflectionMode;
    int    shadowMode;
    float  time;
    uint   samplerIndex;
    uint   seed;
    uint   numSamplesPerSet;
    uint   numSampleSets;
    uint   numPixelsPerDimPerSet;
    uint   frameIndex;
    uint   texturesPerMaterial;
}

#include "../include/sunLightCommon.hlsl"
#include "../include/pointLightCommon.hlsl"
#include "../include/utils.hlsl"


[numthreads(8, 8, 1)]

void main(int3 threadId : SV_DispatchThreadID,
        int3 threadGroupThreadId : SV_GroupThreadID)
{
    bool enableShadows = (shadowMode == 1) || (shadowMode == 3) || (shadowMode == 4);

    float3 position  = positionSRV[threadId.xy].xyz;
    float3 normal    = normalSRV[threadId.xy].xyz;
    float3 albedo    = albedoSRV[threadId.xy].xyz;
    float  roughness = normalSRV[threadId.xy].w;
    float  metallic  = 0.0; // Find a place to store the metalness

    float3 sunLighting =
        GetBRDFSunLight(albedo, normal, position, roughness, metallic, threadId.xy);

    sunLightUAV[threadId.xy] = float4(sunLighting.xyz, 1.0);


    // Random indirect light ray from the hit position

    RayDesc ray;
    ray.TMax      = 100000.0;
    ray.Origin    = position;
    ray.Direction = GetRandomRayDirection(threadId.xy, -normal.xyz, (uint2)screenSize, 0);
    ray.TMin      = 0.1;

    // Cull non opaque here occludes the light sources holders from casting shadows
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;

    rayQuery.TraceRayInline(rtAS, RAY_FLAG_NONE, ~0, ray);

    rayQuery.Proceed();

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        float3 secondarySurfaceHitPosition = rayQuery.WorldRayOrigin() +
                                             (rayQuery.CommittedRayT() * rayQuery.WorldRayDirection());
        
        int geometryIndex  = rayQuery.CommittedGeometryIndex();
        int primitiveIndex = rayQuery.CommittedPrimitiveIndex();
        int instanceIndex  = rayQuery.CommittedInstanceIndex();
        
        int materialIndex  = instanceIndexToMaterialMapping[instanceIndex] + (geometryIndex * texturesPerMaterial);
        int attributeIndex = instanceIndexToAttributesMapping[instanceIndex] + geometryIndex;
        
        float2 uvCoord =
            GetTexCoord(rayQuery.CommittedTriangleBarycentrics(), attributeIndex, primitiveIndex);
        
        float3 rayP0 = rayQuery.WorldRayOrigin() + (rayQuery.WorldRayDirection() * ray.TMin);
        float3 rayP1 = rayQuery.WorldRayOrigin() + (rayQuery.WorldRayDirection() * ray.TMax);
        
        int      offset                        = instanceIndex * 9;
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
        
        // FUCK THIS MATRIX DECOMPOSITION BULLSHIT!!!!
        float4x3 cachedTransform        = rayQuery.CommittedObjectToWorld4x3();
        float4x4 objectToWorldTransform = {
            float4(cachedTransform[0].xyz, 0.0), float4(cachedTransform[1].xyz, 0.0),
            float4(cachedTransform[2].xyz, 0.0), float4(cachedTransform[3].xyz, 1.0)};
        
        float mipLevel = 0;//ComputeMipLevel(rayQuery.CommittedTriangleBarycentrics(), attributeIndex,
                           //              primitiveIndex, rayP0, rayP1, secondarySurfaceHitPosition, threadId.xy,
                           //              objectToWorldTransform, instanceNormalMatrixTransform);
        
        float3 secondarySurfaceAlbedo = pow(diffuseTexture[NonUniformResourceIndex(materialIndex)]
                                           .SampleLevel(bilinearWrap, uvCoord, mipLevel)
                                           .xyz, 2.2);
        
        float secondarySurfaceRoughness = diffuseTexture[NonUniformResourceIndex(materialIndex + 2)]
                                                         .SampleLevel(bilinearWrap, uvCoord, mipLevel)
                                                         .y;
        
        float3 secondarySurfaceNormalMap = diffuseTexture[NonUniformResourceIndex(materialIndex + 1)]
                                                         .SampleLevel(bilinearWrap, uvCoord, mipLevel)
                                                         .xyz;
        
        // Converts from [0,1] space to [-1,1] space
        float3 secondarySurfaceNormal = secondarySurfaceNormalMap * 2.0f - 1.0f;
        
        // Compute the normal from loading the triangle vertices
        float3x3 tbnMat =
            GetTBN(rayQuery.CommittedTriangleBarycentrics(), attributeIndex, primitiveIndex);
        
        // If there is a failure in getting the TBN matrix then use the computed normal without
        // normal mappings
        if (any(isnan(tbnMat[0])))
        {
            secondarySurfaceNormal = normalize(mul(tbnMat[2], instanceNormalMatrixTransform));
        }
        else
        {
            float3x3 tbnMatNormalTransform = mul(tbnMat, instanceNormalMatrixTransform);
        
            secondarySurfaceNormal = normalize(mul(secondarySurfaceNormal, tbnMatNormalTransform));
        }
        
        float3 secondarySurfaceReflectionColor = GetBRDFPointLight(secondarySurfaceAlbedo,
                                                                   secondarySurfaceNormal,
                                                                   secondarySurfaceHitPosition,
                                                                   secondarySurfaceRoughness,
                                                                   0.0,
                                                                   threadId.xy,
                                                                   false,
                                                                   rayQuery.CommittedRayT());

        indirectLightRaysUAV[threadId.xy].xyz = secondarySurfaceReflectionColor;
        
        const float temporalFade = 0.01666;
        //const float temporalFade = 0.2;
        indirectLightRaysHistoryUAV[threadId.xy].xyz = (temporalFade * indirectLightRaysUAV[threadId.xy].xyz) +
                                                       ((1.0 - temporalFade) * indirectLightRaysHistoryUAV[threadId.xy].xyz);

        debug0UAV[threadId.xy].xyz = secondarySurfaceAlbedo;
        debug1UAV[threadId.xy].xyz = secondarySurfaceReflectionColor;
        debug2UAV[threadId.xy].xyz = ray.Direction;
    }

    if (frameIndex == 0)
    {
        indirectLightRaysHistoryUAV[threadId.xy].xyz = float3(0.0, 0.0, 0.0);
    }
}