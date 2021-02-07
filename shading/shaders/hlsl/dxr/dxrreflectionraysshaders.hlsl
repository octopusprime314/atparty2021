#include "../include/structs.hlsl"
#include "../include/dxr1_0_defines.hlsl"

RaytracingAccelerationStructure             rtAS : register(t0, space0);
Texture2D                                   diffuseTexture[] : register(t1, space1);
StructuredBuffer<CompressedAttribute>       vertexBuffer[] : register(t2, space2);
Buffer<uint>                                indexBuffer[] : register(t3, space3);
Texture2D                                   albedoSRV : register(t4, space0);
Texture2D                                   normalSRV : register(t5, space0);
Texture2D                                   positionSRV : register(t6, space0);
Buffer<uint>                                instanceIndexToMaterialMapping : register(t7, space0);
Buffer<uint>                                instanceIndexToAttributesMapping : register(t8, space0);
Buffer<float>                               instanceNormalMatrixTransforms : register(t9, space0);
StructuredBuffer<UniformMaterial>           uniformMaterials : register(t10, space0);
StructuredBuffer<AlignedHemisphereSample3D> sampleSets : register(t11, space0);

RWTexture2D<float4> reflectionUAV : register(u0);
RWTexture2D<float4> pointLightOcclusionUAV : register(u1);
RWTexture2D<float4> pointLightOcclusionHistoryUAV : register(u2);
RWTexture2D<float4> debugUAV : register(u3);

SamplerState bilinearWrap : register(s0);

cbuffer globalData : register(b0)
{
    float4x4 inverseView;

    float4 pointLightColors[MAX_LIGHTS];
    float4 pointLightRanges[MAX_LIGHTS / 4];
    float4 pointLightPositions[MAX_LIGHTS];

    float2 screenSize;
    int    numPointLights;

    uint seed;
    uint numSamplesPerSet;
    uint numSampleSets;
    uint numPixelsPerDimPerSet;
    uint texturesPerMaterial;
}

#include "../include/pointLightCommon.hlsl"
#include "../include/utils.hlsl"

static float reflectionIndex = 0.5;
static float refractionIndex = 1.0 - reflectionIndex;

[shader("raygeneration")]
void ReflectionRaygen()
{
    float3 normal = normalSRV[DispatchRaysIndex().xy].xyz;

    if (normal.x == 0.0 &&
        normal.y == 0.0 &&
        normal.z == 0.0)
    {
        reflectionUAV[DispatchRaysIndex().xy] = float4(0.0, 0.0, 0.0, 0.0);
    }
    else
    {
        float3 hitPosition = positionSRV[DispatchRaysIndex().xy].xyz;
        float3 albedo      = albedoSRV[DispatchRaysIndex().xy].xyz;

        float transmittance = albedoSRV[DispatchRaysIndex().xy].w;
        float metallic      = positionSRV[DispatchRaysIndex().xy].w;
        float roughness     = normalSRV[DispatchRaysIndex().xy].w;

        // Reconstruct primary ray by taking the camera position and subtracting it from hit
        // position
        float3 cameraPosition  = float3(inverseView[3][0], inverseView[3][1], inverseView[3][2]);
        float3 rayDirection    = normalize(hitPosition - cameraPosition);

        float3 reflectionColor = GetBRDFPointLight(albedo,
                                                   normal,
                                                   hitPosition,
                                                   roughness,
                                                   metallic,
                                                   DispatchRaysIndex().xy,
                                                   false);

        if (roughness < 0.25)
        {
            RayDesc ray;
            ray.Origin    = hitPosition;
            ray.TMin      = MIN_RAY_LENGTH;
            ray.TMax      = MAX_RAY_LENGTH;

            Payload payload;
            payload.color          = float3(0.0, 0.0, 0.0);
            payload.recursionCount = 0;

            // Generate reflection/refraction ray pair with half light energy for each ray
            if (transmittance > 0.0)
            {
                LaunchReflectionRefractionRayPair(payload,
                                                  rayDirection,
                                                  normal,
                                                  ray,
                                                  GLASS_IOR,
                                                  reflectionColor);
            }
            else
            {
                // All light goes into reflection ray
                payload.color.xyz += reflectionColor;
                payload.recursionCount++;

                ray.Direction = rayDirection - (2.0f * dot(rayDirection, normal) * normal);
                TraceRay(rtAS, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);
            }
        }
        else
        {
            reflectionUAV[DispatchRaysIndex().xy] = float4(reflectionColor, 1.0);
        }
    }
}

