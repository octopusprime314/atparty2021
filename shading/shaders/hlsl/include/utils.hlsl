
// Basically pragma once to prevent redefinition of shared files
#ifndef __UTILS_HLSL__
#define __UTILS_HLSL__

#include "math.hlsl"

#ifdef RAYTRACING_ENABLED

float2 GetTexCoord(float2 barycentrics, uint instanceIndex, uint primitiveIndex)
{
    float2 texCoord[3];

    uint3 index =
        uint3(indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 0),
              indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 1),
              indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 2));

    texCoord[0] = float2(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normalZuvX >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).uvYUnused & 0xFFFF)));

    texCoord[1] = float2(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normalZuvX >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).uvYUnused & 0xFFFF)));

    texCoord[2] = float2(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normalZuvX >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).uvYUnused & 0xFFFF)));

    float2 interpolatedUV = (texCoord[0] + barycentrics.x * (texCoord[1] - texCoord[0]) +
                             barycentrics.y * (texCoord[2] - texCoord[0]));

    return interpolatedUV;
}

float3 GetNormalCoord(float2 barycentrics, uint instanceIndex, uint primitiveIndex)
{
    float3 normal[3];

    uint3 index =
        uint3(indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 0),
              indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 1),
              indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 2));

    normal[0] = float3(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normalXY & 0xFFFF)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normalXY >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normalZuvX & 0xFFFF)));

    normal[1] = float3(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normalXY & 0xFFFF)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normalXY >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normalZuvX & 0xFFFF)));

    normal[2] = float3(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normalXY & 0xFFFF)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normalXY >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normalZuvX & 0xFFFF)));

    return normalize(normal[0] + barycentrics.x * (normal[1] - normal[0]) +
                     barycentrics.y * (normal[2] - normal[0]));
}


// Compute barycentric coordinates (u, v, w) for
// point p with respect to triangle (a, b, c)
void ComputeBarycentric(float3 p, float3 a, float3 b, float3 c, out float u, out float v,
                        out float w)
{
    float3 v0    = b - a;
    float3 v1    = c - a;
    float3 v2    = p - a;
    float  d00   = dot(v0, v0);
    float  d01   = dot(v0, v1);
    float  d11   = dot(v1, v1);
    float  d20   = dot(v2, v0);
    float  d21   = dot(v2, v1);
    float  denom = d00 * d11 - d01 * d01;
    u            = (d11 * d20 - d01 * d21) / denom;
    v            = (d00 * d21 - d01 * d20) / denom;
    w            = 1.0f - u - v;
}

float3x3 GetTBN(float2 barycentrics, uint instanceIndex, uint primitiveIndex)
{
    float3 pos[3];

    uint3 index =
        uint3(indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 0),
              indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 1),
              indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 2));

    pos[0] = vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).vertex;
    pos[1] = vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).vertex;
    pos[2] = vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).vertex;

    float2 texCoord[3];

    texCoord[0] = float2(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normalZuvX >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).uvYUnused & 0xFFFF)));

    texCoord[1] = float2(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normalZuvX >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).uvYUnused & 0xFFFF)));

    texCoord[2] = float2(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normalZuvX >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).uvYUnused & 0xFFFF)));

    float3 normal[3];

    normal[0] = float3(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normalXY & 0xFFFF)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normalXY >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normalZuvX & 0xFFFF)));

    normal[1] = float3(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normalXY & 0xFFFF)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normalXY >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normalZuvX & 0xFFFF)));

    normal[2] = float3(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normalXY & 0xFFFF)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normalXY >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normalZuvX & 0xFFFF)));

    float3 surfaceNormal = normal[0] + barycentrics.x * (normal[1] - normal[0]) +
                           barycentrics.y * (normal[2] - normal[0]);

    float3 edge[2];

    edge[0] = pos[1] - pos[0];
    edge[1] = pos[2] - pos[0];

    float2 deltaUV[2];
    deltaUV[0] = texCoord[1] - texCoord[0];
    deltaUV[1] = texCoord[2] - texCoord[0];

    float f = 1.0f / (deltaUV[0].x * deltaUV[1].y - deltaUV[1].x * deltaUV[0].y);

    // Return identity matrix if bad uv/normals
    if (isnan(f))
    {
        float3   t      = float3(f, f, f);
        float3   b      = float3(f, f, f);

        float3 BA = pos[1] - pos[0];
        float3 CA = pos[2] - pos[0];
        surfaceNormal = cross(BA, CA);

        float3   n      = normalize(surfaceNormal);
        float3x3 tbnMat = float3x3(t, b, n);

        return tbnMat;
    }

    float3 tangent;
    tangent.x = f * (deltaUV[1].y * edge[0].x - deltaUV[0].y * edge[1].x);
    tangent.y = f * (deltaUV[1].y * edge[0].y - deltaUV[0].y * edge[1].y);
    tangent.z = f * (deltaUV[1].y * edge[0].z - deltaUV[0].y * edge[1].z);

    float3 bitangent;
    bitangent.x = f * (-deltaUV[1].x * edge[0].x + deltaUV[0].x * edge[1].x);
    bitangent.y = f * (-deltaUV[1].x * edge[0].y + deltaUV[0].x * edge[1].y);
    bitangent.z = f * (-deltaUV[1].x * edge[0].z + deltaUV[0].x * edge[1].z);

    float3   t      = normalize(tangent);
    float3   b      = normalize(bitangent);
    float3   n      = normalize(surfaceNormal);
    float3x3 tbnMat = float3x3(t, b, n);

    return tbnMat;
}

