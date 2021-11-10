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
Texture2D                                   viewZSRV                         : register(t12, space0);

RWTexture2D<float4> reflectionUAV : register(u0);
RWTexture2D<float2> occlusionUAV : register(u1);
RWTexture2D<float4> occlusionHistoryUAV : register(u2);
RWTexture2D<float4> indirectLightRaysUAV : register(u3);
RWTexture2D<float4> indirectLightRaysHistoryBufferUAV : register(u4);
RWTexture2D<float4> indirectSpecularLightRaysUAV : register(u5);
RWTexture2D<float4> indirectSpecularLightRaysHistoryBufferUAV : register(u6);

SamplerState bilinearWrap : register(s0);

cbuffer globalData : register(b0)
{
    float4x4 inverseView;
    float4x4 viewTransform;

    float4 pointLightColors[MAX_LIGHTS];
    float4 pointLightRanges[MAX_LIGHTS / 4];
    float4 pointLightPositions[MAX_LIGHTS];

    float4 sunLightColor;
    float4 sunLightPosition;
    float2 screenSize;
    float  sunLightRadius;
    float  sunLightRange;
    
    int    numPointLights;

    uint seed;
    uint numSamplesPerSet;
    uint numSampleSets;
    uint numPixelsPerDimPerSet;
    uint texturesPerMaterial;

    uint resetHistoryBuffer;
}

#include "../../include/pointLightCommon.hlsl"
#include "../../include/sunLightCommon.hlsl"
#include "../../include/utils.hlsl"

static float reflectionIndex = 0.5;
static float refractionIndex = 1.0 - reflectionIndex;

#define RNG_BRDF_X(bounce) (4 + 4 + 9 * bounce)
#define RNG_BRDF_Y(bounce) (4 + 5 + 9 * bounce)

[numthreads(8, 8, 1)]

