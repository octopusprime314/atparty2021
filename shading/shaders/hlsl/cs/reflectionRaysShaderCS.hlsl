#include "../include/structs.hlsl"
#include "../include/dxr1_1_defines.hlsl"

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

RWTexture2D<float4> reflectionUAV : register(u0);
RWTexture2D<float4> pointLightOcclusionUAV : register(u1);
RWTexture2D<float4> pointLightOcclusionHistoryUAV : register(u2);
RWTexture2D<float4> debugUAV : register(u3);

SamplerState bilinearWrap : register(s0);

cbuffer globalData : register(b0)
{
    float4x4 inverseView;

    float4 pointLightColors[MAX_LIGHTS];
    float4 pointLightRanges[MAX_LIGHTS / 4];
    float4 pointLightPositions[MAX_LIGHTS];
    
    float2   screenSize;
    int      numPointLights;

    uint seed;
    uint numSamplesPerSet;
    uint numSampleSets;
    uint numPixelsPerDimPerSet;
    uint texturesPerMaterial;
}

#include "../include/pointLightCommon.hlsl"
#include "../include/utils.hlsl"

static float reflectionIndex = 0.5;
static float refractionIndex = 1.0 - reflectionIndex;

[numthreads(8, 8, 1)]

void main(int3 threadId            : SV_DispatchThreadID,
          int3 threadGroupThreadId : SV_GroupThreadID)
{
    float3 normal = normalSRV[threadId.xy].xyz;

    if (normal.x == 0.0 &&
        normal.y == 0.0 &&
        normal.z == 0.0)
    {
        reflectionUAV[threadId.xy] = float4(0.0, 0.0, 0.0, 0.0);
    }
    else
    {
        float3 hitPosition   = positionSRV[threadId.xy].xyz;
        float3 albedo        = albedoSRV[threadId.xy].xyz;

        float  transmittance = albedoSRV[threadId.xy].w;
        float  metallic      = positionSRV[threadId.xy].w;
        float  roughness     = normalSRV[threadId.xy].w;

        // Reconstruct primary ray by taking the camera position and subtracting it from hit
        // position
        float3 cameraPosition = float3(inverseView[3][0], inverseView[3][1], inverseView[3][2]);
        float3 rayDirection   = normalize(hitPosition - cameraPosition);

        float3 reflectionColor = GetBRDFPointLight(albedo,
                                                   normal,
                                                   hitPosition,
                                                   roughness,
                                                   metallic,
                                                   threadId.xy,
                                                   false);

        uint bounceIndex = 0;

        while (roughness < 0.25 && bounceIndex < RECURSION_LIMIT)
        {
            // Trace the ray.
            // Set the ray's extents.
            RayDesc ray;
            ray.TMin   = MIN_RAY_LENGTH;
            ray.TMax   = MAX_RAY_LENGTH;
            ray.Origin = hitPosition;

            // Punch through ray with zero reflection
            if (transmittance > 0.0)
            {
                ray.Direction = rayDirection;
            }
            // Opaque materials make a reflected ray
            else
            {
                ray.Direction = rayDirection - (2.0f * dot(rayDirection, normal) * normal);
            }

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
                float3 prevPosition = hitPosition;

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

                debugUAV[threadId.xy].xyz = hitPosition;

                // Reflection ray direction
                if (transmittance == 0.0)
                {
                    rayDirection = normalize(rayQuery.WorldRayOrigin() - hitPosition);
                }

                reflectionColor += GetBRDFPointLight(albedo,
                                                     normal,
                                                     hitPosition,
                                                     roughness,
                                                     metallic,
                                                     threadId.xy,
                                                     false);
                bounceIndex++;
            }
            else
            {
                break;
            }
        }
        reflectionUAV[threadId.xy] = float4(reflectionColor, 1.0);
    }
}