float TriUVInfoFromRayCone(float3 pos0, float3 pos1, float3 pos2,
                           float2 uv0,  float2 uv1,  float2 uv2,
                           float3 rayDir, float rayConeWidth, float3 surfaceNormal)
{
    // Only 8 mip levels are generated so treat the textures as if they are all 256x256
    // which gives us 0.5 * log base 2(256 x 256) = 8.0;
    float  width      = 256;
    float  height     = 256;
    float2 vUV10      = uv1 - uv0;
    float2 vUV20      = uv2 - uv0;
    float  fTriUVArea = abs(vUV10.x * vUV20.y - vUV20.x * vUV10.y);
    float3 vEdge10    = pos1 - pos0;
    float3 vEdge20    = pos2 - pos0;
    float3 vFaceNrm   = cross(vEdge10, vEdge20);

    float fTriLODOffset = 0.5f * log2(fTriUVArea / length(vFaceNrm));
    fTriLODOffset      += 0.5 * log2(width * height);
    fTriLODOffset      -= log2(abs(dot(rayDir, surfaceNormal)));
    return fTriLODOffset;
}

//float ComputeMipLevel(float2 barycentrics, uint instanceIndex, uint primitiveIndex,
//                      float3 rayOrigin, float3 rayDest, float3 hitPosition, uint2 threadId, float4x4 objectToWorld,
//                      float3x3 blasNormalMatrixTransform)
//{
//    uint3 index =
//        uint3(indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 0),
//              indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 1),
//              indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 2));
//
//    float3 pos[3];
//
//    pos[0] = mul(float4(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).pos, 1.0),
//            objectToWorld).xyz;
//    pos[1] = mul(float4(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).pos, 1.0),
//            objectToWorld).xyz;
//    pos[2] = mul(float4(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).pos, 1.0),
//            objectToWorld).xyz;
//
//    float2 texCoord[3];
//
//    texCoord[0] = vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).uv;
//    texCoord[1] = vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).uv;
//    texCoord[2] = vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).uv;
//
//    float4 position = float4(pos[0] + barycentrics.x * (pos[1] - pos[0]) + barycentrics.y * (pos[2] - pos[0]), 1.0);
//
//    float3 p0;
//    int intersectionType = IntersectRayTriangle(rayOrigin, rayDest, pos[0], pos[1], pos[2], p0);
//
//    float u0, v0, w0;
//    ComputeBarycentric(p0, pos[0], pos[1], pos[2], u0, v0, w0);
//
//    float2 uv0 = (texCoord[0] + u0 * (texCoord[1] - texCoord[0]) + v0 * (texCoord[2] - texCoord[0]));
//
//    float2 uvCompare = (texCoord[0] + barycentrics.x * (texCoord[1] - texCoord[0]) +
//                        barycentrics.y * (texCoord[2] - texCoord[0]));
//
//    rayOrigin += float3(0, 0.1, 0);
//    rayDest   += float3(0, 0.1, 0);
//    float3 p1;
//    intersectionType = IntersectRayTriangle(rayOrigin, rayDest, pos[0], pos[1], pos[2], p1);
//
//    float u1, v1, w1;
//    ComputeBarycentric(p1, pos[0], pos[1], pos[2], u1, v1, w1);
//
//    float2 uv1 = (texCoord[0] + u1 * (texCoord[1] - texCoord[0]) + v1 * (texCoord[2] - texCoord[0]));
//
//    rayOrigin += float3(0.1, -0.1, 0);
//    rayDest   += float3(0.1, -0.1, 0);
//    float3 p2;
//    intersectionType = IntersectRayTriangle(rayOrigin, rayDest, pos[0], pos[1], pos[2], p2);
//
//    float u2, v2, w2;
//    ComputeBarycentric(p2, pos[0], pos[1], pos[2], u2, v2, w2);
//    float2 uv2 = (texCoord[0] + u2 * (texCoord[1] - texCoord[0]) + v2 * (texCoord[2] - texCoord[0]));
//
//    //debug0UAV[threadId.xy].xyz = p0.xyz;
//    //debug1UAV[threadId.xy].xyz = p1.xyz;
//    //debug2UAV[threadId.xy].xyz = p2.xyz;
//
//    float3 normal[3];
//
//    normal[0] = mul(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normal,
//            blasNormalMatrixTransform);
//    normal[1] = mul(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normal,
//            blasNormalMatrixTransform);
//    normal[2] = mul(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normal,
//            blasNormalMatrixTransform);
//
//    float3 surfaceNormal = normal[0] + barycentrics.x * (normal[1] - normal[0]) +
//                           barycentrics.y * (normal[2] - normal[0]);
//
//    surfaceNormal = normalize(surfaceNormal);
//
//    return TriUVInfoFromRayCone(p0, p1, p2, uv0, uv1, uv2, normalize(rayDest-rayOrigin), 0.1, surfaceNormal);
//}


