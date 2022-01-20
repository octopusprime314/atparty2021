#include "../../include/structs.hlsl"
#include "../../include/dxr1_1_defines.hlsl"

RaytracingAccelerationStructure             rtAS                             : register(t0, space0);
Texture2D                                   diffuseTexture[]                 : register(t1, space1);
StructuredBuffer<CompressedAttribute>       vertexBuffer[]                   : register(t2, space2);
Buffer<uint>                                indexBuffer[]                    : register(t3, space3);
Buffer<uint>                                instanceIndexToMaterialMapping   : register(t4, space0);
Buffer<uint>                                instanceIndexToAttributesMapping : register(t5, space0);
Buffer<float>                               instanceNormalMatrixTransforms   : register(t6, space0);
StructuredBuffer<UniformMaterial>           uniformMaterials                 : register(t7, space0);
StructuredBuffer<AlignedHemisphereSample3D> sampleSets                       : register(t8, space0);
TextureCube                                 skyboxTexture                    : register(t9, space0);
//Texture2D                                   indirectLightRaysHistoryBufferSRV         : register(t10, space0);
//Texture2D                                   indirectSpecularLightRaysHistoryBufferSRV : register(t11, space0);

RWTexture2D<float4> indirectLightRaysUAV : register(u0);
RWTexture2D<float4> indirectSpecularLightRaysUAV : register(u1);
RWTexture2D<float4> diffusePrimarySurfaceModulation : register(u2);
RWTexture2D<float4> specularPrimarySurfaceModulation : register(u3);
RWTexture2D<float4> albedoUAV : register(u4);
RWTexture2D<float4> positionUAV : register(u5);
RWTexture2D<float4> normalUAV : register(u6);
RWTexture2D<float4> viewZUAV : register(u7);


SamplerState bilinearWrap : register(s0);

#define USE_SANITIZATION 1

cbuffer globalData : register(b0)
{
    float4x4 inverseView;
    float4x4 viewTransform;

    float4 lightColors[MAX_LIGHTS];
    float4 lightPositions[MAX_LIGHTS];
    float4  lightRanges[MAX_LIGHTS/4];
    uint   isPointLight[MAX_LIGHTS];
    uint   numLights;

    float2 screenSize;

    uint seed;
    uint numSamplesPerSet;
    uint numSampleSets;
    uint numPixelsPerDimPerSet;
    uint texturesPerMaterial;

    uint resetHistoryBuffer;
    uint frameNumber;

    uint maxBounces;

    // class enum renderMode
    //{
    //    DIFFUSE_DENOISED = 0,
    //    SPECULAR_DENOISED = 1,
    //    BOTH_DIFFUSE_AND_SPECULAR = 2
    //    DIFFUSE_RAW = 3,
    //    SPECULAR_RAW = 4
    //};

    // Default to both specular and diffuse
    int renderMode;
    int rayBounceIndex;
}

#include "../../include/sunLightCommon.hlsl"
#include "../../include/utils.hlsl"

static float reflectionIndex = 0.5;
static float refractionIndex = 1.0 - reflectionIndex;

#define RNG_BRDF_X(bounce) (4 + 4 + 9 * bounce)
#define RNG_BRDF_Y(bounce) (4 + 5 + 9 * bounce)

float3 LinearToYCoCg(float3 color)
{
    float Co = color.x - color.z;
    float t  = color.z + Co * 0.5;
    float Cg = color.y - t;
    float Y  = t + Cg * 0.5;

    // TODO: useful, but not needed in many cases
    Y = max(Y, 0.0);

    return float3(Y, Co, Cg);
}

float3 YCoCgToLinear(float3 color)
{
    // TODO: useful, but not needed in many cases
    color.x = max(color.x, 0.0);

    float  t   = color.x - color.z * 0.5;
    float  g   = color.z + t;
    float  b   = t - color.y * 0.5;
    float  r   = b + color.y;
    float3 res = float3(r, g, b);

    return res;
}

[numthreads(8, 8, 1)]

