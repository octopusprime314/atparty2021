#include "../../include/structs.hlsl"
#include "../../include/dxr1_1_defines.hlsl"

RaytracingAccelerationStructure       rtAS                             : register(t0, space0);
Texture2D                             diffuseTexture[]                 : register(t1, space1);
StructuredBuffer<CompressedAttribute> vertexBuffer[]                   : register(t2, space2);
Buffer<uint>                          indexBuffer[]                    : register(t3, space3);
Buffer<uint>                          instanceIndexToMaterialMapping   : register(t4, space0);
Buffer<uint>                          instanceIndexToAttributesMapping : register(t5, space0);
Buffer<float>                         instanceNormalMatrixTransforms   : register(t6, space0);
StructuredBuffer<UniformMaterial>     uniformMaterials                 : register(t7, space0);
TextureCube                           skyboxTexture                    : register(t8, space0);


RWTexture2D<float4> albedoUAV   : register(u0);
RWTexture2D<float4> positionUAV : register(u1);
RWTexture2D<float4> normalUAV   : register(u2);
RWTexture2D<float4> viewZUAV    : register(u3);


SamplerState bilinearWrap : register(s0);

cbuffer globalData : register(b0)
{
    float4x4 viewTransform;
    float4x4 inverseView;
    float2   screenSize;
    uint     texturesPerMaterial;
    float3   cameraPos;
    float    fov;
}

#include "../../include/utils.hlsl"

static float reflectionIndex = 0.5;
static float refractionIndex = 1.0 - reflectionIndex;

[numthreads(8, 8, 1)]

    void
    main(int3 threadId           : SV_DispatchThreadID,
         int3 threadGroupThreadId : SV_GroupThreadID) {
        float3 rayDir;
        float3 origin;

        GenerateCameraRay(threadId.xy, origin, rayDir, viewTransform);

        RayDesc ray;
        ray.Origin    = origin;
        ray.Direction = rayDir;
        ray.TMin      = MIN_RAY_LENGTH;
        ray.TMax      = MAX_RAY_LENGTH;

        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE> rayQuery;
        rayQuery.TraceRayInline(rtAS, RAY_FLAG_NONE, ~0, ray);
        rayQuery.Proceed();

        //while (rayQuery.Proceed())
        //{
        //    RayTraversalData rayData;
        //    rayData.worldRayOrigin    = rayQuery.WorldRayOrigin();
        //    rayData.currentRayT       = rayQuery.CandidateTriangleRayT();
        //    rayData.closestRayT       = rayQuery.CommittedRayT();
        //    rayData.worldRayDirection = rayQuery.WorldRayDirection();
        //    rayData.geometryIndex     = rayQuery.CandidateGeometryIndex();
        //    rayData.primitiveIndex    = rayQuery.CandidatePrimitiveIndex();
        //    rayData.instanceIndex     = rayQuery.CandidateInstanceIndex();
        //    rayData.barycentrics      = rayQuery.CandidateTriangleBarycentrics();
        //    rayData.objectToWorld     = rayQuery.CandidateObjectToWorld4x3();
        //
        //    bool isHit = ProcessTransparentTriangle(rayData);
        //    if (isHit)
        //    {
        //        rayQuery.CommitNonOpaqueTriangleHit();
        //    }
        //}

        if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        {
            float3 albedo;
            float  roughness;
            float  metallic;
            float3 normal;
            float3 hitPosition;
            float  transmittance;
            float3 emissiveColor;

            RayTraversalData rayData;
            rayData.worldRayOrigin    = rayQuery.WorldRayOrigin();
            rayData.closestRayT       = rayQuery.CommittedRayT();
            rayData.worldRayDirection = rayQuery.WorldRayDirection();
            rayData.geometryIndex     = rayQuery.CommittedGeometryIndex();
            rayData.primitiveIndex    = rayQuery.CommittedPrimitiveIndex();
            rayData.instanceIndex     = rayQuery.CommittedInstanceIndex();
            rayData.barycentrics      = rayQuery.CommittedTriangleBarycentrics();
            rayData.objectToWorld     = rayQuery.CommittedObjectToWorld4x3();
            rayData.uvIsValid         = false;

            ProcessOpaqueTriangle(rayData,
                                  albedo,
                                  roughness,
                                  metallic,
                                  normal,
                                  hitPosition,
                                  transmittance,
                                  emissiveColor);

            if (rayQuery.CommittedTriangleFrontFace() == false)
            {
                normal = -normal;
            }

            normalUAV[threadId.xy].xyz   = (normal + 1.0) / 2.0;
            positionUAV[threadId.xy].xyz = hitPosition;
            albedoUAV[threadId.xy].xyz   = albedo.xyz;

            // Denoiser can't handle roughness value of 0.0
            normalUAV[threadId.xy].w     = max(roughness, 0.05);
            positionUAV[threadId.xy].w   = rayData.instanceIndex;
            albedoUAV[threadId.xy].w     = metallic;

            viewZUAV[threadId.xy].x =  mul(float4(hitPosition, 1.0), viewTransform).z;
        }
        else
        {
            float3 sampleVector = normalize(rayDir);
            float4 dayColor     = skyboxTexture.SampleLevel(bilinearWrap, float3(sampleVector.x, sampleVector.y, sampleVector.z), 0);

            albedoUAV[threadId.xy]   = float4(dayColor.xyz, 0.0);
            normalUAV[threadId.xy]   = float4(0.0, 0.0, 0.0, 1.0);
            positionUAV[threadId.xy] = float4(0.0, 0.0, 0.0, -1.0);

            viewZUAV[threadId.xy].x = 1e5;
        }
    }