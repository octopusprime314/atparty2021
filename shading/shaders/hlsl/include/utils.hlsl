
// Basically pragma once to prevent redefinition of shared files
#ifndef __UTILS_HLSL__
#define __UTILS_HLSL__

#include "math.hlsl"

float2 GetTexCoord(float2 barycentrics, uint instanceIndex, uint primitiveIndex)
{
    float2 texCoord[3];

    uint3 index =
        uint3(indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 0),
              indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 1),
              indexBuffer[NonUniformResourceIndex(instanceIndex)].Load((primitiveIndex * 3) + 2));

    texCoord[0] = float2(
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).uv[0]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).uv[1]));

    texCoord[1] = float2(
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).uv[0]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).uv[1]));

    texCoord[2] = float2(
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).uv[0]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).uv[1]));

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
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normal[0]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normal[1]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normal[2]));

    normal[1] = float3(
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normal[0]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normal[1]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normal[2]));

    normal[2] = float3(
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normal[0]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normal[1]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normal[2]));

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
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).uv[0]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).uv[1]));

    texCoord[1] = float2(
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).uv[0]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).uv[1]));

    texCoord[2] = float2(
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).uv[0]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).uv[1]));

    float3 normal[3];

    normal[0] = float3(
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normal[0]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normal[1]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.x).normal[2]));

    normal[1] = float3(
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normal[0]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normal[1]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.y).normal[2]));

    normal[2] = float3(
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normal[0]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normal[1]),
        halfFloatToFloat(vertexBuffer[NonUniformResourceIndex(instanceIndex)].Load(index.z).normal[2]));

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

#endif