// Camera ray with projective rays eminating from a single point being the eye location
void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction, in float4x4 viewTransform)
{
    // Projection ray
    float3 cameraPosition = float3(inverseView[3][0], inverseView[3][1], inverseView[3][2]);
    origin                = cameraPosition;

    float imageAspectRatio = screenSize.x / screenSize.y; // assuming width > height
    float Px = (2.0 * ((index.x + 0.5) / screenSize.x) - 1.0) * tan(fov / 2.0 * PI / 180.0) *
               imageAspectRatio;
    float Py = (1.0 - 2.0 * ((index.y + 0.5) / screenSize.y)) * tan(fov / 2.0 * PI / 180.0);

    float4 rayDirection   = float4(Px, Py, 1.0, 1.0);
    float4 rayOrigin      = float4(0, 0, 0, 1.0);
    float3 rayOriginWorld = mul(viewTransform, rayOrigin).xyz;
    float3 rayPWorld      = mul(viewTransform, rayDirection).xyz;
    direction             = normalize(rayPWorld - rayOriginWorld);
}

void ProcessTransparentTriangleShadow(inout RayQuery<RAY_FLAG_NONE> rayQuery)
{

    // if (rayQuery.CandidateTriangleRayT() < rayQuery.CommittedRayT())
    //{
    //    float3 hitPosition =
    //        rayQuery.WorldRayOrigin() +
    //        (rayQuery.CandidateTriangleRayT() * rayQuery.WorldRayDirection());
    //
    //    int geometryIndex  = rayQuery.CandidateGeometryIndex();
    //    int primitiveIndex = rayQuery.CandidatePrimitiveIndex();
    //    int instanceIndex  = rayQuery.CandidateInstanceIndex();
    //
    //    int materialIndex = instanceIndexToMaterialMapping[instanceIndex] +
    //                        (geometryIndex * texturesPerMaterial);
    //
    //    int attributeIndex =
    //        instanceIndexToAttributesMapping[instanceIndex] + geometryIndex;
    //
    //    float2 uvCoord = GetTexCoord(rayQuery.CandidateTriangleBarycentrics(),
    //                                 attributeIndex, primitiveIndex);
    //
    //    // This is a trasmittive material dielectric like glass or water
    //    if (instanceUniformMaterialMapping[attributeIndex].transmittance > 0.0)
    //    {
    //        rayQuery.CommitNonOpaqueTriangleHit();
    //    }
    //    // Alpha transparency texture that is treated as alpha cutoff for leafs and
    //    // foliage, etc.
    //    else if (instanceUniformMaterialMapping[attributeIndex].transmittance == 0.0)
    //    {
    //        float alpha = diffuseTexture[NonUniformResourceIndex(materialIndex)]
    //                          .SampleLevel(bilinearWrap, uvCoord, 0)
    //                          .w;
    //
    //        if (alpha >= 0.9)
    //        {
    //            rayQuery.CommitNonOpaqueTriangleHit();
    //        }
    //    }
    //}

    // Don't worry about non opaque shadow processing for now
    rayQuery.CommitNonOpaqueTriangleHit();
}

