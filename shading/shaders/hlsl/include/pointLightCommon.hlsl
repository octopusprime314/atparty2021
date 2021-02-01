#include "math.hlsl"

#ifdef COMPILE_DXR_1_1_ONLY
#include "randomRays.hlsl"
#include "utils.hlsl"
#endif

#ifdef COMPILE_DXR_1_0_ONLY
#include "randomRays.hlsl"
#include "utils.hlsl"
#endif

float3 GetBRDFPointLight(float3 albedo,
                         float3 normal,
                         float3 hitPosition,
                         float  roughness,
                         float  metallic,
                         int2   threadId,
                         bool   onlyDiffuse)
{
    float3 cameraPosition = float3(inverseView[3][0], inverseView[3][1], inverseView[3][2]);
    float3 eyeVector      = normalize(hitPosition - cameraPosition);

    // FO is initialized as the hardcoded 0.04 generalized reflectance constanct that interpolates
    // between metallic and albedo for fresnel calculations
    const float3 reflectanceConstant = float3(0.04f, 0.04f, 0.04f);
    float3       F0                  = lerp(reflectanceConstant, albedo, metallic);

    // reflectance equation
    float3 reflectance = float3(0.0f, 0.0f, 0.0f);

    const uint maxLightsToProcess = 16;
    bool       totalOcclusion     = 0.0;

#ifdef COMPILE_DXR_1_0_ONLY

    Payload payload;
    payload.recursionCount = 0;
    payload.occlusion      = 0.0;

#endif

    for (int i = 0; i < maxLightsToProcess && i < numPointLights; i++)
    {
        // calculate per-light radiance
        float3 lightDirection = normalize(hitPosition - pointLightPositions[i].xyz);
        float  lightRange     = pointLightRanges[i / 4][i % 4];
        float3 halfVector     = normalize(eyeVector + lightDirection);
        // Offset distance is a way to compute diffuse indirect lighting
        float  distance       = length(pointLightPositions[i].xyz - hitPosition);
        float  attenuation    = 1.0f / (distance * distance);
        float  lightIntensity = lightRange;
        float3 radiance       = pointLightColors[i].xyz * lightIntensity * attenuation;

        // Use the light radiance to guide whether or not a light is contributing to surface lighting
        if (length(radiance) > 0.01)
        {
            // Occlusion shadow ray from the hit position to the target light
            RayDesc ray;

            // Shoot difference of light minus position
            // but also shorten the ray to make sure it doesn't hit the primary ray target
            ray.TMax = distance;

            float3 pointLightPosition = pointLightPositions[i].xyz;

            ray.Origin                 = hitPosition;
            float3 penumbraLightVector = normalize(pointLightPosition - ray.Origin);
            ray.Direction              = penumbraLightVector;

            //ray.Direction = normalize(penumbraLightVector +
            //                          (GetRandomRayDirection(threadId, penumbraLightVector.xyz, (uint2)screenSize, 0) * 0.025));

            ray.TMin = MIN_RAY_LENGTH;

            float occlusion = 0.0;

#ifdef COMPILE_DXR_1_1_ONLY
            // Cull non opaque here occludes the light sources holders from casting shadows
            RayQuery<RAY_FLAG_NONE> rayQuery;

            rayQuery.TraceRayInline(rtAS, RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES, ~0, ray);

            while (rayQuery.Proceed())
            {
                ProcessTransparentTriangleShadow(rayQuery);
            }

            if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
            {
                // Main occlusion test passes so assume completely in shadow
                occlusion      = 1.0;
                totalOcclusion = occlusion;

                //float t = 1.0 - (rayQuery.CommittedRayT() / distance);
                //if (t >= 1.0)
                //{
                //    occlusion = 0.0;
                //}
                //else
                //{
                //    float lambda                = 10.0f;
                //    float occlusionCoef         = exp(-lambda * t * t);
                //    occlusion                   = 1.0 - occlusionCoef;
                //}
            }
#endif
#ifdef COMPILE_DXR_1_0_ONLY

            TraceRay(rtAS,
                     RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES,
                     ~0, 1, 0, 1, ray, payload);

            payload.recursionCount++;
            occlusion = payload.occlusion;
            totalOcclusion = occlusion;
#endif

            float3 lighting = GetLighting(normal, halfVector, roughness, eyeVector, lightDirection, metallic, occlusion, albedo, radiance, F0);
            reflectance += lighting;
        }
    }

    float3 ambient = float3(0.003f, 0.003f, 0.003f) * albedo;
    float3 color   = reflectance;

    // Gamma correction
    float colorScale = 1.0f / 2.2f;
    color = color / (color + float3(1.0f, 1.0f, 1.0f));
    color = pow(color, colorScale);

#ifdef COMPILE_DXR_1_1_ONLY
    // pointLightOcclusionUAV[threadId.xy].x = noOcclusion ? 1.0 : 0.0;
    //pointLightOcclusionUAV[threadId.xy].xyz = color;
    pointLightOcclusionUAV[threadId.xy].x = (1.0f - totalOcclusion);

    const float temporalFade = 0.01666666666;
    // const float temporalFade = 0.2;
    //pointLightOcclusionHistoryUAV[threadId.xy].xyz = (temporalFade * pointLightOcclusionUAV[threadId.xy].xyz) +
    //                                               ((1.0 - temporalFade) * pointLightOcclusionHistoryUAV[threadId.xy].xyz);

    pointLightOcclusionHistoryUAV[threadId.xy].x = (temporalFade * (1.0f - totalOcclusion)) +
                                                   ((1.0 - temporalFade) * pointLightOcclusionHistoryUAV[threadId.xy].x);
#endif

    return color;
}