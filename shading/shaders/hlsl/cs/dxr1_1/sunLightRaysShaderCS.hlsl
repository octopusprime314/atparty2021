#include "../../include/structs.hlsl"
#include "../../include/dxr1_1_defines.hlsl"

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
StructuredBuffer<UniformMaterial>           uniformMaterials                 : register(t10, space0);
StructuredBuffer<AlignedHemisphereSample3D> sampleSets                       : register(t11, space0);



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

#include "../../include/sunLightCommon.hlsl"
#include "../../include/pointLightCommon.hlsl"
#include "../../include/utils.hlsl"


[numthreads(8, 8, 1)]

void main(int3 threadId : SV_DispatchThreadID,
        int3 threadGroupThreadId : SV_GroupThreadID)
{
    bool enableShadows = (shadowMode == 1) || (shadowMode == 3) || (shadowMode == 4);

    float3 position  = positionSRV[threadId.xy].xyz;
    float3 normal    = normalSRV[threadId.xy].xyz;
    float3 albedo    = albedoSRV[threadId.xy].xyz;
    float  roughness = normalSRV[threadId.xy].w;
    float  metallic  = positionSRV[threadId.xy].w;

    float3 sunLighting =
        GetBRDFSunLight(albedo, normal, position, roughness, metallic, threadId.xy);

    sunLightUAV[threadId.xy] = float4(sunLighting.xyz, 1.0);


    // Random indirect light ray from the hit position

    RayDesc ray;
    ray.TMax      = MAX_RAY_LENGTH;
    ray.Origin    = position;
    ray.Direction = GetRandomRayDirection(threadId.xy, -normal.xyz, (uint2)screenSize, 0);
    ray.TMin      = MIN_RAY_LENGTH;

    RayQuery<RAY_FLAG_NONE> rayQuery;
    rayQuery.TraceRayInline(rtAS, RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, ~0, ray);

    while (rayQuery.Proceed())
    {
        RayTraversalData rayData;
        rayData.worldRayOrigin    = rayQuery.WorldRayOrigin();
        rayData.currentRayT       = rayQuery.CandidateTriangleRayT();
        rayData.closestRayT       = rayQuery.CommittedRayT();
        rayData.worldRayDirection = rayQuery.WorldRayDirection();
        rayData.geometryIndex     = rayQuery.CandidateGeometryIndex();
        rayData.primitiveIndex    = rayQuery.CandidatePrimitiveIndex();
        rayData.instanceIndex     = rayQuery.CandidateInstanceIndex();
        rayData.barycentrics      = rayQuery.CandidateTriangleBarycentrics();
        rayData.objectToWorld     = rayQuery.CandidateObjectToWorld4x3();

        bool isHit = ProcessTransparentTriangle(rayData);
        if (isHit)
        {
            rayQuery.CommitNonOpaqueTriangleHit();
        }
    }

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        float3 albedo;
        float  roughness;
        float  metallic;
        float3 normal;
        float3 hitPosition;
        float  transmittance;

        RayTraversalData rayData;
        rayData.worldRayOrigin    = rayQuery.WorldRayOrigin();
        rayData.closestRayT       = rayQuery.CommittedRayT();
        rayData.worldRayDirection = rayQuery.WorldRayDirection();
        rayData.geometryIndex     = rayQuery.CommittedGeometryIndex();
        rayData.primitiveIndex    = rayQuery.CommittedPrimitiveIndex();
        rayData.instanceIndex     = rayQuery.CommittedInstanceIndex();
        rayData.barycentrics      = rayQuery.CommittedTriangleBarycentrics();
        rayData.objectToWorld     = rayQuery.CommittedObjectToWorld4x3();

        ProcessOpaqueTriangle(rayData,
                              albedo,
                              roughness,
                              metallic,
                              normal,
                              hitPosition,
                              transmittance);

        float3 secondarySurfaceReflectionColor = GetBRDFPointLight(albedo,
                                                                   normal,
                                                                   hitPosition,
                                                                   roughness,
                                                                   metallic,
                                                                   threadId.xy,
                                                                   false);

        indirectLightRaysUAV[threadId.xy].xyz = secondarySurfaceReflectionColor;
        
        const float temporalFade = 0.01666;
        //const float temporalFade = 0.2;
        indirectLightRaysHistoryUAV[threadId.xy].xyz = (temporalFade * indirectLightRaysUAV[threadId.xy].xyz) +
                                                       ((1.0 - temporalFade) * indirectLightRaysHistoryUAV[threadId.xy].xyz);

        debug0UAV[threadId.xy].xyz = albedo;
        debug1UAV[threadId.xy].xyz = secondarySurfaceReflectionColor;
        debug2UAV[threadId.xy].xyz = ray.Direction;
    }

    if (frameIndex == 0)
    {
        indirectLightRaysHistoryUAV[threadId.xy].xyz = float3(0.0, 0.0, 0.0);
    }
}