bool ProcessTransparentTriangle(in RayTraversalData rayData)
{
    if (rayData.currentRayT < rayData.closestRayT)
    {
        float3 hitPosition = rayData.worldRayOrigin + (rayData.currentRayT * rayData.worldRayDirection);

        int geometryIndex  = rayData.geometryIndex;
        int primitiveIndex = rayData.primitiveIndex;
        int instanceIndex  = rayData.instanceIndex;
        float2 barycentrics  = rayData.instanceIndex;

        int materialIndex = instanceIndexToMaterialMapping[instanceIndex] + (geometryIndex * texturesPerMaterial);
        int attributeIndex = instanceIndexToAttributesMapping[instanceIndex] + geometryIndex;

        float2 uvCoord = GetTexCoord(barycentrics, attributeIndex, primitiveIndex);

        // This is a trasmittive material dielectric like glass or water
        if (uniformMaterials[attributeIndex].transmittance > 0.0)
        {
            return true;
        }
        // Alpha transparency texture that is treated as alpha cutoff for leafs and foliage, etc.
        else if (uniformMaterials[attributeIndex].transmittance == 0.0)
        {
            float alpha = diffuseTexture[NonUniformResourceIndex(materialIndex)]
                              .SampleLevel(bilinearWrap, uvCoord, 0)
                              .w;

            if (alpha >= 0.9)
            {
                return true;
            }
        }
    }
    return false;
}

float3 refract(const float3 normal, const float3 incident, float n)
{
    const float  cosI  = -dot(normal, incident);
    const float  sinT2 = n * n * (1.0 - cosI * cosI);
    if (sinT2 > 1.0)
        return float3(0.0, 0.0, 0.0); // Total internal reflection....what
    const float cosT = sqrt(1.0 - sinT2);
    return n * incident + (n * cosI - cosT) * normal;
}

float3 RefractionRay(float3 normal, float3 incidentRayDirection)
{
    float cosIncident   = clamp(-1.0, 1.0, dot(incidentRayDirection, normal));
    float  etaIncident   = 1.0;
    float  etaRefraction = WATER_IOR;
    float3 n   = normal;
    if (cosIncident < 0.0)
    {
        cosIncident = -cosIncident;
    }
    else
    {
        etaIncident   = WATER_IOR;
        etaRefraction = 1.0;
        n    = -normal;
    }
    float eta = etaIncident / etaRefraction;
    float k   = 1.0 - eta * eta * (1.0 - cosIncident * cosIncident);
    return (k < 0.0) ? 0.0 : eta * incidentRayDirection + (eta * cosIncident - sqrt(k)) * n;
}

#ifdef COMPILE_DXR_1_0_ONLY

