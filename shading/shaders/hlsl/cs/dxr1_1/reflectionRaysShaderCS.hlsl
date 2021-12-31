#include "../../include/structs.hlsl"
#include "../../include/dxr1_1_defines.hlsl"

RaytracingAccelerationStructure             rtAS                             : register(t0, space0);
Texture2D                                   diffuseTexture[]                 : register(t1, space1);
StructuredBuffer<CompressedAttribute>       vertexBuffer[]                   : register(t2, space2);
Buffer<uint>                                indexBuffer[]                    : register(t3, space3);
//Texture2D                                   albedoSRV                        : register(t4, space0);
//Texture2D                                   normalSRV                        : register(t5, space0);
//Texture2D                                   positionSRV                      : register(t6, space0);
Buffer<uint>                                instanceIndexToMaterialMapping   : register(t4, space0);
Buffer<uint>                                instanceIndexToAttributesMapping : register(t5, space0);
Buffer<float>                               instanceNormalMatrixTransforms   : register(t6, space0);
StructuredBuffer<UniformMaterial>           uniformMaterials                 : register(t7, space0);
StructuredBuffer<AlignedHemisphereSample3D> sampleSets                       : register(t8, space0);
Texture2D                                   viewZSRV                         : register(t9, space0);
TextureCube                                 skyboxTexture                    : register(t10, space0);