[shader("anyhit")] void ReflectionAnyHit(inout Payload                               payload,
                                         in    BuiltInTriangleIntersectionAttributes attr)
{
    RayTraversalData rayData;
    rayData.worldRayOrigin    = WorldRayOrigin();
    rayData.currentRayT       = RayTCurrent();
    // Anyhit invokation dictates that previous accepted non opaque triangle is farther away
    // than current
    rayData.closestRayT       = RayTCurrent() + 1.0;
    rayData.worldRayDirection = WorldRayDirection();
    rayData.geometryIndex     = GeometryIndex();
    rayData.primitiveIndex    = PrimitiveIndex();
    rayData.instanceIndex     = InstanceIndex();
    rayData.barycentrics      = attr.barycentrics;
    rayData.objectToWorld     = ObjectToWorld4x3();

    bool isHit = ProcessTransparentTriangle(rayData);
    if (isHit == false)
    {
        IgnoreHit();
    }
}

[shader("closesthit")] void ReflectionClosestHit(inout Payload                               payload,
                                                 in    BuiltInTriangleIntersectionAttributes attr)
{
    RayTraversalData rayData;
    rayData.worldRayOrigin    = WorldRayOrigin();
    rayData.closestRayT       = RayTCurrent();
    rayData.worldRayDirection = WorldRayDirection();
    rayData.geometryIndex     = GeometryIndex();
    rayData.primitiveIndex    = PrimitiveIndex();
    rayData.instanceIndex     = InstanceIndex();
    rayData.barycentrics      = attr.barycentrics;
    rayData.objectToWorld     = ObjectToWorld4x3();
    rayData.uvIsValid         = false;

    float3 albedo;
    float  roughness;
    float  metallic;
    float3 normal;
    float3 hitPosition;
    float  transmittance;

    ProcessOpaqueTriangle(rayData,
                          albedo,
                          roughness,
                          metallic,
                          normal,
                          hitPosition,
                          transmittance);

    debugUAV[DispatchRaysIndex().xy].xyz = hitPosition;

    float3 reflectionColor = GetBRDFPointLight(albedo,
                                               normal,
                                               hitPosition,
                                               roughness,
                                               metallic,
                                               DispatchRaysIndex().xy,
                                               false);

    if (roughness < 0.25 && payload.recursionCount < RECURSION_LIMIT)
    {
        RayDesc ray;
        ray.Origin = hitPosition;
        ray.TMin   = MIN_RAY_LENGTH;
        ray.TMax   = MAX_RAY_LENGTH;

        // Generate reflection/refraction ray pair with half light energy for each ray
        if (transmittance > 0.0 && payload.recursionCount + 1 < RECURSION_LIMIT)
        {
            LaunchReflectionRefractionRayPair(payload,
                                              WorldRayDirection(),
                                              normal,
                                              ray,
                                              GLASS_IOR,
                                              reflectionColor);
        }
        // Opaque materials make a reflected ray
        else
        {
            ray.Direction = WorldRayDirection() - (2.0f * dot(WorldRayDirection(), normal) * normal);

            // All light goes into reflection ray
            payload.color.xyz += reflectionColor;
            payload.recursionCount++;

            TraceRay(rtAS, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);
        }
    }
    else
    {
        reflectionUAV[DispatchRaysIndex().xy] = float4(payload.color.xyz + reflectionColor, 1.0);
    }
}

[shader("miss")]
void ReflectionMiss(inout Payload payload)
{
    reflectionUAV[DispatchRaysIndex().xy] = float4(payload.color.xyz, 1.0);
}

// Shadow occlusion hit group and miss shader
[shader("anyhit")]
void ShadowAnyHit(inout Payload                            payload,
                  in BuiltInTriangleIntersectionAttributes attr)
{
    payload.occlusion = 1.0;
}

[shader("closesthit")]
void ShadowClosestHit(inout Payload                            payload,
                      in BuiltInTriangleIntersectionAttributes attr) 
{
    payload.occlusion = 1.0;
}

[shader("miss")]
void ShadowMiss(inout Payload payload)
{
    payload.occlusion = 0.0;
}