void LaunchReflectionRefractionRayPair(inout Payload incidentPayload,
                                       in    float3  incidentRay,
                                       in    float3  normal,
                                       in    RayDesc ray,
                                       in    float   indexOfRefraction,
                                       in    float3  incidentLight)
{
    float reflectionCoeff = (pow((1.0 - indexOfRefraction), 2.0) / pow((1.0 + indexOfRefraction), 2.0));
    float refractionCoeff = 1.0 - reflectionCoeff;

    Payload payloadRefraction;
    payloadRefraction.recursionCount = incidentPayload.recursionCount + 1;
    payloadRefraction.color          = incidentLight * refractionCoeff;

    ray.Direction = RefractionRay(normal, incidentRay);
    TraceRay(rtAS, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payloadRefraction);

    Payload payloadReflection;
    payloadReflection.recursionCount = incidentPayload.recursionCount + 1;
    payloadReflection.color          = incidentLight * reflectionCoeff;

    ray.Direction = incidentRay - (2.0f * dot(incidentRay, normal) * normal);
    TraceRay(rtAS, RAY_FLAG_NONE, ~0, 0, 0, 0, ray, payloadReflection);
}

#endif

#endif

float3 GetNormalFromVertices(uint instanceIndex, uint primitiveIndex)
{
    float3 pos[3];

    uint3 index =
        uint3(indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 0),
              indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 1),
              indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 2));

    pos[0] = vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).vertex;
    pos[1] = vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).vertex;
    pos[2] = vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).vertex;

    float3 ac = pos[0] - pos[2];
    float3 bc = pos[1] - pos[2];

    return cross(ac, bc);
}

float3 GetVertex(uint instanceIndex, uint vertexId)
{
    float3 vertex = vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(vertexId).vertex;
    return vertex;
}

float3 GetNormal(uint instanceIndex, uint vertexId)
{
    float3 normal =
       float3(halfFloatToFloat(
                  min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(vertexId).normalXY & 0xFFFF)),
              halfFloatToFloat(
                  min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(vertexId).normalXY >> 16)),
              halfFloatToFloat(
                  min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(vertexId).normalZuvX & 0xFFFF)));

    return normal;
}

float2 GetUV(uint instanceIndex, uint vertexId)
{
    float2 uv = float2(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(vertexId).normalZuvX >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(vertexId).uvYUnused & 0xFFFF)));
    return uv;
}


float3x3 GetTBN(float3 surfaceNormal, uint instanceIndex, uint primitiveIndex)
{
    float3 pos[3];

    uint3 index =
        uint3(indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 0),
              indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 1),
              indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 2));

    pos[0] = vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).vertex;
    pos[1] = vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).vertex;
    pos[2] = vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).vertex;

    float2 texCoord[3];

    texCoord[0] = float2(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normalZuvX >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).uvYUnused & 0xFFFF)));

    texCoord[1] = float2(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normalZuvX >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).uvYUnused & 0xFFFF)));

    texCoord[2] = float2(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normalZuvX >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).uvYUnused & 0xFFFF)));

    float3 normal[3];

    normal[0] = float3(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normalXY & 0xFFFF)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normalXY >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normalZuvX & 0xFFFF)));

    normal[1] = float3(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normalXY & 0xFFFF)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normalXY >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normalZuvX & 0xFFFF)));

    normal[2] = float3(
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normalXY & 0xFFFF)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normalXY >> 16)),
        halfFloatToFloat(min16uint(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normalZuvX & 0xFFFF)));

    float3 edge[2];

    edge[0] = pos[1] - pos[0];
    edge[1] = pos[2] - pos[0];

    float2 deltaUV[2];
    deltaUV[0] = texCoord[1] - texCoord[0];
    deltaUV[1] = texCoord[2] - texCoord[0];

    float f = 1.0f / (deltaUV[0].x * deltaUV[1].y - deltaUV[1].x * deltaUV[0].y);

    // Return identity matrix if bad uv/normals
    if (isnan(f))
    {
        float3   t      = float3(f, f, f);
        float3   b      = float3(f, f, f);

        float3 BA = pos[1] - pos[0];
        float3 CA = pos[2] - pos[0];
        surfaceNormal = cross(BA, CA);

        float3   n      = normalize(surfaceNormal);
        float3x3 tbnMat = float3x3(t, b, n);

        return tbnMat;
    }

    float3 tangent;
    tangent.x = f * (deltaUV[1].y * edge[0].x - deltaUV[0].y * edge[1].x);
    tangent.y = f * (deltaUV[1].y * edge[0].y - deltaUV[0].y * edge[1].y);
    tangent.z = f * (deltaUV[1].y * edge[0].z - deltaUV[0].y * edge[1].z);

    float3 bitangent;
    bitangent.x = f * (-deltaUV[1].x * edge[0].x + deltaUV[0].x * edge[1].x);
    bitangent.y = f * (-deltaUV[1].x * edge[0].y + deltaUV[0].x * edge[1].y);
    bitangent.z = f * (-deltaUV[1].x * edge[0].z + deltaUV[0].x * edge[1].z);

    float3   t      = normalize(tangent);
    float3   b      = normalize(bitangent);
    float3   n      = normalize(surfaceNormal);
    float3x3 tbnMat = float3x3(t, b, n);

    return tbnMat;
}

