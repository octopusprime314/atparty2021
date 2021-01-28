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

SamplerState bilinearWrap : register(s0);

cbuffer globalData : register(b0)
{
    float4x4 viewTransform;
    float4x4 inverseView;
    float2   screenSize;
    uint     texturesPerMaterial;

    float4 pointLightColors[MAX_LIGHTS];
    float4 pointLightRanges[MAX_LIGHTS / 4];
    float4 pointLightPositions[MAX_LIGHTS];
    int    numPointLights;

    uint seed;
    uint numSamplesPerSet;
    uint numSampleSets;
    uint numPixelsPerDimPerSet;
}

#include "../include/utils.hlsl"

[shader("raygeneration")]
void PrimaryRaygen()
{
    float3 rayDir;
    float3 origin;

    GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir, viewTransform);

    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = rayDir;

    // Set TMin to a non-zero small val(ue to avoid aliasing issues due to floating - point errors.
    // TMin should be kept small to prevent missing geometry at close contact areas.
    ray.TMin           = MIN_RAY_LENGTH;
    ray.TMax           = MAX_RAY_LENGTH;
    Payload payload    = {float4(0, 0, 0, 0)};

    TraceRay(rtAS, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payload);
}

[shader("anyhit")] void PrimaryAnyHit(inout Payload                               payload,
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

[shader("closesthit")] void PrimaryClosestHit(inout Payload                               payload,
                                              in    BuiltInTriangleIntersectionAttributes attr)
{
    float3 albedo;
    float  roughness;
    float  metallic;
    float3 normal;
    float3 hitPosition;
    float  transmittance;

    RayTraversalData rayData;
    rayData.worldRayOrigin    = WorldRayOrigin();
    rayData.closestRayT       = RayTCurrent();
    rayData.worldRayDirection = WorldRayDirection();
    rayData.geometryIndex     = GeometryIndex();
    rayData.primitiveIndex    = PrimitiveIndex();
    rayData.instanceIndex     = InstanceIndex();
    rayData.barycentrics      = attr.barycentrics;
    rayData.objectToWorld     = ObjectToWorld4x3();

    RayDesc ray;
    ray.TMin      = MIN_RAY_LENGTH;
    ray.TMax      = MAX_RAY_LENGTH;

    ProcessOpaqueTriangle(rayData,
                          ray,
                          albedo,
                          roughness,
                          metallic,
                          normal,
                          hitPosition,
                          transmittance);

    normalUAV  [DispatchRaysIndex().xy].xyz = normal;
    positionUAV[DispatchRaysIndex().xy].xyz = hitPosition;
    albedoUAV  [DispatchRaysIndex().xy].xyz = albedo.xyz;

    normalUAV  [DispatchRaysIndex().xy].w = roughness;
    positionUAV[DispatchRaysIndex().xy].w = metallic;
    albedoUAV  [DispatchRaysIndex().xy].w = transmittance;
}

[shader("miss")]
void PrimaryMiss(inout Payload payload)
{
    albedoUAV[DispatchRaysIndex().xy]   = float4(0.0, 0.0, 0.0, 0.0);
    normalUAV[DispatchRaysIndex().xy]   = float4(0.0, 0.0, 0.0, 0.0);
    positionUAV[DispatchRaysIndex().xy] = float4(0.0, 0.0, 0.0, 0.0);
}
