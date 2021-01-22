
// Basically pragma once to prevent redefinition of shared files
#ifndef __MATH_HLSL__
#define __MATH_HLSL__

#define PI 3.14159265f

/***************************************************************/
// 3D value noise
// Ref: https://www.shadertoy.com/view/XsXfRH
// The MIT License
// Copyright © 2017 Inigo Quilez
float hash(float3 p)
{
    p = frac(p * 0.3183099 + .1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// Ref: http://www.reedbeta.com/blog/quick-and-easy-gpu-random-numbers-in-d3d11/
namespace RNG
{
    // Create an initial random number for this thread
    uint SeedThread(uint seed)
    {
        // Thomas Wang hash 
        // Ref: http://www.burtleburtle.net/bob/hash/integer.html
        seed = (seed ^ 61) ^ (seed >> 16);
        seed *= 9;
        seed = seed ^ (seed >> 4);
        seed *= 0x27d4eb2d;
        seed = seed ^ (seed >> 15);
        return seed;
    }

    // Generate a random 32-bit integer
    uint Random(inout uint state)
    {
        // Xorshift algorithm from George Marsaglia's paper.
        state ^= (state << 13);
        state ^= (state >> 17);
        state ^= (state << 5);
        return state;
    }

    // Generate a random float in the range [0.0f, 1.0f)
    float Random01(inout uint state)
    {
        return asfloat(0x3f800000 | Random(state) >> 9) - 1.0;
    }

    // Generate a random float in the range [0.0f, 1.0f]
    float Random01inclusive(inout uint state)
    {
        return Random(state) / float(0xffffffff);
    }


    // Generate a random integer in the range [lower, upper]
    uint Random(inout uint state, uint lower, uint upper)
    {
        return lower + uint(float(upper - lower + 1) * Random01(state));
    }
}

float DistributionGGX(float3 normal, float3 halfVector, float roughness)
{
    float a            = roughness * roughness;
    float aSquared     = a * a;
    float nDotH        = max(dot(normal, halfVector), 0.0f);
    float nDotHSquared = nDotH * nDotH;

    float num   = aSquared;
    float denom = (nDotHSquared * (aSquared - 1.0f) + 1.0f);
    denom       = PI * denom * denom;

    return num / denom;
}

float GeometrySchlickGGX(float nDotV, float roughness)
{
    float r = (roughness + 1.0f);
    float k = (r * r) / 8.0f;

    float num   = nDotV;
    float denom = nDotV * (1.0f - k) + k;

    return num / denom;
}
float GeometrySmith(float3 normal, float3 eyeVector, float3 lightDirection, float roughness)
{
    float NdotV = max(dot(normal, eyeVector), 0.0);
    float NdotL = max(dot(normal, lightDirection), 0.0);
    float ggx2  = GeometrySchlickGGX(NdotV, roughness);
    float ggx1  = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    //Fixes circular corruption when ensuring pow() doesn't get a negative number
    return F0 + (1.0 - F0) * pow(abs(1.0 - cosTheta), 5.0);
    //return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Return random value between 0 and 1
float pseudoRand(in float2 uv)
{
    float2 noise = (frac(sin(dot(uv, float2(12.9898, 78.233) * 2.0)) * 43758.5453));
    return abs(noise.x + noise.y) * 0.5;
}

float2 rand_2_10(in float2 uv)
{
    float noiseX = (frac(sin(dot(uv, float2(12.9898, 78.233) * 2.0)) * 43758.5453));
    float noiseY = sqrt(1 - noiseX * noiseX);
    return float2(noiseX, noiseY);
}

float2 rand_2_0004(in float2 uv)
{
    float noiseX = (frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453));
    float noiseY = (frac(sin(dot(uv, float2(12.9898, 78.233) * 2.0)) * 43758.5453));
    return float2(noiseX, noiseY) * 0.004;
}

#define SMALL_NUM 0.00000001 // anything that avoids division overflow

// intersect3D_RayTriangle(): find the 3D intersection of a ray with a triangle
//    Input:  a ray R, and a triangle T
//    Output: *I = intersection point (when it exists)
//    Return: -1 = triangle is degenerate (a segment or point)
//             0 =  disjoint (no intersect)
//             1 =  intersect in unique point I1
//             2 =  are in the same plane
int IntersectRayTriangle(float3 rayMin, float3 rayMax, float3 point0, float3 point1, float3 point2,
                         out float3 intersection)
{
    float3 u, v, n;    // triangle vectors
    float3 dir, w0, w; // ray vectors
    float  r, a, b;    // params to calc ray-plane intersect

    // get triangle edge vectors and plane normal
    u = point1 - point0;
    v = point2 - point0;
    n = cross(u, v);          // cross product

    dir = rayMax - rayMin; // ray direction vector
    w0  = rayMin - point0;
    a   = -dot(n, w0);
    b   =  dot(n, dir);

    //if (abs(b) < SMALL_NUM)
    //{               
    //    if (a == 0)   // ray is  parallel to triangle plane
    //        return 2; // ray lies in triangle plane
    //    else
    //        return 0; // ray disjoint from plane
    //}

    // get intersect point of ray with triangle plane
    r = a / b;
    //if (r < 0.0)  // ray goes away from triangle
    //    return 0; // => no intersect
    // for a segment, also test if (r > 1.0) => no intersect

    intersection = rayMin + r * dir; // intersect point of ray and plane

    // is I inside T?
    float uu, uv, vv, wu, wv, D;
    uu = dot(u, u);
    uv = dot(u, v);
    vv = dot(v, v);
    w  = intersection - point0;
    wu = dot(w, u);
    wv = dot(w, v);
    D  = uv * uv - uu * vv;

    // get and test parametric coords
    float s, t;
    s = (uv * wv - vv * wu) / D;
    //if (s < 0.0 || s > 1.0) // I is outside T
    //    return 0;
    t = (uv * wu - uu * wv) / D;
    //if (t < 0.0 || (s + t) > 1.0) // I is outside T
    //    return 0;

    return 1; // I is in T
}

// Convert the specified half float number to a single precision float number.
float halfFloatToFloat(uint16_t halfFloat)
{

    /// A static constant for a half float with a value of zero.
    const uint16_t ZERO = 0x0000;

    /// A static constant for a half float with a value of not-a-number.
    const uint16_t NOT_A_NUMBER = 0xFFFF;

    /// A static constant for a half float with a value of positive infinity.
    const uint16_t POSITIVE_INFINITY = 0x7C00;

    /// A static constant for a half float with a value of negative infinity.
    const uint16_t NEGATIVE_INFINITY = 0xFC00;

    /// A mask which isolates the sign of a half float number.
    const uint16_t HALF_FLOAT_SIGN_MASK = 0x8000;

    /// A mask which isolates the exponent of a half float number.
    const uint16_t HALF_FLOAT_EXPONENT_MASK = 0x7C00;

    /// A mask which isolates the significand of a half float number.
    const uint16_t HALF_FLOAT_SIGNIFICAND_MASK = 0x03FF;

    /// A mask which isolates the sign of a single precision float number.
    const uint32_t FLOAT_SIGN_MASK = 0x80000000;

    /// A mask which isolates the exponent of a single precision float number.
    const uint32_t FLOAT_EXPONENT_MASK = 0x7F800000;

    /// A mask which isolates the significand of a single precision float number.
    const uint32_t FLOAT_SIGNIFICAND_MASK = 0x007FFFFF;


    //// Catch special case half floating point values.
    //switch (halfFloat)
    //{
    //    case NOT_A_NUMBER:
    //        return std::numeric_limits<float>::quiet_NaN();
    //    case POSITIVE_INFINITY:
    //        return std::numeric_limits<float>::infinity();
    //    case NEGATIVE_INFINITY:
    //        return std::numeric_limits<float>::infinity();
    //}

    // Start by computing the significand in single precision format.
    uint32_t value = uint32_t(halfFloat & HALF_FLOAT_SIGNIFICAND_MASK) << 13;

    uint32_t exponent = uint32_t(halfFloat & HALF_FLOAT_EXPONENT_MASK) >> 10;

    if (exponent != 0)
    {
        // Add the exponent of the float, converting the offset binary formats of the
        // representations.
        value |= (((exponent - 15 + 127) << 23) & FLOAT_EXPONENT_MASK);
    }

    // Add the sign bit.
    value |= uint32_t(halfFloat & HALF_FLOAT_SIGN_MASK) << 16;

    return asfloat(value);
}

#endif