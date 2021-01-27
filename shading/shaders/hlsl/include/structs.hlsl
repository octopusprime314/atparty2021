
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

#define MAX_LIGHTS 1024


#endif