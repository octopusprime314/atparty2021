
// Basically pragma once to prevent redefinition of shared files
#ifndef __STRUCTS_HLSL__
#define __STRUCTS_HLSL__

struct CompressedAttribute
{
    float3   vertex;
    uint16_t normal[3];
    uint16_t uv[2];
    uint16_t padding;
};

#define ColorValidBit     1
#define NormalValidBit    2
#define RoughnessValidBit 4
#define MetallicValidBit  8

struct UniformMaterial
{
    float3 baseColor;
    float  metallic;
    float  roughness;
    float  transmittance;
    uint   validBits;
};

struct AlignedHemisphereSample3D
{
    float3 value;
    uint   padding; // Padding to 16B
};

struct RayTraversalData
{
    int      geometryIndex;
    int      primitiveIndex;
    int      instanceIndex;
    float2   barycentrics;
    float    closestRayT;
    float    currentRayT;
    float3   worldRayOrigin;
    float3   worldRayDirection;
    float4x3 objectToWorld;
};

struct Payload
{
    float3 color;
    uint   recursionCount;
};

#define MAX_LIGHTS 1024
#define MAX_RAY_LENGTH 100000.0
#define MIN_RAY_LENGTH 0.1
#define RECURSION_LIMIT 10

#endif