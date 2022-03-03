
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

float square(float x) { return x * x; }

float3 ImportanceSampleGGX_VNDF(float2 u, float roughness, float3 V, float3x3 basis)
{
    float alpha = square(roughness);

    float3 Ve = -float3(dot(V, basis[0]), dot(V, basis[2]), dot(V, basis[1]));

    float3 Vh = normalize(float3(alpha * Ve.x, alpha * Ve.y, Ve.z));
    
    float lensq = square(Vh.x) + square(Vh.y);
    float3 T1 = lensq > 0.0 ? float3(-Vh.y, Vh.x, 0.0) * rsqrt(lensq) : float3(1.0, 0.0, 0.0);
    float3 T2 = cross(Vh, T1);

    float r = sqrt(u.x /** global_ubo.pt_ndf_trim*/);
    float phi = 2.0 * PI * u.y;
    float t1 = r * cos(phi);
    float t2 = r * sin(phi);
    float s = 0.5 * (1.0 + Vh.z);
    t2 = (1.0 - s) * sqrt(1.0 - square(t1)) + s * t2;

    float3 Nh = t1 * T1 + t2 * T2 + sqrt(max(0.0, 1.0 - square(t1) - square(t2))) * Vh;

    // Tangent space H
    float3 Ne = float3(alpha * Nh.x, max(0.0, Nh.z), alpha * Nh.y);

    // World space H
    return normalize(mul(Ne, basis));
}

float3x3 orthoNormalBasis(float3 normal)
{
    float3x3 ret;
    ret[1] = normal;
    if (normal.z < -0.999805696f)
    {
        ret[0] = float3(0.0f, -1.0f, 0.0f);
        ret[2] = float3(-1.0f, 0.0f, 0.0f);
    }
    else
    {
        float a = 1.0f / (1.0f + normal.z);
        float b = -normal.x * normal.y * a;
        ret[0]          = float3(1.0f - normal.x * normal.x * a, b, -normal.x);
        ret[2]          = float3(b, 1.0f - normal.y * normal.y * a, -normal.y);
    }
    return ret;
}

float DistributionGGX(float3 normal, float3 halfVector, float roughness)
{
    float a            = roughness;
    float aSquared     = a * a;
    float nDotH        = max(dot(normal, halfVector), 0.0f);
    float nDotHSquared = nDotH * nDotH;

    float num   = max(aSquared, 0.01);

    //if (aSquared <= 0.01)
    //{
    //    num = 0.0;
    //}
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

    return ggx2 * ggx1;
}

// Smith G1 term (masking function) further optimized for GGX distribution (by substituting G_a into
// G1_GGX)
float Smith_G1_GGX(float alpha, float NdotS, float alphaSquared, float NdotSSquared)
{
    return 2.0f /
           (sqrt(((alphaSquared * (1.0f - NdotSSquared)) + NdotSSquared) / NdotSSquared) + 1.0f);
}

