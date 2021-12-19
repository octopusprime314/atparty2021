#include "math.hlsl"
#include "randomRays.hlsl"
#include "NRD.hlsl"

float3 GetBRDFSunLight(float3 albedo, float3 normal, float3 hitPosition, float roughness,
                       float metallic, int2 threadId,
                       inout float3 diffuseRadiance,
                       inout float3 specularRadiance,
                       out   float3 lightRadiance,
                       bool recordOcclusion = false)
{
    float3 cameraPosition = float3(inverseView[3][0], inverseView[3][1], inverseView[3][2]);
    float3 eyeVector      = normalize(hitPosition - cameraPosition);

    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0        = lerp(F0, albedo, metallic);

    // reflectance equation
    float3 Lo = float3(0.0f, 0.0f, 0.0f);

    float3 sunLightPos = sunLightPosition.xyz;

    // calculate per-light radiance
    float3 lightDirection = normalize(hitPosition - sunLightPos);
    float3 halfVector     = normalize(eyeVector + lightDirection);
    float  distance       = length(hitPosition - sunLightPos);
    float3 radiance       = sunLightColor.xyz * sunLightRange;

    // Treat the sun as an infinite power light source so no need to apply attenuation
    float  attenuation    = 1.0f / (distance * distance);
    //radiance *= attenuation;

    // Weird sun light leaking is occuring and for now just basically disable the sun light within
    // the cave for pbr testing
    float lightRange = sunLightRange;

    bool occluded = false;

    // Main occlusion test passes so assume completely in shadow
    float occlusion = 0.0;

    // Occlusion shadow ray from the hit position to the target light
    RayDesc ray;

    // Shoot difference of light minus position
    // but also shorten the ray to make sure it doesn't hit the primary ray target
    ray.TMax = distance;

    // For directional shoot from hit position toward the light position
    // Adding noise to ray

    float sunLightRadius = sunLightRange / 400.0f;

    float2 index = threadId.xy;

    ray.Origin                 = hitPosition + (-lightDirection * 0.001);
    float3 penumbraLightVector = -lightDirection;
    penumbraLightVector        = penumbraLightVector + GetRandomRayDirection(threadId, penumbraLightVector, screenSize, 0) * 0.005;
    ray.Direction              = penumbraLightVector;

    // Always edge out ray min value towards the sun to prevent self occlusion
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
        /*float3 prevPosition = hitPosition;

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
        ProcessOpaqueTriangle(rayData, albedo, roughness, metallic, normal, hitPosition,
                                transmittance);*/

        lightRadiance = float3(0.0, 0.0, 0.0);

        diffuseRadiance = diffuse;

        specularRadiance = specular;

        //Lo = float3(0.0, 1.0, 0.0);

    }
    else
    {
        occluded    = false;
        float NdotL = max(dot(normal, lightDirection), 0.0f);

        // 1) Add the diffuse and specular light components and multiply them by the overall incident ray's light energy (radiance)
        // and then also multiply by the alignment of the surface normal with the incoming light ray's direction and shadowed intensity.
        // 2) NdotL basically says that more aligned the normal and light direction is, the more the light
        // will be scattered within the surface (diffuse lighting) rather than get reflected (specular)
        // which will get color from the diffuse surface the reflected light hits after the bounce.
        Lo = (diffuse + specular) * radiance * NdotL/* * (min(1.0, occlusionHistoryUAV[threadId.xy].x + 0.3))*/;
        //Lo = float3(1.0, 0.0, 0.0);

        lightRadiance = radiance * NdotL;

        diffuseRadiance = diffuse;

        specularRadiance = specular;
    }
    return Lo;
}