void main(int3 threadId            : SV_DispatchThreadID,
          int3 threadGroupThreadId : SV_GroupThreadID)
{

    float4 diffHitDistParams = float4(3.0f, 0.1f, 10.0f, -25.0f);
    float4 specHitDistParams = float4(3.0f, 0.1f, 10.0f, -25.0f);

    float4 nrdSpecular = REBLUR_FrontEnd_PackRadiance(float3(0.0, 0.0, 0.0), 0.0, 0);
    float4 nrdDiffuse = REBLUR_FrontEnd_PackRadiance(float3(0.0, 0.0, 0.0), 0.0, 0);

    float3 indirectPos    = float3(0.0, 0.0, 0.0);
    float3 indirectNormal = float3(0.0, 0.0, 0.0);

    float3 throughput = float3(1.0, 1.0, 1.0);

    int i = 0;

    float3 albedo      = float3(0.0, 0.0, 0.0);

    float transmittance = 0.0;
    float metallic      = 0.0;
    float3 emissiveColor = float3(0.0, 0.0, 0.0);
    float roughness     = 0.0;

    float3 previousPosition = float3(0.0, 0.0, 0.0);

    float3 diffuseAlbedoDemodulation = float3(0.0, 0.0, 0.0);
    float3 specularFresnelDemodulation = float3(0.0, 0.0, 0.0);

    float3 rayDir = float3(0.0, 0.0, 0.0);

    float3 skyboxContribution = float3(0.0, 0.0, 0.0);

    float roughnessAccumulation = 0.0;

    int reflectedSpecularRayCount = 0;
    int reflectedDiffuseRayCount = 0;
    int refractedDiffuseRayCount  = 0;
    int refractedSpecularRayCount = 0;

    bool grabbedPrimarySurfaceDemodulator = false;

    for (i = 0; i < maxBounces; i++)
    {
        // First ray is directional and perfect mirror from camera eye so specular it is
        bool diffuseRay = false;

        if (i == 0)
        {
            GenerateCameraRay(threadId.xy, indirectPos, rayDir, viewTransform);
            reflectedSpecularRayCount++;
        }
        else
        {
            indirectNormal = normalize(-indirectNormal);

            float2 diffuseOrSpecular = GetRandomSample(threadId.xy, screenSize).xy;
            
            if (diffuseOrSpecular.x < 0.0 /*false*/)
            {
                diffuseRay = true;
                // Punch through ray with zero reflection
                if (transmittance > 0.0 && diffuseOrSpecular.y < 0.0)
                {
                    indirectNormal = normalize(-indirectNormal);
                    rayDir = normalize(GetRandomRayDirection(threadId.xy, indirectNormal, screenSize, 0, indirectPos));
                    refractedDiffuseRayCount++;
                }
                // Opaque materials make a reflected ray
                else
                {
                    rayDir = normalize(GetRandomRayDirection(threadId.xy, indirectNormal, screenSize, 0, indirectPos));
                    reflectedDiffuseRayCount++;
                }

                float3 diffuseWeight = albedo * (1.0 - metallic);
                throughput *= (diffuseWeight);
                // pdf stuff
                //throughput /= 0.5;
            }
            else
            {
                diffuseRay = false;

                // Punch through ray with zero reflection
                if (transmittance > 0.0 && diffuseOrSpecular.y >= 0.0/*true*/)
                {
                    indirectNormal = normalize(-indirectNormal);
                }

                // Specular
                float3x3 basis = orthoNormalBasis(indirectNormal);

                // Sampling of normal distribution function to compute the reflected ray.
                // See the paper "Sampling the GGX Distribution of Visible Normals" by E. Heitz,
                // Journal of Computer Graphics Techniques Vol. 7, No. 4, 2018.
                // http://jcgt.org/published/0007/04/01/paper.pdf

                float3 viewVector = normalize(indirectPos - previousPosition);

                // ImportanceSampleGGX_VNDF doesn't like negative input
                float2 rng = abs(GetRandomSample(threadId.xy, screenSize).xy);

                float3 V = viewVector;
                float3 H = ImportanceSampleGGX_VNDF(rng, roughness, V, basis);

                // Punch through ray with zero reflection
                if (transmittance > 0.0 && diffuseOrSpecular.y >= 0.0/*true*/)
                {
                    rayDir = reflect(V, H);
                    refractedSpecularRayCount++;
                }
                // Opaque materials make a reflected ray
                else
                {
                    // VNDF reflection sampling
                    rayDir = reflect(V, H);
                    reflectedSpecularRayCount++;
                }

                float3 N = indirectNormal;
                
                float NdotV = max(0, -dot(N, viewVector));
                float NdotL = max(0, dot(N, rayDir));
                //float NoH = max(0, dot(N, H));
                float VoH = max(0, -dot(V, H));
                
                float3 F0  = float3(0.04f, 0.04f, 0.04f);
                F0       = lerp(F0, albedo, metallic);
                
                float3 F              = FresnelSchlick(VoH, F0);
                float3 specularWeight = F * Smith_G2_Over_G1_Height_Correlated(roughness, roughness * roughness, NdotL, NdotV);
                throughput *= specularWeight;
                // pdf stuff
                //throughput /= 0.5;
            }
        }
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

        float3 rayDirection = float3(0.0, 0.0, 0.0);
        //if (i == 0)
        //{
            rayDirection = normalize(rayDir);
        //}
        //else
        //{
        //    float3 viewVector = normalize(indirectPos - previousPosition);
        //    rayDirection = normalize(viewVector - (2.0f * dot(viewVector, indirectNormal) * indirectNormal));
        //}

        previousPosition = indirectPos;

        RayDesc ray;
        ray.TMin = MIN_RAY_LENGTH;
        ray.TMax = MAX_RAY_LENGTH;

        //ray.Origin = indirectPos + (rayDirection * 0.001);
        ray.Origin = indirectPos + (indirectNormal * 0.001);

        ray.Direction = rayDirection;

        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE> rayQuery;
        rayQuery.TraceRayInline(rtAS, RAY_FLAG_NONE, ~0, ray);

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

            float3 prevIndirectPos = indirectPos;

            ProcessOpaqueTriangle(rayData, albedo, roughness, metallic, indirectNormal, indirectPos,
                                  transmittance, emissiveColor);

            emissiveColor *= 10.0;

            if (rayQuery.CommittedTriangleFrontFace() == false)
            {
                indirectNormal = -indirectNormal;
            }

            float3 accumulatedLightRadiance = float3(0.0, 0.0, 0.0);
            float3 accumulatedDiffuseRadiance = float3(0.0, 0.0, 0.0);
            float3 accumulatedSpecularRadiance = float3(0.0, 0.0, 0.0);

            for (int lightIndex = 0; lightIndex < numLights; lightIndex++)
            {
                float3 lightRadiance            = float3(0.0, 0.0, 0.0);
                float3 indirectDiffuseRadiance  = float3(0.0, 0.0, 0.0);
                float3 indirectSpecularRadiance = float3(0.0, 0.0, 0.0);

                float3 indirectLighting = GetBRDFLight(albedo, indirectNormal, indirectPos, roughness, metallic, threadId.xy, previousPosition,
                                 lightPositions[lightIndex].xyz, isPointLight[lightIndex], lightRanges[lightIndex/4][lightIndex%4], lightColors[lightIndex].xyz,
                                 indirectDiffuseRadiance, indirectSpecularRadiance, lightRadiance);

                // bug fix for light leaking
                if (length(lightRadiance) > 0.0)
                {
                    accumulatedLightRadiance += lightRadiance;
                    accumulatedDiffuseRadiance += indirectDiffuseRadiance;
                    accumulatedSpecularRadiance += indirectSpecularRadiance;
                }
            }

            // Only reconstruct primary surface hits in composite if the metallic is less than or
            // equal to 0.5 otherwise let denoiser take care of it
            if (/*i == 0 */grabbedPrimarySurfaceDemodulator == false && metallic <= 0.5 && transmittance == 0.0)
            {
                //diffuseAlbedoDemodulation = albedo * (1.0 - metallic);
                //grabbedPrimarySurfaceDemodulator = true;
                //
                //normalUAV[threadId.xy].xyz   = (indirectNormal + 1.0) / 2.0;
                //positionUAV[threadId.xy].xyz = indirectPos;
                //albedoUAV[threadId.xy].xyz   = albedo.xyz;
                //
                //// Denoiser can't handle roughness value of 0.0
                //normalUAV[threadId.xy].w   = max(roughness, 0.05);
                //positionUAV[threadId.xy].w = rayData.instanceIndex;
                //albedoUAV[threadId.xy].w   = metallic;
                //
                //viewZUAV[threadId.xy].x = mul(float4(indirectPos, 1.0), viewTransform).z;
            }

            // Primary surface recording for denoiser
            if (i == 0)
            {
                diffuseAlbedoDemodulation = albedo * (1.0 - metallic);
                // specularFresnelDemodulation = indirectSpecularRadiance;
                grabbedPrimarySurfaceDemodulator = true;
            
                normalUAV[threadId.xy].xyz   = (-indirectNormal + 1.0) / 2.0;
                positionUAV[threadId.xy].xyz = indirectPos;
                albedoUAV[threadId.xy].xyz   = albedo.xyz;
            
                // Denoiser can't handle roughness value of 0.0
                normalUAV[threadId.xy].w   = max(roughness, 0.05);
                positionUAV[threadId.xy].w = rayData.instanceIndex;
                albedoUAV[threadId.xy].w   = metallic;
            
                viewZUAV[threadId.xy].x = mul(float4(indirectPos, 1.0), viewTransform).z;
            }

            roughnessAccumulation += roughness;

            if (diffuseRay == true)
            {
                float3 light = float3(0.0, 0.0, 0.0);

                light = (accumulatedSpecularRadiance + accumulatedDiffuseRadiance) *
                        accumulatedLightRadiance * throughput;

                if (length(emissiveColor) > 0.0)
                {
                    // Account for emissive surfaces
                    light += throughput * emissiveColor;
                }

                if (i == rayBounceIndex || rayBounceIndex == -1)
                {
                    float normDist = REBLUR_FrontEnd_GetNormHitDist(rayQuery.CommittedRayT(),
                                                                    viewZUAV[threadId.xy].x,
                                                                    diffHitDistParams, roughness);

                    nrdDiffuse += REBLUR_FrontEnd_PackRadiance(light, normDist, USE_SANITIZATION);
                }
            }
            else
            {
                float3 light = float3(0.0, 0.0, 0.0);
               
                light = (accumulatedSpecularRadiance + accumulatedDiffuseRadiance) *
                        accumulatedLightRadiance * throughput;

                if (length(emissiveColor) > 0.0)
                {
                    // Account for emissive surfaces
                    light += throughput * emissiveColor;
                }

                if (i == rayBounceIndex || rayBounceIndex == -1)
                {
                    float normDist = REBLUR_FrontEnd_GetNormHitDist(rayQuery.CommittedRayT(),
                                                                    viewZUAV[threadId.xy].x,
                                                                    specHitDistParams, roughness);

                    nrdSpecular += REBLUR_FrontEnd_PackRadiance(light, normDist, USE_SANITIZATION);
                }
            }
        }
        else
        {
            float3 sampleVector = normalize(ray.Direction);
            float4 dayColor     = skyboxTexture.SampleLevel(bilinearWrap, float3(sampleVector.x, sampleVector.y, sampleVector.z), 0);

            if (i == 0)
            {
                float3 light       = dayColor.xyz * throughput;
                skyboxContribution = light;

                diffuseAlbedoDemodulation += skyboxContribution;

                if (i == rayBounceIndex || rayBounceIndex == -1)
                {
                    nrdDiffuse = float4(1.0, 1.0, 1.0, 1.0);
                }
                grabbedPrimarySurfaceDemodulator = true;

                float3 sampleVector = normalize(rayDir);
                float4 dayColor     = skyboxTexture.SampleLevel(bilinearWrap, float3(sampleVector.x, sampleVector.y, sampleVector.z), 0);

                albedoUAV[threadId.xy]   = float4(dayColor.xyz, 0.0);
                normalUAV[threadId.xy]   = float4(0.0, 0.0, 0.0, 1.0);
                positionUAV[threadId.xy] = float4(0.0, 0.0, 0.0, -1.0);

                viewZUAV[threadId.xy].x = 1e5;
            }
            else
            {
                if (diffuseRay == true)
                {
                    float normDist = REBLUR_FrontEnd_GetNormHitDist(1e5, viewZUAV[threadId.xy].x,
                                                                    diffHitDistParams, 1.0);

                    float3 light = dayColor.xyz * throughput;

                    if (i == rayBounceIndex || rayBounceIndex == -1)
                    {
                        nrdDiffuse +=
                            REBLUR_FrontEnd_PackRadiance(light, normDist, USE_SANITIZATION);
                    }
                }
                else
                {
                    float normDist = REBLUR_FrontEnd_GetNormHitDist(1e5, viewZUAV[threadId.xy].x,
                                                                    specHitDistParams, 1.0);
                    float3 light    = dayColor.xyz * throughput;
                    
                    if (i == rayBounceIndex || rayBounceIndex == -1)
                    {
                        nrdSpecular +=
                            REBLUR_FrontEnd_PackRadiance(light, normDist, USE_SANITIZATION);
                    }
                }
            }
            break;
        }
    }

    if (renderMode == 1 || renderMode == 2 || renderMode == 4)
    {
        indirectSpecularLightRaysUAV[threadId.xy] = nrdSpecular;
    }

    if (renderMode == 0 || renderMode == 2 || renderMode == 3)
    {
        indirectLightRaysUAV[threadId.xy] = nrdDiffuse;
    }

    if (grabbedPrimarySurfaceDemodulator == false)
    {
        diffusePrimarySurfaceModulation[threadId.xy] = float4(1.0, 1.0, 1.0, 1.0);
    }
    else
    {
        diffusePrimarySurfaceModulation[threadId.xy] = float4(diffuseAlbedoDemodulation.xyz, 1.0);
    }

    // Write out number of diffuse rays, specular rays and total rays cast
    specularPrimarySurfaceModulation[threadId.xy] =
            float4(reflectedDiffuseRayCount, reflectedSpecularRayCount, refractedDiffuseRayCount, refractedSpecularRayCount);
}