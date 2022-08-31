#include "../../include/structs.hlsl"
#include "../../include/dxr1_1_defines.hlsl"
#define COMPILER_DXC 1
#include "../../../hlsl/include/NRD.hlsli"

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

RWTexture2D<float4> indirectLightRaysUAV : register(u0);
RWTexture2D<float4> indirectSpecularLightRaysUAV : register(u1);
RWTexture2D<float4> diffusePrimarySurfaceModulation : register(u2);
RWTexture2D<float4> specularRefraction : register(u3);
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
    float4 lightRanges[MAX_LIGHTS / 4];
    uint4  isPointLight[MAX_LIGHTS / 4];
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

    int diffuseOrSpecular;
    int reflectionOrRefraction;
    bool enableEmissives;
    bool enableIBL;
    float fov;
}

#include "../../include/sunLightCommon.hlsl"
#include "../../include/utils.hlsl"

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

float GetPDF(float NoL = 1.0) // default can be useful to handle NoL cancelation ( PDF's NoL
                              // cancels throughput's NoL )
{
    float pdf = NoL / PI;

    return max(pdf, 1e-7);
}

float3 GetRay(float2 rnd)
{
    float cosTheta = sqrt(saturate(rnd.y));

    float sinTheta = sqrt(saturate(1.0 - cosTheta * cosTheta));
    float phi      = rnd.x * 2.0 * PI;

    float3 ray;
    ray.x = sinTheta * cos(phi);
    ray.y = sinTheta * sin(phi);
    ray.z = cosTheta;

    return ray;
}

// http://marc-b-reynolds.github.io/quaternions/2016/07/06/Orthonormal.html
float3x3 GetBasis(float3 N)
{
    float sz = sign(N.z);
    float a  = 1.0 / (sz + N.z);
    float ya = N.y * a;
    float b  = N.x * ya;
    float c  = N.x * sz;

    float3 T = float3(c * N.x * a - 1.0, sz * b, c);
    float3 B = float3(b, N.y * ya - sz, N.y);

    // Note: due to the quaternion formulation, the generated frame is rotated by 180 degrees,
    // s.t. if N = (0, 0, 1), then T = (-1, 0, 0) and B = (0, -1, 0).
    return float3x3(T, B, N);
}