float4 GetAtmosphericDiffuseLighting(float height)
{
    return float4(0.0, 0.0, 0.44, 0.0);
    //return float4(0.0, 0.3, 0.44, 0.0);
    //return float4(137.0 / 256.0, 207.0 / 256.0, 240.0 / 256.0, 0.0) / 4.0;
    //return float4(137.0 / 256.0, 207.0 / 256.0, 240.0 / 256.0, 0.0);
    //return float4(137.0 / 256.0, 207.0 / 256.0, 240.0 / 256.0, 0.0) * (height * 3.0) + 
    //    float4(0.0, 0.0, 0.44, 0.0) * (1.0 - height * 3.0);
}

void ProcessOpaqueTriangle(in  RayTraversalData        rayData,
                           out float3                  albedo,
                           out float                   roughness,
                           out float                   metallic,
                           out float3                  normal,
                           out float3                  hitPosition,
                           out float                   transmittance,
                           out float3                  emissiveColor)
{
    hitPosition = rayData.worldRayOrigin + (rayData.closestRayT * rayData.worldRayDirection);

    int    geometryIndex   = rayData.geometryIndex;
    int    primitiveIndex  = rayData.primitiveIndex;
    int    instanceIndex   = rayData.instanceIndex;
    float2 barycentrics    = rayData.barycentrics;

    int materialIndex = instanceIndexToMaterialMapping[instanceIndex] + (geometryIndex * texturesPerMaterial);
    int attributeIndex = instanceIndexToAttributesMapping[instanceIndex] + geometryIndex;

    float2 uvCoord = float2(0.0, 0.0);

#ifdef RAYTRACING_ENABLED
    if (rayData.uvIsValid == false)
    {
        uvCoord = GetTexCoord(barycentrics, attributeIndex, primitiveIndex);
    }
    else
    {
        uvCoord = rayData.uv;
    }
#else
    uvCoord = rayData.uv;
#endif

    float4x3 cachedTransform        = rayData.objectToWorld;
    float4x4 objectToWorldTransform = {float4(cachedTransform[0].xyz, 0.0),
                                        float4(cachedTransform[1].xyz, 0.0),
                                        float4(cachedTransform[2].xyz, 0.0),
                                        float4(cachedTransform[3].xyz, 1.0)};

    int offset = instanceIndex * 9;
  
    float3x3 instanceNormalMatrixTransform = {
        float3(instanceNormalMatrixTransforms[NonUniformResourceIndex(offset)],
                instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 3)],
                instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 6)]),
        float3(instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 1)],
                instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 4)],
                instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 7)]),
        float3(instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 2)],
                instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 5)],
                instanceNormalMatrixTransforms[NonUniformResourceIndex(offset + 8)])};

    float mipLevel = 0;

    albedo = float3(0.0, 0.0, 0.0);
    if (uniformMaterials[attributeIndex].validBits & ColorValidBit)
    {
        albedo = uniformMaterials[attributeIndex].baseColor;
    }
    else
    {
        albedo = diffuseTexture[NonUniformResourceIndex(materialIndex)].SampleLevel(bilinearWrap, uvCoord, mipLevel).xyz;
    }

    roughness = 0.0;
    if (uniformMaterials[attributeIndex].validBits & RoughnessValidBit)
    {
        roughness = uniformMaterials[attributeIndex].roughness;
    }
    else
    {
        roughness = diffuseTexture[NonUniformResourceIndex(materialIndex + 2)]
                        .SampleLevel(bilinearWrap, uvCoord, mipLevel)
                        .y * uniformMaterials[attributeIndex].roughness;
    }

    // denoiser breaks if roughness is 0 - DO NOT CHANGE!!!!
    roughness = max(roughness, 0.05);

    metallic = 0.0;
    if (uniformMaterials[attributeIndex].validBits & MetallicValidBit)
    {
        metallic = uniformMaterials[attributeIndex].metallic;
    }
    else
    {
        metallic = diffuseTexture[NonUniformResourceIndex(materialIndex + 2)]
                        .SampleLevel(bilinearWrap, uvCoord, mipLevel)
                        .z *  uniformMaterials[attributeIndex].metallic; 
    }

    normal = float3(0.0, 0.0, 0.0);

    if (uniformMaterials[attributeIndex].validBits & NormalValidBit)
    {
#ifdef RAYTRACING_ENABLED
        normal = mul(-GetNormalCoord(barycentrics, attributeIndex, primitiveIndex), instanceNormalMatrixTransform);
        normal = normalize(normal);
#else
        normal = rayData.normal;
#endif
    }
    else
    {
        float3 normalMap = diffuseTexture[NonUniformResourceIndex(materialIndex + 1)]
                                .SampleLevel(bilinearWrap, uvCoord, mipLevel)
                                .xyz;

       
#ifdef RAYTRACING_ENABLED
        // Compute the normal from loading the triangle vertices
        float3x3 tbnMat = GetTBN(barycentrics, attributeIndex, primitiveIndex);
#else
        normal          = rayData.normal;
        float3x3 tbnMat = GetTBN(normal, attributeIndex, primitiveIndex);
#endif
        // If there is a failure in getting the TBN matrix then use the computed normal without normal mappings
        if (any(isnan(tbnMat[0])))
        {
            normal = -normalize(mul(tbnMat[2], instanceNormalMatrixTransform));
        }
        else
        {
            float3x3 tbnMatNormalTransform = mul(tbnMat, instanceNormalMatrixTransform);

            if (length(normalMap) == 0.0)
            {
                normal = -normalize(mul(tbnMat[2], instanceNormalMatrixTransform));
            }
            else
            {
                // Converts from [0,1] space to [-1,1] space
                normalMap = normalMap * 2.0f - 1.0f;
                normal = -normalize(mul(normalMap, tbnMatNormalTransform));
            }
        }
    }

    transmittance = uniformMaterials[attributeIndex].transmittance;

    if (uniformMaterials[attributeIndex].validBits & EmissiveValidBit)
    {
        emissiveColor = uniformMaterials[attributeIndex].emissiveColor;
    }
    else
    {
        emissiveColor = diffuseTexture[NonUniformResourceIndex(materialIndex + 3)]
                       .SampleLevel(bilinearWrap, uvCoord, mipLevel).xyz * uniformMaterials[attributeIndex].emissiveColor;
    }
}

