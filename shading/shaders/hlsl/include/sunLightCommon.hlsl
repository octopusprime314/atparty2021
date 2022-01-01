#include "math.hlsl"
#include "randomRays.hlsl"
#include "NRD.hlsl"

float3 GetBRDFLight(float3 albedo, float3 normal, float3 hitPosition, float roughness,
                       float metallic, int2 threadId, float3 prevPosition,
                       float3 lightPos, uint pointLight, float lightIntensity, float3 lightColor,
                       inout float3 diffuseRadiance,
                       inout float3 specularRadiance,
                       out   float3 lightRadiance)
{
    float3 eyeVector      = normalize(hitPosition - prevPosition);

    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0        = lerp(F0, albedo, metallic);

    // reflectance equation
    float3 Lo = float3(0.0f, 0.0f, 0.0f);

    // calculate per-light radiance
    float3 lightDirection = normalize(hitPosition - lightPos);
    float3 halfVector     = normalize(eyeVector + lightDirection);
    float  distance       = length(hitPosition - lightPos);
    float3 radiance       = lightColor * lightIntensity;

    if (pointLight == 1)
    {
        // Attenuate point lights and not directional lights like the sun
        float attenuation = 1.0f / (distance * distance);
        radiance *= attenuation;
    }

    bool occluded = false;

    // Main occlusion test passes so assume completely in shadow
    float occlusion = 0.0;

    // Occlusion shadow ray from the hit position to the target light
    RayDesc ray;

    // Shoot difference of light minus position
    // but also shorten the ray to make sure it doesn't hit the primary ray target
    ray.TMax = distance;

    float2 index = threadId.xy;

    ray.Origin                 = hitPosition + (-lightDirection * 0.001);
    float3 penumbraLightVector = -lightDirection;
    penumbraLightVector        = penumbraLightVector + GetRandomRayDirection(threadId, penumbraLightVector, screenSize, 0, hitPosition) * 0.005;
    ray.Direction              = penumbraLightVector;

    ray.TMin = MIN_RAY_LENGTH;

    // Cull non opaque here occludes the light sources holders from casting shadows
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
                RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES | 
                RAY_FLAG_FORCE_OPAQUE> rayQuery;

    rayQuery.TraceRayInline(rtAS, RAY_FLAG_NONE, ~0, ray);

    rayQuery.Proceed();

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
    float  denominator =  4.0 * max(dot(normal, eyeVector), 0.0f) * max(dot(normal, lightDirection), 0.0f);
    float3 specular    = numerator / max(denominator, 0.001f);
    float3 diffuse     = kD * albedo * (1.0f/PI)/*(0.75f)*/;

    if (rayQuery.CommittedStatus() == COMMITTED_TRIANGLE_HIT)
    {
        occluded = true;

        lightRadiance = float3(0.0, 0.0, 0.0);

        diffuseRadiance = diffuse;

        specularRadiance = specular;

    }
    else
    {
        occluded    = false;
        float NdotL = max(dot(normal, lightDirection), 0.0f);

        lightRadiance = radiance * NdotL;

        diffuseRadiance = diffuse;

        specularRadiance = specular;
    }
    return Lo;
}