[numthreads(8, 8, 1)]
void main(int3 threadId            : SV_DispatchThreadID,
          int3 threadGroupThreadId : SV_GroupThreadID)
{

    float4 diffHitDistParams = float4(3.0f, 0.1f, 10.0f, -25.0f);
    float4 specHitDistParams = float4(3.0f, 0.1f, 10.0f, -25.0f);

    float4 nrdSpecular = REBLUR_FrontEnd_PackRadianceAndHitDist(float3(0.0, 0.0, 0.0), 0.0, 0);
    float4 nrdDiffuse  = REBLUR_FrontEnd_PackRadianceAndHitDist(float3(0.0, 0.0, 0.0), 0.0, 0);
    float4 nrdSpecularRefraction = REBLUR_FrontEnd_PackRadianceAndHitDist(float3(0.0, 0.0, 0.0), 0.0, 0);

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

    bool rayIsBotched = false;

    for (i = 0; i < maxBounces; i++)
    {
        // First ray is directional and perfect mirror from camera eye so mirror ray it is
        bool diffuseRay = false;
        float path       = 0.0; 
        bool  isRefractiveRay = false;

        if (i == 0)
        {
            GenerateCameraRay(threadId.xy, indirectPos, rayDir, viewTransform);
            reflectedSpecularRayCount++;
        }
        else
        {
            float2 stochastic = GetRandomSample(threadId.xy, screenSize).xy;
            stochastic        = (stochastic + 1.0) / 2.0;
            
            float3 viewVector = normalize(indirectPos - previousPosition);
            // Decide whether to sample diffuse or specular BRDF (based on Fresnel term)
            float brdfProbability = getBrdfProbability(albedo, metallic, viewVector, indirectNormal);

            // When calculating the fresnel term we need to make the probability flipped for translucent geometry
            // and shoot rays based on the brdf probability
            // Make probability 0.5 reflection/refraction for less noise
            if (transmittance > 0.0 && stochastic.y < /*brdfProbability*/0.5)
            {
                isRefractiveRay = true;
            }

            // Water simulation
            if (transmittance > 0.0)
            {
                //float sinFrameNumber = asin(sin(float(frameNumber + threadId.x + threadId.y) / 30.0)) * 0.5;
                //float cosFrameNumber = acos(cos(float(frameNumber + threadId.x + threadId.y) / 30.0)) * 0.5;
                //indirectNormal       = normalize(float3(indirectNormal.x * sinFrameNumber,
                //                                        indirectNormal.y,
                //                                        indirectNormal.z * sinFrameNumber));
            }

            // specular rays can only be launched 1-3 bounce index
            if (stochastic.x < brdfProbability && i < 3/* && potentialSpecular*/)
            {
                //throughput /= brdfProbability;
                diffuseRay = false;
            }
            // diffuse ray
            else
            {
                float3 newRayDir = float3(0.0, 0.0, 0.0);
                if ((isRefractiveRay == true && reflectionOrRefraction == 2) ||
                    reflectionOrRefraction == 1)
                {
                    newRayDir = -indirectNormal;
                }
                else if ((isRefractiveRay == false && reflectionOrRefraction == 2) ||
                         reflectionOrRefraction == 0)
                {
                    newRayDir = indirectNormal;
                }

                float NdotL = max(0, dot(indirectNormal, newRayDir));

                //throughput /= (1.0f - brdfProbability);
                diffuseRay = true;
            }

            if (reflectionOrRefraction == 1 || reflectionOrRefraction == 0 ||
                diffuseOrSpecular == 1 || diffuseOrSpecular == 0)
            {
                brdfProbability = 1.0;
            }

            if (reflectionOrRefraction == 1)
            {
                isRefractiveRay = true;
            }
            else if (reflectionOrRefraction == 0)
            {
                isRefractiveRay = false;
            }

            if (diffuseOrSpecular == 1)
            {
                diffuseRay = false;
            }
            else if (diffuseOrSpecular == 0)
            {
                diffuseRay = true;
            }

            indirectNormal = normalize(-indirectNormal);

            if ((diffuseRay == true && diffuseOrSpecular == 2) || diffuseOrSpecular == 0)
            {
                float pdf = 0.0;
                if ((isRefractiveRay == true && reflectionOrRefraction == 2) || reflectionOrRefraction == 1)
                {
                    indirectNormal = normalize(-indirectNormal);
                    rayDir = normalize(GetRandomRayDirection(threadId.xy, indirectNormal, screenSize, 0, indirectPos));
                    refractedDiffuseRayCount++;
                }
                else if ((isRefractiveRay == false && reflectionOrRefraction == 2) || reflectionOrRefraction == 0)
                {
                    rayDir = normalize(GetRandomRayDirection(threadId.xy, indirectNormal, screenSize, 0, indirectPos));
                    reflectedDiffuseRayCount++;
                }

                float3 viewVector = normalize(indirectPos - previousPosition);
                float NdotL = max(0, dot(indirectNormal, rayDir));

                float3 diffuseWeight = albedo * (1.0 - metallic);
                // NdotL is for cosign weighted diffuse distribution
                throughput *= (diffuseWeight * NdotL);
            }
            else if ((diffuseRay == false && diffuseOrSpecular == 2) || diffuseOrSpecular == 1)
            {
                // Specular
                float3x3 basis = orthoNormalBasis(indirectNormal);

                // Sampling of normal distribution function to compute the reflected ray.
                // See the paper "Sampling the GGX Distribution of Visible Normals" by E. Heitz,
                // Journal of Computer Graphics Techniques Vol. 7, No. 4, 2018.
                // http://jcgt.org/published/0007/04/01/paper.pdf

                float3 viewVector = normalize(indirectPos - previousPosition);

                float3 V = viewVector;
                float3 R = reflect(viewVector, indirectNormal);
                // Tests perfect reflections
                //float3 H = normalize(-V + R);
                float3 H = ImportanceSampleGGX_VNDF(stochastic, roughness, V, basis);

                if ((isRefractiveRay == true && reflectionOrRefraction == 2) || reflectionOrRefraction == 1)
                {
                    float3 refractedRay = RefractionRay(indirectNormal, V);
                    rayDir       = refractedRay;
                    refractedSpecularRayCount++;
                }
                else if ((isRefractiveRay == false && reflectionOrRefraction == 2) || reflectionOrRefraction == 0)
                {
                    // VNDF reflection sampling
                    rayDir = reflect(V, H);
                    reflectedSpecularRayCount++;
                }

                if ((isRefractiveRay == true && reflectionOrRefraction == 2) || reflectionOrRefraction == 1)
                {
                    indirectNormal = normalize(-indirectNormal);
                }

                float3 N = indirectNormal;

                float NdotV = max(0, -dot(N, viewVector));
                float NdotL = max(0, dot(N, rayDir));
                float VoH = max(0, -dot(V, H));

                float3 F0 = float3(0.04f, 0.04f, 0.04f);
                F0        = lerp(F0, albedo, metallic);
                float3 F  = FresnelSchlick(VoH, F0);
                
                float3 specularWeight = F * Smith_G2_Over_G1_Height_Correlated(roughness, roughness * roughness, NdotL, NdotV);
                throughput *= specularWeight;
            }
        }

        float3 rayDirection = normalize(rayDir);

        previousPosition = indirectPos;

        RayDesc ray;
        ray.TMin = MIN_RAY_LENGTH;
        ray.TMax = MAX_RAY_LENGTH;

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

            emissiveColor *= enableEmissives ? 20.0 : 0.0;

            float3 accumulatedLightRadiance = float3(0.0, 0.0, 0.0);
            float3 accumulatedDiffuseRadiance = float3(0.0, 0.0, 0.0);
            float3 accumulatedSpecularRadiance = float3(0.0, 0.0, 0.0);

            if (rayQuery.CommittedTriangleFrontFace() == false)
            {
                indirectNormal = -indirectNormal;
            }

            int pointLightCount = 0;
            for (int lightIndex = 0; lightIndex < numLights; lightIndex++)
            {
                float3 lightRadiance            = float3(0.0, 0.0, 0.0);
                float3 indirectDiffuseRadiance  = float3(0.0, 0.0, 0.0);
                float3 indirectSpecularRadiance = float3(0.0, 0.0, 0.0);

                float3 indirectLighting = GetBRDFLight(albedo, indirectNormal, indirectPos, roughness, metallic, threadId.xy, previousPosition,
                                 lightPositions[lightIndex].xyz, isPointLight[lightIndex/4][lightIndex%4], lightRanges[lightIndex/4][lightIndex%4], lightColors[lightIndex].xyz,
                                 indirectDiffuseRadiance, indirectSpecularRadiance, lightRadiance);

                // bug fix for light leaking
                if (length(lightRadiance) > 0.0)
                {
                    accumulatedLightRadiance += lightRadiance;
                    accumulatedDiffuseRadiance += indirectDiffuseRadiance;
                    accumulatedSpecularRadiance += indirectSpecularRadiance;
                }
            }

            // Primary surface recording for denoiser
            if (i == 0 /*roughness >= 0.1 && grabbedPrimarySurfaceDemodulator == false*/)
            {
                diffuseAlbedoDemodulation        = albedo;
                grabbedPrimarySurfaceDemodulator = true;

                normalUAV[threadId.xy].xyz   = (-indirectNormal + 1.0) / 2.0;
                positionUAV[threadId.xy].xyz = indirectPos;
                albedoUAV[threadId.xy].xyz   = albedo;

                // Denoiser can't handle roughness value of 0.0
                normalUAV[threadId.xy].w   = roughness;
                positionUAV[threadId.xy].w = rayData.instanceIndex;
                albedoUAV[threadId.xy].w   = metallic;

                viewZUAV[threadId.xy].x = mul(float4(indirectPos, 1.0), viewTransform).z;
            }

            if (i == 0)
            {
                float3 light = float3(0.0, 0.0, 0.0);

                light = (accumulatedSpecularRadiance + accumulatedDiffuseRadiance) *
                        accumulatedLightRadiance * throughput;

                if (length(emissiveColor) > 0.0)
                {
                    // Account for emissive surfaces
                    light += throughput * emissiveColor;
                }

                float sampleWeight = NRD_GetSampleWeight(light, USE_SANITIZATION);

                if (i == rayBounceIndex || rayBounceIndex == -1)
                {
                    nrdDiffuse +=
                        REBLUR_FrontEnd_PackRadianceAndHitDist(light, 0, USE_SANITIZATION) *
                        sampleWeight;
                }
            }
            else
            {
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

                    float sampleWeight = NRD_GetSampleWeight(light, USE_SANITIZATION);

                    if (i == rayBounceIndex || rayBounceIndex == -1)
                    {

                        path += NRD_GetCorrectedHitDist(rayQuery.CommittedRayT(), 0, 1.0);

                        float normDist = REBLUR_FrontEnd_GetNormHitDist(path,
                                                                        viewZUAV[threadId.xy].x,
                                                                        diffHitDistParams);

                        nrdDiffuse += REBLUR_FrontEnd_PackRadianceAndHitDist(light, normDist, USE_SANITIZATION) * sampleWeight;
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

                    float sampleWeight = NRD_GetSampleWeight(light, USE_SANITIZATION);

                    if (i == rayBounceIndex || rayBounceIndex == -1)
                    {
                        path += NRD_GetCorrectedHitDist(rayQuery.CommittedRayT(), 0, roughness);
                        float normDist = REBLUR_FrontEnd_GetNormHitDist(path,
                                                                        viewZUAV[threadId.xy].x,
                                                                        specHitDistParams);


                        if (isRefractiveRay)
                        {
                            nrdSpecularRefraction += REBLUR_FrontEnd_PackRadianceAndHitDist(light, normDist, USE_SANITIZATION) * sampleWeight;
                        }
                        else
                        {
                            nrdSpecular += REBLUR_FrontEnd_PackRadianceAndHitDist(light, normDist, USE_SANITIZATION) * sampleWeight;
                            
                        }
                        
                    }
                }
            }
        }
        else
        {
        
            float3 sampleVector = normalize(ray.Direction);
            float4 dayColor     = min(skyboxTexture.SampleLevel(bilinearWrap, float3(sampleVector.x, sampleVector.y, sampleVector.z), 0), 10.0);

            //dayColor *= 5.0;

            if (enableIBL == false)
            {
                break;
            }

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


                albedoUAV[threadId.xy]   = float4(dayColor.xyz, 0.0);
                normalUAV[threadId.xy]   = float4(0.0, 0.0, 0.0, 1.0);
                positionUAV[threadId.xy] = float4(0.0, 0.0, 0.0, -1.0);

                viewZUAV[threadId.xy].x = 1e7f;
            }
            else
            {
                if (diffuseRay == true)
                {
                    path += NRD_GetCorrectedHitDist(1e7f, 0, roughness);
                    float  normDist = REBLUR_FrontEnd_GetNormHitDist(path, viewZUAV[threadId.xy].x,
                                                                    diffHitDistParams);
                    float3 light = dayColor.xyz * throughput;

                    if (i == rayBounceIndex || rayBounceIndex == -1)
                    {
                        nrdDiffuse += REBLUR_FrontEnd_PackRadianceAndHitDist(light, normDist, USE_SANITIZATION);
                    }
                  
                }
                else
                {
                    path += NRD_GetCorrectedHitDist(1e7f, 0, roughness);
                    float  normDist = REBLUR_FrontEnd_GetNormHitDist(path, viewZUAV[threadId.xy].x,
                                                                   specHitDistParams);
                    float3 light    = dayColor.xyz * throughput;
                    
                    if (i == rayBounceIndex || rayBounceIndex == -1)
                    {
                        if (isRefractiveRay)
                        {
                            nrdSpecularRefraction += REBLUR_FrontEnd_PackRadianceAndHitDist(light, normDist,
                                                                                  USE_SANITIZATION);
                        }
                        else
                        {
                            nrdSpecular += REBLUR_FrontEnd_PackRadianceAndHitDist(light, normDist,
                                                                                  USE_SANITIZATION);
                        }
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

    specularRefraction[threadId.xy] = nrdSpecularRefraction;
}