float luminance(float3 rgb) { return dot(rgb, float3(0.2126f, 0.7152f, 0.0722f)); }

// Calculates probability of selecting BRDF (specular or diffuse) using the approximate Fresnel term
float getBrdfProbability(float3 albedo, float metallic, float3 V, float3 shadingNormal)
{

    // Evaluate Fresnel term using the shading normal
    // Note: we use the shading normal instead of the microfacet normal (half-vector) for Fresnel
    // term here. That's suboptimal for rough surfaces at grazing angles, but half-vector is yet
    // unknown at this point
    float3 F0        = float3(0.04f, 0.04f, 0.04f);
    F0               = lerp(F0, albedo, metallic);
    float specularF0         = luminance(F0);

    float diffuseReflectance = luminance(albedo * (1.0 - metallic));
    float Fresnel = saturate(luminance(FresnelSchlick(max(0.0f, dot(V, shadingNormal)), specularF0)));

    // Approximate relative contribution of BRDFs using the Fresnel term
    float specular = Fresnel;
    float diffuse =
        diffuseReflectance *
        (1.0f - Fresnel); //< If diffuse term is weighted by Fresnel, apply it here as well

    // Return probability of selecting specular BRDF over diffuse BRDF
    float p = (specular / max(0.0001f, (specular + diffuse)));

    // Clamp probability to avoid undersampling of less prominent BRDF
    return clamp(p, 0.1f, 0.9f);
}
#endif