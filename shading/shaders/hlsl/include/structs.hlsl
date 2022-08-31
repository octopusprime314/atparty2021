
// Basically pragma once to prevent redefinition of shared files
#ifndef __STRUCTS_HLSL__
#define __STRUCTS_HLSL__

struct CompressedAttribute
{
    float3    vertex;
    uint      normalXY;
    uint      normalZuvX;
    uint      uvYUnused;
};

#define ColorValidBit     1
#define NormalValidBit    2
#define RoughnessValidBit 4
#define MetallicValidBit  8
#define EmissiveValidBit  16

struct UniformMaterial
{
    float3 baseColor;
    float  metallic;
    float  roughness;
    float  transmittance;
    float3 emissiveColor;
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

    float2   uv;
    bool     uvIsValid;
    float3   normal;
};

struct Payload
{
    float3 color;
    float  occlusion;
    uint   recursionCount;
};

#define MAX_LIGHTS 512
#define MAX_RAY_LENGTH 100000.0
#define MIN_RAY_LENGTH 0.0
#define RECURSION_LIMIT 31

#define WATER_IOR   1.1
#define GLASS_IOR   1.5
#define DIAMOND_IOR 1.8

// Use glass reflection coefficients from this website
//https://glassproperties.com/glasses/

#endif