void main(int3 threadId            : SV_DispatchThreadID,
          int3 threadGroupThreadId : SV_GroupThreadID)
{
    float3 normal = (normalSRV[threadId.xy].xyz * 2.0) - 1.0;
    float3 debugNormal = normal;

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
        float  metallic      = 0.0;//positionSRV[threadId.xy].w;
        float  roughness     = normalSRV[threadId.xy].w;

        // Reconstruct primary ray by taking the camera position and subtracting it from hit
        // position
        float3 cameraPosition = float3(inverseView[3][0], inverseView[3][1], inverseView[3][2]);
        float3 objectPosition = mul(inverseView, float4(hitPosition, 1.0)).xyz;
        float3 rayDirection   = normalize(objectPosition - cameraPosition);

        uint   bounceIndex = 0;

        //float3 reflectionColor = GetBRDFPointLight(albedo,
        //                                           normal,
        //                                           hitPosition,
        //                                           roughness,
        //                                           metallic,
        //                                           threadId.xy,
        //                                           false,
        //                                           bounceIndex);

        float3 jitteredNormal = GetRandomRayDirection(threadId.xy, normal.xyz, (uint2)screenSize, 0);

        float3 diffuseRadiance = float3(0.0, 0.0, 0.0);
        float3 specularRadiance = float3(0.0, 0.0, 0.0);
        float3 lightRadiance;
        float3 reflectionColor  = GetBRDFSunLight(albedo, normal, hitPosition, roughness, metallic, threadId.xy,
                            diffuseRadiance, specularRadiance, lightRadiance, true);

        float3 reflectionColorDiffuse  = diffuseRadiance * lightRadiance;
        float3 reflectionColorSpecular = specularRadiance * lightRadiance;

        //while (/*roughness < 0.5 && */bounceIndex < 1)
        //{
        //    // Trace the ray.
        //    // Set the ray's extents.
        //    RayDesc ray;
        //    ray.TMin   = MIN_RAY_LENGTH;
        //    ray.TMax   = MAX_RAY_LENGTH;
        //    ray.Origin = hitPosition + ((normal) * 0.001);

        //    // Punch through ray with zero reflection
        //    if (transmittance > 0.0)
        //    {
        //        ray.Direction = rayDirection;
        //    }
        //    // Opaque materials make a reflected ray
        //    else
        //    {
        //        ray.Direction = rayDirection - (2.0f * dot(rayDirection, normal) * normal);
        //    }

        //    RayQuery<RAY_FLAG_NONE> rayQuery;
        //    rayQuery.TraceRayInline(rtAS, RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, ~0, ray);

        //    while (rayQuery.Proceed())
        //    {
        //        RayTraversalData rayData;
        //        rayData.worldRayOrigin    = rayQuery.WorldRayOrigin();
        //        rayData.currentRayT       = rayQuery.CandidateTriangleRayT();
        //        rayData.closestRayT       = rayQuery.CommittedRayT();
        //        rayData.worldRayDirection = rayQuery.WorldRayDirection();
        //        rayData.geometryIndex     = rayQuery.CandidateGeometryIndex();
        //        rayData.primitiveIndex    = rayQuery.CandidatePrimitiveIndex();
        //        rayData.instanceIndex     = rayQuery.CandidateInstanceIndex();
        //        rayData.barycentrics      = rayQuery.CandidateTriangleBarycentrics();
        //        rayData.objectToWorld     = rayQuery.CandidateObjectToWorld4x3();

        //        bool isHit = ProcessTransparentTriangle(rayData);
        //        if (isHit)
        //        {
        //            rayQuery.CommitNonOpaqueTriangleHit();
        //        }
        //    }

        //    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        //    {
        //        float3 prevPosition = hitPosition;

        //        RayTraversalData rayData;
        //        rayData.worldRayOrigin    = rayQuery.WorldRayOrigin();
        //        rayData.closestRayT       = rayQuery.CommittedRayT();
        //        rayData.worldRayDirection = rayQuery.WorldRayDirection();
        //        rayData.geometryIndex     = rayQuery.CommittedGeometryIndex();
        //        rayData.primitiveIndex    = rayQuery.CommittedPrimitiveIndex();
        //        rayData.instanceIndex     = rayQuery.CommittedInstanceIndex();
        //        rayData.barycentrics      = rayQuery.CommittedTriangleBarycentrics();
        //        rayData.objectToWorld     = rayQuery.CommittedObjectToWorld4x3();
        //        rayData.uvIsValid         = false;

        //        ProcessOpaqueTriangle(rayData,
        //                              albedo,
        //                              roughness,
        //                              metallic,
        //                              normal,
        //                              hitPosition,
        //                              transmittance);

        //        // Reflection ray direction
        //        if (transmittance == 0.0)
        //        {
        //            rayDirection = normalize(rayQuery.WorldRayOrigin() - hitPosition);
        //        }

        //        //reflectionColor += GetBRDFPointLight(albedo,
        //        //                                     normal,
        //        //                                     hitPosition,
        //        //                                     roughness,
        //        //                                     metallic,
        //        //                                     threadId.xy,
        //        //                                     false,
        //        //                                     bounceIndex) / 2.0;

        //        //reflectionColor +=
        //        //    GetBRDFSunLight(albedo, normal, hitPosition, roughness, metallic, threadId.xy, diffuseRadiance);

        //        reflectionColor = GetBRDFSunLight(albedo, normal, hitPosition, roughness, metallic, threadId.xy, diffuseRadiance);

        //        //if (resetHistoryBuffer == 1)
        //        //{
        //        //    indirectLightRaysHistoryBufferUAV[threadId.xy] = float4(diffuseRadiance, 1.0);
        //        //}
        //        //else/* if (length(diffuseRadiance) > 0.0)*/
        //        //{
        //        //    indirectLightRaysUAV[threadId.xy].rgb = diffuseRadiance;

        //        //    indirectLightRaysHistoryBufferUAV[threadId.xy].w += 1.0;

        //        //    indirectLightRaysHistoryBufferUAV[threadId.xy].xyz +=
        //        //        (diffuseRadiance - indirectLightRaysHistoryBufferUAV[threadId.xy].rgb) /
        //        //        indirectLightRaysHistoryBufferUAV[threadId.xy].w;
        //        //}


        //        //reflectionColor = diffuseRadiance;

        //        bounceIndex++;
        //    }
        //    else
        //    {
        //        float3 sampleVector = normalize(ray.Direction);
        //        
        //        reflectionColor =
        //            (float4(137.0 / 256.0, 207.0 / 256.0, 240.0 / 256.0, 0.0) * (sampleVector.y*3.0) +
        //            float4(0.0, 0.0, 0.44, 0.0) * (1.0 - sampleVector.y*3.0)).xyz;

        //        // Indirect diffuse ray
        //        ray.Direction = GetRandomRayDirection(threadId.xy, normal, screenSize, 0);

        //        rayQuery.TraceRayInline(rtAS, RAY_FLAG_NONE, ~0, ray);

        //        rayQuery.Proceed();

        //        if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
        //        {
        //            float3 prevPosition = hitPosition;

        //            RayTraversalData rayData;
        //            rayData.worldRayOrigin    = rayQuery.WorldRayOrigin();
        //            rayData.closestRayT       = rayQuery.CommittedRayT();
        //            rayData.worldRayDirection = rayQuery.WorldRayDirection();
        //            rayData.geometryIndex     = rayQuery.CommittedGeometryIndex();
        //            rayData.primitiveIndex    = rayQuery.CommittedPrimitiveIndex();
        //            rayData.instanceIndex     = rayQuery.CommittedInstanceIndex();
        //            rayData.barycentrics      = rayQuery.CommittedTriangleBarycentrics();
        //            rayData.objectToWorld     = rayQuery.CommittedObjectToWorld4x3();
        //            rayData.uvIsValid         = false;

        //            float transmittance = 0.0;
        //            ProcessOpaqueTriangle(rayData, albedo, roughness, metallic, normal, hitPosition,
        //                                  transmittance);

        //            //if (resetHistoryBuffer == 1)
        //            //{
        //            //    indirectLightRaysHistoryBufferUAV[threadId.xy] = float4(albedo, 1.0);
        //            //}
        //            //else /*if (length(albedo) > 0.0)*/
        //            //{
        //            //    indirectLightRaysUAV[threadId.xy].rgb = albedo;

        //            //    indirectLightRaysHistoryBufferUAV[threadId.xy].w += 1.0;

        //            //    indirectLightRaysHistoryBufferUAV[threadId.xy].xyz +=
        //            //        (albedo - indirectLightRaysHistoryBufferUAV[threadId.xy].rgb) /
        //            //        indirectLightRaysHistoryBufferUAV[threadId.xy].w;
        //            //}
        //        }

        //        

        //        //reflectionColor += GetBRDFSunLight(albedo, normal, hitPosition, roughness, metallic, threadId.xy, diffuseRadiance) / 10.0f;
        //        //reflectionColor = float3(0.0, 0.0, 0.0);
        //        break;
        //    }
        //}

        //if (length(abs(reflectionColor)) != 0.0)
        //{
        //    diffuseRadiance  *= /*pathRayNdotL **/ sunLightColor.xyz;
        //    specularRadiance *= /*pathRayNdotL **/ sunLightColor.xyz;
        //}

        float3 indirectLighting = float3(0.0, 0.0, 0.0);
        float3 indirectRadiance = float3(0.0, 0.0, 0.0);
        float3 indirectPos      = positionSRV[threadId.xy].xyz;
        float3 indirectNormal   = debugNormal;
        float  indirectHitDistance = 0.0;
        float3 indirectAlbedo      = float3(0.0, 0.0, 0.0);
        float3 indirectLightEnergy = diffuseRadiance;

        float3 indirectSpecularLightEnergy = specularRadiance * lightRadiance;

        float3 indirectDiffuseLightEnergy = diffuseRadiance;

        float indirectHitDistanceSpecular = 0.0;

        float3 indirectSpecular = float3(0.0, 0.0, 0.0);
        int i = 0;

        for (i = 0; i < 1; i++)
        {
            indirectNormal = normalize(-indirectNormal);

            // Specular
            float3x3 basis = orthoNormalBasis(indirectNormal);

            // Sampling of normal distribution function to compute the reflected ray.
            // See the paper "Sampling the GGX Distribution of Visible Normals" by E. Heitz,
            // Journal of Computer Graphics Techniques Vol. 7, No. 4, 2018.
            // http://jcgt.org/published/0007/04/01/paper.pdf

            float3 cameraPosition = float3(inverseView[3][0], inverseView[3][1], inverseView[3][2]);
            float3 eyeVector      = normalize(indirectPos - cameraPosition);

            float2 rng3 = GetRandomSample(threadId.xy, screenSize).xy;

            float3 N = indirectNormal;
            float3 V = eyeVector;
            float3 H = ImportanceSampleGGX_VNDF(rng3, roughness, V, basis);
            float3 L = float3(0.0, 0.0, 0.0);

            //// Punch through ray with zero reflection
            //if (transmittance > 0.0)
            //{
            //    L = rayDirection;
            //}
            //// Opaque materials make a reflected ray
            //else
            //{
                L = reflect(V, H);
            //}

            float NoV = max(0, -dot(indirectNormal, eyeVector));
            float NoL = max(0, dot(N, L));
            float NoH = max(0, dot(N, H));
            float VoH = max(0, -dot(V, H));

            if (NoL > 0 && NoV > 0)
            {
                // See the Heitz paper referenced above for the estimator explanation.
                //   (BRDF / PDF) = F * G2(V, L) / G1(V)
                // The Fresnel term F is already embedded into "primary_specular" by
                // direct_lighting.rgen. Assume G2 = G1(V) * G1(L) here and simplify that
                // expression to just G1(L).

                // float G1_NoL = G1_Smith(primary_roughness, NoL);
                //
                // bounce_throughput *= G1_NoL;
                //
                // bounce_throughput *= 1 / specular_pdf;
                // is_specular_ray  = true;

                float3 specularRayDirection = normalize(L);
                //float3 specularRayDirection = rayDirection - (2.0f * dot(rayDirection, indirectNormal) * indirectNormal);

                RayDesc raySpecular;
                raySpecular.TMin = MIN_RAY_LENGTH;
                raySpecular.TMax = MAX_RAY_LENGTH;

                //if (transmittance > 0.0)
                //{
                    raySpecular.Origin = indirectPos + (indirectNormal * 0.001);
                //}
                //else
                //{
                //    raySpecular.Origin = indirectPos + (indirectNormal * -0.001);
                //}
                raySpecular.Direction = specularRayDirection;

                RayQuery<RAY_FLAG_NONE> rayQuerySpecular;
                rayQuerySpecular.TraceRayInline(rtAS, RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, ~0, raySpecular);

                rayQuerySpecular.Proceed();

                if (rayQuerySpecular.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
                {
                    RayTraversalData rayData;
                    rayData.worldRayOrigin    = rayQuerySpecular.WorldRayOrigin();
                    rayData.closestRayT       = rayQuerySpecular.CommittedRayT();
                    rayData.worldRayDirection = rayQuerySpecular.WorldRayDirection();
                    rayData.geometryIndex     = rayQuerySpecular.CommittedGeometryIndex();
                    rayData.primitiveIndex    = rayQuerySpecular.CommittedPrimitiveIndex();
                    rayData.instanceIndex     = rayQuerySpecular.CommittedInstanceIndex();
                    rayData.barycentrics      = rayQuerySpecular.CommittedTriangleBarycentrics();
                    rayData.objectToWorld     = rayQuerySpecular.CommittedObjectToWorld4x3();
                    rayData.uvIsValid         = false;

                    ProcessOpaqueTriangle(rayData, albedo, roughness, metallic, indirectNormal,
                                            indirectPos, transmittance);

                    float3 currIndirectRadiance = float3(0.0, 0.0, 0.0);
                    float3  currIndirectSpecularRadiance = float3(0.0, 0.0, 0.0);
                    float3 currLightRadiance = float3(0.0, 0.0, 0.0);
                    float3 indirectLighting = GetBRDFSunLight(albedo, indirectNormal, indirectPos, roughness, metallic, threadId.xy, currIndirectRadiance,
                        currIndirectSpecularRadiance, currLightRadiance);

                    indirectHitDistanceSpecular += rayQuerySpecular.CommittedRayT();

                    indirectSpecular += ((currIndirectSpecularRadiance * indirectSpecularLightEnergy) + 
                                         (currIndirectRadiance * indirectDiffuseLightEnergy)) *
                                          currLightRadiance;

                    //indirectAlbedo += (currIndirectRadiance * currLightRadiance) * indirectDiffuseLightEnergy;

                    indirectSpecularLightEnergy *= currIndirectSpecularRadiance;

                    indirectDiffuseLightEnergy *= currIndirectRadiance;
                }
                else
                {
                    break;
                }
            }
        }

        // struct HitDistanceParameters
        //{
        //    float A = 3.0f;     // constant value (m)
        //    float B = 0.1f;     // viewZ based linear scale (m / units) (1 m - 10 cm, 10 m - 1 m,
        //    100 m - 10 m) float C = 10.0f;    // roughness based scale, "> 1" to get bigger hit
        //    distance for low roughness float D = -25.0f;   // roughness based exponential scale,
        //    "< 0", absolute value should be big enough to collapse "exp2( D * roughness ^ 2 )" to
        //    "~0" for roughness = 1
        //};

        float4 hitDistanceParams = float4(3.0f, 0.1f, 10.0f, -25.0f);
        float normHitDist = REBLUR_FrontEnd_GetNormHitDist(indirectHitDistanceSpecular, viewZSRV[threadId.xy].x, hitDistanceParams);

        indirectSpecularLightRaysUAV[threadId.xy] = REBLUR_FrontEnd_PackRadiance(
            indirectSpecular /*indirectSpecularLightEnergy*/ /*reflectionColor*/
            /*reflectionColorSpecular*/ /*debugNormal*/, normHitDist);

        i = 0;
        indirectPos    = positionSRV[threadId.xy].xyz;
        indirectNormal = debugNormal;

        for (i = 0; i < 10; i++)
        {
            indirectNormal = normalize(-indirectNormal);

            // Diffuse
            RayDesc ray;
            ray.TMin      = MIN_RAY_LENGTH;
            ray.TMax      = MAX_RAY_LENGTH;

            ray.Origin    = indirectPos + (indirectNormal * 0.001);
            ray.Direction = GetRandomRayDirection(threadId.xy, indirectNormal, screenSize, 0);

            RayQuery<RAY_FLAG_NONE> rayQuery;
            rayQuery.TraceRayInline(rtAS, RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, ~0, ray);

            rayQuery.Proceed();

            if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
            {
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

                float transmittance = 0.0;
                ProcessOpaqueTriangle(rayData, albedo, roughness, metallic, indirectNormal, indirectPos,
                                      transmittance);

                float3 currIndirectRadiance = float3(0.0, 0.0, 0.0);
                float3  currIndirectSpecularRadiance = float3(0.0, 0.0, 0.0);
                float3 currLightRadiance;
                float3 indirectDiffuseLighting = GetBRDFSunLight(albedo, indirectNormal, indirectPos, roughness, metallic, threadId.xy, currIndirectRadiance,
                                                   currIndirectSpecularRadiance, currLightRadiance);

                indirectHitDistance += rayQuery.CommittedRayT();

                indirectAlbedo += currIndirectRadiance * currLightRadiance * indirectLightEnergy;

                indirectLightEnergy *= currIndirectRadiance;
            }
            else
            {
                break;
            }
        }

        normHitDist = REBLUR_FrontEnd_GetNormHitDist(indirectHitDistance, viewZSRV[threadId.xy].x, hitDistanceParams);
        
        indirectLightRaysUAV[threadId.xy] =
            REBLUR_FrontEnd_PackRadiance(indirectAlbedo.rgb + reflectionColor, normHitDist);

        float3 color = (((REBLUR_BackEnd_UnpackRadiance(indirectSpecularLightRaysHistoryBufferUAV[threadId.xy])/* * 0.000001*/)) +
                        (REBLUR_BackEnd_UnpackRadiance(indirectLightRaysHistoryBufferUAV[threadId.xy])/* * 0.000001*/)).xyz;
        float colorScale = 1.0f / 2.2f;
        color            = color / (color + float3(1.0f, 1.0f, 1.0f));
        color            = pow(color, colorScale);


        reflectionUAV[threadId.xy] = float4(color, 1.0);
        //reflectionUAV[threadId.xy] = (REBLUR_BackEnd_UnpackRadiance(indirectLightRaysHistoryBufferUAV[threadId.xy]));
        
    }
}