RWTexture2D<float4> reflectionUAV : register(u0);
//RWTexture2D<float2> occlusionUAV : register(u1);
//RWTexture2D<float4> occlusionHistoryUAV : register(u2);
RWTexture2D<float4> indirectLightRaysUAV : register(u1);
RWTexture2D<float4> indirectLightRaysHistoryBufferUAV : register(u2);
RWTexture2D<float4> indirectSpecularLightRaysUAV : register(u3);
RWTexture2D<float4> indirectSpecularLightRaysHistoryBufferUAV : register(u4);

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
    uint frameNumber;
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

    uint   bounceIndex = 0;

    //class enum RenderVariant
    //{
    //    ONLY_DIFFUSE = 0,
    //    ONLY_SPECULAR = 1,
    //    BOTH_DIFFUSE_AND_SPECULAR = 2
    //};

    int renderVariant = 2;

    float3 diffuseRadiance = float3(0.0, 0.0, 0.0);
    float3 specularRadiance = float3(0.0, 0.0, 0.0);
    float3 indirectRadiance = float3(0.0, 0.0, 0.0);

    float3 indirectDiffuse   = float3(0.0, 0.0, 0.0);
    float3 indirectSpecular = float3(0.0, 0.0, 0.0);
    float3 lightRadiance;

    float3 indirectLighting = float3(0.0, 0.0, 0.0);
    float3 indirectPos         = float3(0.0, 0.0, 0.0);
    float3 indirectNormal      = float3(0.0, 0.0, 0.0);
    float  indirectHitDistance = 0.0;

    float3 indirectSpecularLightEnergy = float3(1.0, 1.0, 1.0);

    float3 indirectDiffuseLightEnergy = float3(1.0, 1.0, 1.0);

    float indirectHitDistanceSpecular = 0.0;

    int i = 0;

    float3 albedo      = float3(0.0, 0.0, 0.0);

    float transmittance = 0.0;
    float metallic      = 0.0;
    float roughness     = 0.0;

    float3 cachedPrimaryHitPosition = float3(0.0, 0.0, 0.0);
    float3 cachedPrimaryNormal      = float3(0.0, 0.0, 0.0);

    float3 cachedSpecularLightEnergy = float3(1.0, 1.0, 1.0);
    float3 cachedDiffuseLightEnergy  = float3(1.0, 1.0, 1.0);

    float3 previousSpecularPosition = float3(1.0, 1.0, 1.0);
    float3 previousSpecularNormal = float3(1.0, 1.0, 1.0);
    float  previousRoughness        = 0.0;
    float3 cameraPosition = float3(inverseView[3][0], inverseView[3][1], inverseView[3][2]);

    float3 previousPosition = float3(0.0, 0.0, 0.0);

    for (i = 0; i < 3; i++)
    {
        indirectNormal = normalize(-indirectNormal);
        bool diffuseRay = true;

        float3 rayDir = float3(0.0, 0.0, 0.0);
        if (i == 0)
        {
            GenerateCameraRay(threadId.xy, indirectPos, rayDir, viewTransform);
        }
        else
        {
            float2 rng3 = GetRandomSample(threadId.xy, screenSize, indirectPos).xy;

            if (rng3.x < 0.0 || rng3.y < 0.0)
            {
                diffuseRay = true;
                rayDir = GetRandomRayDirection(threadId.xy, indirectNormal, screenSize, 0, indirectPos);
            }
            else
            {
                diffuseRay = false;
                
                // Specular
                float3x3 basis = orthoNormalBasis(indirectNormal);
                
                // Sampling of normal distribution function to compute the reflected ray.
                // See the paper "Sampling the GGX Distribution of Visible Normals" by E. Heitz,
                // Journal of Computer Graphics Techniques Vol. 7, No. 4, 2018.
                // http://jcgt.org/published/0007/04/01/paper.pdf
                
                float3 lightVector = normalize(indirectPos - previousPosition);
                //cameraPosition   = indirectPos;
                
                float2 rng3 = GetRandomSample(threadId.xy, screenSize, indirectPos).xy;
                
                // roughness = max(roughness, 0.25);
                
                float3 N = indirectNormal;
                float3 V = lightVector;
                float3 H = ImportanceSampleGGX_VNDF(rng3, roughness, V, basis);
                
                //// Punch through ray with zero reflection
                // if (transmittance > 0.0)
                //{
                //    rayDir = rayDirection;
                //}
                //// Opaque materials make a reflected ray
                // else
                //{
                rayDir = reflect(V, H);
                //}
                
                float NoV = max(0, -dot(indirectNormal, lightVector));
                float NoL = max(0, dot(N, rayDir));
                float NoH = max(0, dot(N, H));
                float VoH = max(0, -dot(V, H));
            }
        }

        previousPosition = indirectPos;

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

        float3 specularRayDirection = float3(0.0, 0.0, 0.0);
        //if (i == 0)
        //{
            specularRayDirection = normalize(rayDir);
        //}
        /*else
        {
            specularRayDirection = normalize(rayDir - (2.0f * dot(rayDir, indirectNormal) * indirectNormal));
        }*/

        RayDesc raySpecular;
        raySpecular.TMin = MIN_RAY_LENGTH;
        raySpecular.TMax = MAX_RAY_LENGTH;

        //if (transmittance > 0.0)
        //{
        //    raySpecular.Origin = indirectPos + (indirectNormal * -0.001);
        //}
        //else
        //{
            //raySpecular.Origin = indirectPos + (indirectNormal * 0.001);
            raySpecular.Origin = indirectPos + (specularRayDirection * 0.001);
        //}
        raySpecular.Direction = specularRayDirection;

        RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | RAY_FLAG_FORCE_OPAQUE> rayQuerySpecular;
        rayQuerySpecular.TraceRayInline(rtAS, RAY_FLAG_NONE, ~0, raySpecular);

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

            if (rayQuerySpecular.CommittedTriangleFrontFace() == false)
            {
                indirectNormal = -indirectNormal;
            }

            float3 currIndirectRadiance = float3(0.0, 0.0, 0.0);
            float3  currIndirectSpecularRadiance = float3(0.0, 0.0, 0.0);
            float3 currLightRadiance = float3(0.0, 0.0, 0.0);
            float3 indirectLighting = GetBRDFSunLight(albedo, indirectNormal, indirectPos, roughness, metallic, threadId.xy, previousPosition, currIndirectRadiance, currIndirectSpecularRadiance,
                                currLightRadiance);

            indirectHitDistanceSpecular += rayQuerySpecular.CommittedRayT();

            /*if (i % 2 == 1)
            {
                indirectDiffuse += currIndirectRadiance * currLightRadiance * indirectDiffuseLightEnergy;
                indirectDiffuseLightEnergy *= currIndirectRadiance;
            }
            else
            {*/
                //indirectDiffuse += currIndirectRadiance * currLightRadiance * indirectDiffuseLightEnergy;
                //indirectDiffuseLightEnergy *= currIndirectRadiance;
                //
            if (i == 0)
            {
                indirectDiffuse += (currIndirectSpecularRadiance + currIndirectRadiance) *
                                    currLightRadiance * indirectSpecularLightEnergy;
            }
            else
            {
                if (diffuseRay == true)
                {
                    indirectDiffuse += (currIndirectSpecularRadiance + currIndirectRadiance) *
                                        currLightRadiance * indirectDiffuseLightEnergy;
                }
                else
                {
                    indirectSpecular += (currIndirectSpecularRadiance + currIndirectRadiance) *
                                        currLightRadiance * indirectSpecularLightEnergy;
                }
            }

            // Specular
            float2   rng3         = GetRandomSample(threadId.xy, screenSize, indirectPos).xy;
            float3x3 basis = orthoNormalBasis(indirectNormal);
            float3 N = indirectNormal;
            float3 V = raySpecular.Direction;
            float3 H = ImportanceSampleGGX_VNDF(rng3, roughness, V, basis);
            float3 reflectedRay = reflect(V, H);

            //float3 cameraPosition = float3(inverseView[3][0], inverseView[3][1], inverseView[3][2]);
            //float3 lightVector = normalize(raySpecular.Direction);
            float3 lightVector = normalize(indirectPos - previousPosition);

            float3 F0 = float3(0.04f, 0.04f, 0.04f);
            F0        = lerp(F0, albedo, metallic);

            // calculate per-light radiance
            float3 halfVector = normalize(lightVector + reflectedRay);

                // Cook-Torrance BRDF for specular lighting calculations
            float  NDF = DistributionGGX(indirectNormal, halfVector, roughness);
            float  G   = GeometrySmith(indirectNormal, lightVector, halfVector, roughness);
            float3 F   = FresnelSchlick(max(dot(halfVector, lightVector), 0.0), F0);

            // Specular component of light that is reflected off the surface
            float3 kS = F;
            // Diffuse component of light is left over from what is not reflected and thus
            // refracted
            float3 kD = float3(1.0f, 1.0f, 1.0f) - kS;
            // Metallic prevents any sort of diffuse (refracted) light from occuring.
            // Metallic of 1.0 signals only specular component of light
            kD *= 1.0 - metallic;

            float3 numerator   = NDF * G * F;
            float  denominator = 4.0 * max(dot(indirectNormal, lightVector), 0.0f) *
                                max(dot(indirectNormal, halfVector), 0.0f);
            float3 specular = numerator / max(denominator, 0.001f);

            float3 specularWeight = G * F;
            indirectSpecularLightEnergy *= specularWeight;

            float3 diffuseWeight = kD * albedo;
            indirectDiffuseLightEnergy *= diffuseWeight;
        }
        else
        {
            float3 sampleVector = normalize(raySpecular.Direction);
            float4 dayColor     = skyboxTexture.SampleLevel(bilinearWrap, float3(sampleVector.x, sampleVector.y, sampleVector.z), 0);

            if (i == 0)
            {
                indirectDiffuse += dayColor.xyz * /*sunLightRange **/ indirectDiffuseLightEnergy;
            }
            else
            {
                if (diffuseRay == true)
                {
                    indirectDiffuse += dayColor.xyz * /*sunLightRange **/ indirectDiffuseLightEnergy;
                }
                else
                {
                    indirectSpecular += dayColor.xyz * /*sunLightRange **/ indirectSpecularLightEnergy;
                }
            }
            break;
        }
    }

    float4 hitDistanceParams = float4(3.0f, 0.1f, 10.0f, -25.0f);
    float  normHitDist;

    if (renderVariant == 1 || renderVariant == 2)
    {
        normHitDist = REBLUR_FrontEnd_GetNormHitDist(indirectHitDistanceSpecular, viewZSRV[threadId.xy].x, hitDistanceParams);

        indirectSpecularLightRaysUAV[threadId.xy] = REBLUR_FrontEnd_PackRadiance(indirectSpecular, normHitDist);
    }

    normHitDist = REBLUR_FrontEnd_GetNormHitDist(indirectHitDistance, viewZSRV[threadId.xy].x, hitDistanceParams);

    if (renderVariant == 0 || renderVariant == 2)
    {
        indirectLightRaysUAV[threadId.xy] += REBLUR_FrontEnd_PackRadiance(indirectDiffuse.rgb, normHitDist);
    }

    float3 hdrColor = (((REBLUR_BackEnd_UnpackRadiance(indirectSpecularLightRaysHistoryBufferUAV[threadId.xy]))) +
                    (REBLUR_BackEnd_UnpackRadiance(indirectLightRaysHistoryBufferUAV[threadId.xy]))).xyz;

    const float gamma = 2.2;
    //const float exposure = 0.01;
    const float exposure = 1.0;
    // reinhard tone mapping
    //float3 mapped = hdrColor / (hdrColor + float3(1.0, 1.0, 1.0));
    float3 mapped = float3(1.0, 1.0, 1.0) - exp(-hdrColor * exposure);
    // gamma correction
    mapped = pow(mapped, float3(1.0, 1.0, 1.0) / float3(gamma, gamma, gamma));

    reflectionUAV[threadId.xy] = float4(mapped, 1.0);
}