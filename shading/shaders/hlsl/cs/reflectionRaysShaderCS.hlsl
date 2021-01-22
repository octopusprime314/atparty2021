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
Buffer<float>                               instanceTransmissionMapping      : register(t10, space0);
StructuredBuffer<AlignedHemisphereSample3D> sampleSets                       : register(t11, space0);

RWTexture2D<float4> reflectionUAV : register(u0);
RWTexture2D<float4> pointLightOcclusionUAV : register(u1);
RWTexture2D<float4> pointLightOcclusionHistoryUAV : register(u2);

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

float3 GetLightingColor(float3 position,
                        float3 rayDirection,
                        float3 normal,
                        int2   threadId,
                        float3 albedo,
                        float  roughness,
                        float  metallic,
                        float  transmittance)
{
    float3 reflectionColor =
        GetBRDFPointLight(albedo, normal, position, roughness, metallic, threadId.xy, false, 0.0);

    uint maxReflectionBounces = 1000;
    uint bounceIndex          = 0;
    while (roughness < 0.5 && bounceIndex < maxReflectionBounces)
    {
        // Trace the ray.
        // Set the ray's extents.
        RayDesc ray;
        ray.TMin   = 0.1;
        ray.TMax   = 100000.0;
        ray.Origin = position;

        // Punch through ray with zero reflection
        if (transmittance < 1.0)
        {
            ray.Direction = rayDirection;
        }
        // Opaque materials make a reflected ray
        else
        {
            ray.Direction = rayDirection - (2.0f * dot(rayDirection, normal) * normal);
        }

        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;
        rayQuery.TraceRayInline(rtAS, RAY_FLAG_NONE, ~0, ray);

        // transparency processing
        while (rayQuery.Proceed())
        {
            if (rayQuery.CandidateTriangleRayT() < rayQuery.CommittedRayT())
            {
                float3 hitPosition = rayQuery.WorldRayOrigin() + (rayQuery.CandidateTriangleRayT() *
                                                                  rayQuery.WorldRayDirection());

                int geometryIndex  = rayQuery.CandidateGeometryIndex();
                int primitiveIndex = rayQuery.CandidatePrimitiveIndex();
                int instanceIndex  = rayQuery.CandidateInstanceIndex();

                int materialIndex =
                    instanceIndexToMaterialMapping[instanceIndex] + (geometryIndex * texturesPerMaterial);

                int attributeIndex =
                    instanceIndexToAttributesMapping[instanceIndex] + geometryIndex;

                float2 uvCoord = GetTexCoord(rayQuery.CandidateTriangleBarycentrics(),
                                             attributeIndex, primitiveIndex);

                // This is a trasmittive material dielectric like glass or water
                if (instanceTransmissionMapping[attributeIndex] < 1.0)
                {
                    rayQuery.CommitNonOpaqueTriangleHit();
                }
                // Alpha transparency texture that is treated as alpha cutoff for leafs and foliage,
                // etc.
                else if (instanceTransmissionMapping[attributeIndex] == 1.0)
                {
                    float alpha = diffuseTexture[NonUniformResourceIndex(materialIndex)]
                                      .SampleLevel(bilinearWrap, uvCoord, 0)
                                      .w;

                    if (alpha >= 0.9)
                    {
                        rayQuery.CommitNonOpaqueTriangleHit();
                    }
                }
            }
        }

        if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {

            float3 newPosition =
                rayQuery.WorldRayOrigin() + (rayQuery.CommittedRayT() * rayQuery.WorldRayDirection());

            position = newPosition;

            int geometryIndex  = rayQuery.CommittedGeometryIndex();
            int primitiveIndex = rayQuery.CommittedPrimitiveIndex();
            int instanceIndex  = rayQuery.CommittedInstanceIndex();

            int materialIndex  = instanceIndexToMaterialMapping[instanceIndex] + (geometryIndex * texturesPerMaterial);
            int attributeIndex = instanceIndexToAttributesMapping[instanceIndex] + geometryIndex;

            float2 uvCoord = GetTexCoord(rayQuery.CommittedTriangleBarycentrics(), attributeIndex,
                                         primitiveIndex);

            transmittance = instanceTransmissionMapping[attributeIndex];

            // Punch through ray with zero reflection
            if (transmittance < 1.0)
            {
                ray.Direction = rayDirection;
            }
            // Opaque materials make a reflected ray
            else
            {
                rayDirection = normalize(newPosition - position);
            }

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
            float4x3 cachedTransform = rayQuery.CommittedObjectToWorld4x3();
            float4x4 objectToWorldTransform = {float4(cachedTransform[0].xyz, 0.0),
                                               float4(cachedTransform[1].xyz, 0.0),
                                               float4(cachedTransform[2].xyz, 0.0),
                                               float4(cachedTransform[3].xyz, 1.0)};

            float mipLevel = 0;//ComputeMipLevel(rayQuery.CommittedTriangleBarycentrics(),
                               //              attributeIndex, primitiveIndex, rayP0, rayP1,
                               //              position, threadId.xy, objectToWorldTransform, instanceNormalMatrixTransform);

            albedo = pow(diffuseTexture[NonUniformResourceIndex(materialIndex)]
                                    .SampleLevel(bilinearWrap, uvCoord, mipLevel)
                                    .xyz,
                                2.2);

            roughness = diffuseTexture[NonUniformResourceIndex(materialIndex + 2)]
                        .SampleLevel(bilinearWrap, uvCoord, mipLevel)
                        .y;

            normal = float3(0.0, 0.0, 0.0);

            float3 normalMap = diffuseTexture[NonUniformResourceIndex(materialIndex + 1)]
                                    .SampleLevel(bilinearWrap, uvCoord, mipLevel)
                                    .xyz;

            // Converts from [0,1] space to [-1,1] space
            normalMap = normalMap * 2.0f - 1.0f;

            // Compute the normal from loading the triangle vertices
            float3x3 tbnMat = GetTBN(rayQuery.CommittedTriangleBarycentrics(), attributeIndex, primitiveIndex);

            // If there is a failure in getting the TBN matrix then use the computed normal without normal mappings
            if (any(isnan(tbnMat[0])))
            {
                normal = normalize(mul(-tbnMat[2], instanceNormalMatrixTransform));
            }
            else
            {
                float3x3 tbnMatNormalTransform = mul(tbnMat, instanceNormalMatrixTransform);

                normal = normalize(mul(-normalMap, tbnMatNormalTransform));
            }

            reflectionColor += GetBRDFPointLight(albedo, normal, position, roughness, metallic,
                                                 threadId.xy, false, 0.0);
            bounceIndex++;
        }
        else
        {
            return reflectionColor;
        }
    }
    return reflectionColor;
}

[numthreads(8, 8, 1)]

void main(int3 threadId : SV_DispatchThreadID,
        int3 threadGroupThreadId: SV_GroupThreadID)
{
    float3 normal = normalSRV[threadId.xy].xyz;

    if (normal.x == 0.0 && normal.y == 0.0 && normal.z == 0.0)
    {
        reflectionUAV[threadId.xy] = float4(0.0, 0.0, 0.0, 0.0);
    }
    else
    {
        float  metallic      = 0.0;
        float  roughness     = normalSRV[threadId.xy].w;
        float3 position      = positionSRV[threadId.xy].xyz;
        float  transmittance = albedoSRV[threadId.xy].w;
        // Reconstruct primary ray by taking the camera position and subtracting it from hit
        // position
        float3 cameraPosition = float3(inverseView[3][0], inverseView[3][1], inverseView[3][2]);
        float3 rayDirection   = normalize(position - cameraPosition);

        float3 lightingColor = GetLightingColor(position, rayDirection, normal, threadId.xy,
                             albedoSRV[threadId.xy].xyz, roughness, metallic, transmittance);

        reflectionUAV[threadId.xy] = float4(lightingColor, 1.0);
    }
}