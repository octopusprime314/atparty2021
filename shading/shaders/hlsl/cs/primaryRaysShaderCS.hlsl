#include "../include/structs.hlsl"

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

RWTexture2D<float4> debug0UAV : register(u3);
RWTexture2D<float4> debug1UAV : register(u4);
RWTexture2D<float4> debug2UAV : register(u5);

SamplerState bilinearWrap : register(s0);

cbuffer globalData : register(b0)
{
    float4x4 viewTransform;
    float4x4 inverseView;
    float2   screenSize;
    uint      texturesPerMaterial;
}

#include "../include/utils.hlsl"

static float reflectionIndex = 0.5;
static float refractionIndex = 1.0 - reflectionIndex;

// Camera ray with projective rays eminating from a single point being the eye location
inline void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
    // Projection ray
    float3 cameraPosition = float3(inverseView[3][0], inverseView[3][1], inverseView[3][2]);
    origin                = cameraPosition;

    float fov              = 45.0f;
    float imageAspectRatio = screenSize.x / screenSize.y; // assuming width > height
    float Px = (2.0 * ((index.x + 0.5) / screenSize.x) - 1.0) * tan(fov / 2.0 * PI / 180.0) * imageAspectRatio;
    float Py = (1.0 - 2.0 * ((index.y + 0.5) / screenSize.y)) * tan(fov / 2.0 * PI / 180.0); 

    float4 rayDirection   = float4(Px, Py, 1.0, 1.0);
    float4 rayOrigin      = float4(0, 0, 0, 1.0);
    float3 rayOriginWorld = mul(viewTransform, rayOrigin).xyz;
    float3 rayPWorld      = mul(viewTransform, rayDirection).xyz;
    direction             = normalize(rayPWorld - rayOriginWorld);
}