// A fraction G2/G1 where G2 is height correlated can be expressed using only G1 terms
// Source: "Implementing a Simple Anisotropic Rough Diffuse Material with Stochastic Evaluation",
// Appendix A by Heitz & Dupuy
float Smith_G2_Over_G1_Height_Correlated(float alpha, float alphaSquared, float NdotL, float NdotV)
{
    float G1V = Smith_G1_GGX(alpha, NdotV, alphaSquared, NdotV * NdotV);
    float G1L = Smith_G1_GGX(alpha, NdotL, alphaSquared, NdotL * NdotL);
    return G1L / (G1V + G1L - G1V * G1L);
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    //Fixes circular corruption when ensuring pow() doesn't get a negative number
    return F0 + (1.0 - F0) * pow(1.0 - abs(cosTheta), 5.0);
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

float random(float2 p)
{
    // We need irrationals for pseudo randomness.
    // Most (all?) known transcendental numbers will (generally) work.
    const float2 r = float2(23.1406926327792690, // e^pi (Gelfond's constant)
                        2.6651441426902251); // 2^sqrt(2) (Gelfond–Schneider constant)
    return frac(cos(fmod(123456789., 1e-7 + 256. * dot(p, r))));
}

float3 randomDir(float2 p, float3 normal, out float pdf)
{
    float2 u = float2(random(p), random(float2(1.0, 1.0) - p));

    float  a = 1.0 - 2.0 * u[0];
    float  b = sqrt(1.0 - a * a);
    float  phi = 2 * PI * u[1];
    float  x   = normal[0] + b * cos(phi);
    float  y   = normal[1] + b * sin(phi);
    float z = normal[2] + a;
    pdf        = a / PI;

    return normalize(float3(x, y, z));
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
float halfFloatToFloat(min16uint halfFloat)
{

   /// A static constant for a half float with a value of zero.
    static const min16uint ZERO = 0x0000;

    /// A static constant for a half float with a value of not-a-number.
    static const min16uint NOT_A_NUMBER = 0xFFFF;

    /// A static constant for a half float with a value of positive infinity.
    static const min16uint POSITIVE_INFINITY = 0x7C00;

    /// A static constant for a half float with a value of negative infinity.
    static const min16uint NEGATIVE_INFINITY = 0xFC00;

    /// A mask which isolates the sign of a half float number.
    static const min16uint HALF_FLOAT_SIGN_MASK = 0x8000;

    /// A mask which isolates the exponent of a half float number.
    static const min16uint HALF_FLOAT_EXPONENT_MASK = 0x7C00;

    /// A mask which isolates the significand of a half float number.
    static const min16uint HALF_FLOAT_SIGNIFICAND_MASK = 0x03FF;

    /// A mask which isolates the sign of a single precision float number.
    static const uint32_t FLOAT_SIGN_MASK = 0x80000000;

    /// A mask which isolates the exponent of a single precision float number.
    static const uint32_t FLOAT_EXPONENT_MASK = 0x7F800000;

    /// A mask which isolates the significand of a single precision float number.
    static const uint32_t FLOAT_SIGNIFICAND_MASK = 0x007FFFFF;



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

/// Convert the specified single precision float number to a half precision float number.
static min16uint floatToHalfFloat(float floatValue)
{

     /// A static constant for a half float with a value of zero.
    const min16uint ZERO = 0x0000;

    /// A static constant for a half float with a value of not-a-number.
    const min16uint NOT_A_NUMBER = 0xFFFF;

    /// A static constant for a half float with a value of positive infinity.
    const min16uint POSITIVE_INFINITY = 0x7C00;

    /// A static constant for a half float with a value of negative infinity.
    const min16uint NEGATIVE_INFINITY = 0xFC00;

    /// A mask which isolates the sign of a half float number.
    const min16uint HALF_FLOAT_SIGN_MASK = 0x8000;

    /// A mask which isolates the exponent of a half float number.
    const min16uint HALF_FLOAT_EXPONENT_MASK = 0x7C00;

    /// A mask which isolates the significand of a half float number.
    const min16uint HALF_FLOAT_SIGNIFICAND_MASK = 0x03FF;

    /// A mask which isolates the sign of a single precision float number.
    const uint32_t FLOAT_SIGN_MASK = 0x80000000;

    /// A mask which isolates the exponent of a single precision float number.
    const uint32_t FLOAT_EXPONENT_MASK = 0x7F800000;

    /// A mask which isolates the significand of a single precision float number.
    const uint32_t FLOAT_SIGNIFICAND_MASK = 0x007FFFFF;
    
    /// Epsilon to handle very close to 0.0
    static const float FLOAT_EPSILON = 0.001;

    // Catch special case floating point values.
    if (isnan(floatValue))
    {
        return NOT_A_NUMBER;
    }
    else if (isinf(floatValue))
    {
        return POSITIVE_INFINITY;
    }

    uint32_t value = floatValue;

    // Required otherwise normals get bungled
    if (floatValue <= FLOAT_EPSILON && floatValue >= -FLOAT_EPSILON)
    {
        return min16uint(0);
        //return uint16_t(value >> 16);
    }
    else
    {
        // Start by computing the significand in half precision format.
        min16uint output = min16uint((value & FLOAT_SIGNIFICAND_MASK) >> 13);

        uint32_t exponent = ((value & FLOAT_EXPONENT_MASK) >> 23);

        // Check for subnormal numbers.
        if (exponent != 0)
        {
            // Check for overflow when converting large numbers, returning positive or negative
            // infinity.
            if (exponent > 142)
            {
                return min16uint((value & FLOAT_SIGN_MASK) >> 16) | min16uint(0x7C00);
            }

            // Add the exponent of the half float, converting the offset binary formats of the
            // representations.
            output |= min16uint(((exponent - 112) << 10) & HALF_FLOAT_EXPONENT_MASK);
        }

        // Add the sign bit.
        output |= min16uint((value & FLOAT_SIGN_MASK) >> 16);

        return output;
    }
}

float3 GetLighting(float3 normal, float3 halfVector, float roughness, float3 eyeVector,
                   float3 lightDirection, float metallic, float occlusion, float3 albedo,
                   float3 radiance, float3 reflectance)
{
    // Cook-Torrance BRDF for specular lighting calculations
    float  NDF = DistributionGGX(normal, halfVector, roughness);
    float  G   = GeometrySmith(normal, eyeVector, lightDirection, roughness);
    float3 F   = FresnelSchlick(max(dot(halfVector, eyeVector), 0.0), reflectance);

    // Specular component of light that is reflected off the surface
    float3 kS = F;
    // Diffuse component of light is left over from what is not reflected and thus refracted
    float3 kD = float3(1.0f, 1.0f, 1.0f) - kS;
    // Metallic prevents any sort of diffuse (refracted) light from occuring.
    // Metallic of 1.0 signals only specular component of light
    kD *= 1.0 - metallic;

    float3 numerator = NDF * G * F;
    float  denominator =
        4.0 * max(dot(normal, eyeVector), 0.0f) * max(dot(normal, lightDirection), 0.0f);
    float3 specular = numerator / max(denominator, 0.001f);
    float3 diffuse  = kD * albedo / PI;

    float NdotL = max(dot(normal, lightDirection), 0.0f);

    // 1) Add the diffuse and specular light components and multiply them by the overall incident
    // ray's light energy (radiance) and then also multiply by the alignment of the surface normal
    // with the incoming light ray's direction and shadowed intensity. 2) NdotL basically says that
    // more aligned the normal and light direction is, the more the light will be scattered within
    // the surface (diffuse lighting) rather than get reflected (specular) which will get color from
    // the diffuse surface the reflected light hits after the bounce.
    float3 lighting = (diffuse + specular) * radiance * NdotL * (1.0f - occlusion);

    return lighting;
}

#endif