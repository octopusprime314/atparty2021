#include "math.hlsl"
#include "randomRays.hlsl"

float3 GetBRDFPointLight(float3 albedo,
                         float3 normal,
                         float3 hitPosition,
                         float roughness,
                         float metallic,
                         int2 threadId,
                         bool onlyDiffuse,
                         float offsetDistance)
{
    float3 cameraPosition = float3(inverseView[3][0], inverseView[3][1], inverseView[3][2]);
    float3 eyeVector      = normalize(hitPosition - cameraPosition);

    // FO is initialized as the hardcoded 0.04 generalized reflectance constanct that interpolates
    // between metallic and albedo for fresnel calculations
    const float3 reflectance = float3(0.04f, 0.04f, 0.04f);
    float3       F0          = lerp(reflectance, albedo, metallic);

    // reflectance equation
    float3 Lo = float3(0.0f, 0.0f, 0.0f);

    const uint maxLightsToProcess = 16;
    bool       totalOcclusion      = 0.0;

    for (int i = 0; i < maxLightsToProcess && i < numPointLights; i++)
    {
        // calculate per-light radiance
        float3 lightDirection = normalize(hitPosition - pointLightPositions[i].xyz);
        float  lightRange     = pointLightRanges[i / 4][i % 4];
        float3 halfVector     = normalize(eyeVector + lightDirection);
        // Offset distance is a way to compute diffuse indirect lighting
        float  distance       = length(pointLightPositions[i].xyz - hitPosition) + offsetDistance;
        float  attenuation    = 1.0f / (distance * distance);
        float lightIntensity  = lightRange / 100.0;
        float3 radiance       = pointLightColors[i].xyz * lightIntensity * attenuation;


        // Use the light radiance to guide whether or not a light is contributing to surface lighting
        if (length(radiance) > 0.01)
        {
            // Occlusion shadow ray from the hit position to the target light
            RayDesc ray;

            // Shoot difference of light minus position
            // but also shorten the ray to make sure it doesn't hit the primary ray target
            ray.TMax = distance;

            // Adding noise to ray

            float lightRadius = lightRange / 400.0f;
            
            float2 index = threadId.xy;
            
            float3 pointLightPosition = pointLightPositions[i].xyz;

            ray.Origin                 = hitPosition;
            float3 penumbraLightVector = normalize(pointLightPosition - ray.Origin);
            ray.Direction              = penumbraLightVector;

            //ray.Direction = normalize(penumbraLightVector +
            //                          (GetRandomRayDirection(threadId, penumbraLightVector.xyz, (uint2)screenSize, 0) * 0.025));

            ray.TMin = 0.1;

            // Cull non opaque here occludes the light sources holders from casting shadows
            RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> rayQuery;

            rayQuery.TraceRayInline(rtAS, RAY_FLAG_NONE, ~0, ray);

            rayQuery.Proceed();

            float  occlusion = 0.0;

            if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
            {
                // Main occlusion test passes so assume completely in shadow
                occlusion       = 1.0;
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

            // Cook-Torrance BRDF for specular lighting calculations
            float  NDF = DistributionGGX(normal, halfVector, roughness);
            float  G   = GeometrySmith(normal, eyeVector, lightDirection, roughness);
            float3 F   = FresnelSchlick(max(dot(halfVector, eyeVector), 0.0), F0);

            // Specular component of light that is reflected off the surface
            float3 kS = F;
            // Diffuse component of light is left over from what is not reflected and thus refracted
            float3 kD = float3(1.0f, 1.0f, 1.0f) - kS;
            // Metallic prevents any sort of diffuse (refracted) light from occuring.
            // Metallic of 1.0 signals only specular component of light
            kD *= 1.0 - metallic;

            float3 numerator   = NDF * G * F;
            float  denominator = 4.0 * max(dot(normal, eyeVector), 0.0f) * max(dot(normal, lightDirection), 0.0f);
            float3 specular    = numerator / max(denominator, 0.001f);
            float3 diffuse     = kD * albedo / PI;

            float NdotL = max(dot(normal, lightDirection), 0.0f);

            if (onlyDiffuse)
            {
                specular = float3(0.0, 0.0, 0.0);
            }

            // 1) Add the diffuse and specular light components and multiply them by the overall incident ray's light energy (radiance)
            // and then also multiply by the alignment of the surface normal with the incoming light ray's direction and shadowed intensity.
            // 2) NdotL basically says that more aligned the normal and light direction is, the more the light
            // will be scattered within the surface (diffuse lighting) rather than get reflected (specular)
            // which will get color from the diffuse surface the reflected light hits after the bounce.
            Lo += (diffuse + specular) * radiance * NdotL * (1.0f - occlusion);
        }
    }

    float3 ambient = float3(0.003f, 0.003f, 0.003f) * albedo;
    float3 color   = Lo;

    // Gamma correction
    float colorScale = 1.0f / 2.2f;
    color = color / (color + float3(1.0f, 1.0f, 1.0f));
    color = pow(color, colorScale);

    // pointLightOcclusionUAV[threadId.xy].x = noOcclusion ? 1.0 : 0.0;
    //pointLightOcclusionUAV[threadId.xy].xyz = color;
    pointLightOcclusionUAV[threadId.xy].x = (1.0f - totalOcclusion);

    const float temporalFade = 0.01666666666;
    // const float temporalFade = 0.2;
    //pointLightOcclusionHistoryUAV[threadId.xy].xyz = (temporalFade * pointLightOcclusionUAV[threadId.xy].xyz) +
    //                                               ((1.0 - temporalFade) * pointLightOcclusionHistoryUAV[threadId.xy].xyz);

    pointLightOcclusionHistoryUAV[threadId.xy].x = (temporalFade * (1.0f - totalOcclusion)) +
                                                   ((1.0 - temporalFade) * pointLightOcclusionHistoryUAV[threadId.xy].x);


    return color;
}