[numthreads(8, 8, 1)]

    void
    main(int3 threadId           : SV_DispatchThreadID,
         int3 threadGroupThreadId : SV_GroupThreadID) {
        float3 rayDir;
        float3 origin;

        // Why does the y component of the shadow texture mapping need to be 1.0 - yCoord?
        GenerateCameraRay(threadId.xy, origin, rayDir);

        // Trace the ray.
        // Set the ray's extents.
        RayDesc ray;
        ray.Origin    = origin;
        ray.Direction = rayDir;
        ray.TMin      = 0.0;
        ray.TMax      = 100000.0;

        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;
        rayQuery.TraceRayInline(rtAS, RAY_FLAG_NONE, ~0, ray);

        // transparency processing
        while (rayQuery.Proceed())
        {
            if (rayQuery.CandidateTriangleRayT() < rayQuery.CommittedRayT())
            {
                float3 hitPosition = rayQuery.WorldRayOrigin() +
                                     (rayQuery.CandidateTriangleRayT() * rayQuery.WorldRayDirection());
            
                int geometryIndex  = rayQuery.CandidateGeometryIndex();
                int primitiveIndex = rayQuery.CandidatePrimitiveIndex();
                int instanceIndex  = rayQuery.CandidateInstanceIndex();

                int materialIndex = instanceIndexToMaterialMapping[instanceIndex] + (geometryIndex * texturesPerMaterial);
                int attributeIndex = instanceIndexToAttributesMapping[instanceIndex] + geometryIndex;

                float2 uvCoord = GetTexCoord(rayQuery.CandidateTriangleBarycentrics(), attributeIndex, primitiveIndex);

                // This is a trasmittive material dielectric like glass or water
                if (uniformMaterials[attributeIndex].transmittance > 0.0)
                {
                    rayQuery.CommitNonOpaqueTriangleHit();
                }
                // Alpha transparency texture that is treated as alpha cutoff for leafs and foliage, etc.
                else if (uniformMaterials[attributeIndex].transmittance == 0.0)
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
            float3 hitPosition = rayQuery.WorldRayOrigin() +
                                 (rayQuery.CommittedRayT() * rayQuery.WorldRayDirection());

            int geometryIndex  = rayQuery.CommittedGeometryIndex();
            int primitiveIndex = rayQuery.CommittedPrimitiveIndex();
            int instanceIndex  = rayQuery.CommittedInstanceIndex();

            int materialIndex = instanceIndexToMaterialMapping[instanceIndex] + (geometryIndex * texturesPerMaterial);
            int attributeIndex = instanceIndexToAttributesMapping[instanceIndex] + geometryIndex;

            float2 uvCoord = GetTexCoord(rayQuery.CommittedTriangleBarycentrics(), attributeIndex,
                                         primitiveIndex);

            float3 rayP0 = rayQuery.WorldRayOrigin() + (rayQuery.WorldRayDirection() * ray.TMin);
            float3 rayP1 = rayQuery.WorldRayOrigin() + (rayQuery.WorldRayDirection() * ray.TMax);

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

            // FUCK THIS MATRIX DECOMPOSITION BULLSHIT!!!!
            float4x3 cachedTransform = rayQuery.CommittedObjectToWorld4x3();
            float4x4 objectToWorldTransform = {float4(cachedTransform[0].xyz, 0.0),
                                               float4(cachedTransform[1].xyz, 0.0),
                                               float4(cachedTransform[2].xyz, 0.0),
                                               float4(cachedTransform[3].xyz, 1.0)};

            float mipLevel = 0; // ComputeMipLevel(rayQuery.CommittedTriangleBarycentrics(),
                             //              attributeIndex, primitiveIndex, rayP0, rayP1,
                             //              hitPosition, threadId.xy, objectToWorldTransform, instanceNormalMatrixTransform);

            float3 albedo = float3(0.0, 0.0, 0.0);
            if (uniformMaterials[attributeIndex].validBits & ColorValidBit)
            {
                albedo = uniformMaterials[attributeIndex].baseColor;
            }
            else
            {
                albedo = pow(diffuseTexture[NonUniformResourceIndex(materialIndex)].SampleLevel(bilinearWrap, uvCoord, mipLevel).xyz, 2.2);
            }

            float roughness = 0.0;

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

            float metallic = 0.0;
            if (uniformMaterials[attributeIndex].validBits & MetallicValidBit)
            {
                metallic = uniformMaterials[attributeIndex].metallic;
            }

            float3 normal = float3(0.0, 0.0, 0.0);

            if (uniformMaterials[attributeIndex].validBits & NormalValidBit)
            {
                normal = -GetNormalCoord(rayQuery.CommittedTriangleBarycentrics(), attributeIndex, primitiveIndex);
            }
            else
            {
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
                    normal = -normalize(mul(tbnMat[2], instanceNormalMatrixTransform));
                }
                else
                {
                    float3x3 tbnMatNormalTransform = mul(tbnMat, instanceNormalMatrixTransform);

                    normal = -normalize(mul(normalMap, tbnMatNormalTransform));
                }
            }

            normalUAV[threadId.xy].xyz   = normal;
            positionUAV[threadId.xy].xyz = hitPosition;
            albedoUAV[threadId.xy].xyz   = albedo.xyz;

            normalUAV[threadId.xy].w   = roughness;
            positionUAV[threadId.xy].w = metallic;
            albedoUAV[threadId.xy].w   = uniformMaterials[attributeIndex].transmittance;

            // If transmittive glass is detected then zero out albedo for now
            // In the future you can have colored glass than can provide a diffuse color component
            if (uniformMaterials[attributeIndex].transmittance > 0.0)
            {
                albedoUAV[threadId.xy].xyz = float3(0.0, 0.0, 0.0);
            }
        }
        else
        {
            float3 sampleVector = -normalize(rayDir);

            float4 dayColor = skyboxTexture.SampleLevel(
                bilinearWrap, float3(sampleVector.x, sampleVector.y, sampleVector.z), 0);

            albedoUAV[threadId.xy]   = float4(0.0, 0.0, 0.0, 0.0);
            normalUAV[threadId.xy]   = float4(0.0, 0.0, 0.0, 0.0);
            positionUAV[threadId.xy] = float4(0.0, 0.0, 0.0, 0.0);
        }
    }