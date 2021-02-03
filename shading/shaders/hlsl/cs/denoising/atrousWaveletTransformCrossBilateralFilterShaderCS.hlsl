// Atrous Wavelet Transform Cross Bilateral Filter.
// Based on a 1st pass of [SVGF] filter.
// Ref: [Dammertz2010], Edge-Avoiding A-Trous Wavelet Transform for Fast Global Illumination
// Filtering Ref: [SVGF], Spatiotemporal Variance-Guided Filtering Ref: [RTGCH19] Ray Tracing Gems
// (Ch 19)

#include "../include/math.hlsl"

Texture2D<float2>  occlusionAndHitDistanceSRV : register(t0);
Texture2D<float2>  meanVarianceSRV            : register(t1);
Texture2D          normalSRV                  : register(t2);
Texture2D          positionSRV                : register(t3);
Texture2D<float2>  partialDistanceDerivatives : register(t4);
RWTexture2D<float> outputFilteredUAV          : register(u0);

cbuffer globalData : register(b0)
{
    uint2 screenSize;
    float depthWeightCutoff;
    bool  usingBilateralDownsampledBuffers;

    bool  useAdaptiveKernelSize;
    float kernelRadiusLerfCoef;
    uint  minKernelWidth;
    uint  maxKernelWidth;

    float rayHitDistanceToKernelWidthScale;
    float rayHitDistanceToKernelSizeScaleExponent;
    bool  perspectiveCorrectDepthInterpolation;
    float minVarianceToDenoise;

    float valueSigma;
    float depthSigma;
    float normalSigma;
    uint  depthNumMantissaBits;
};

#define GAUSSIAN_KERNEL_3X3

namespace FilterKernel
{
#if defined(BOX_KERNEL_3X3)
static const unsigned int Radius               = 1;
static const unsigned int Width                = 1 + 2 * Radius;
static const float        Kernel[Width][Width] = {
    {1. / 9, 1. / 9, 1. / 9},
    {1. / 9, 1. / 9, 1. / 9},
    {1. / 9, 1. / 9, 1. / 9},
};

#elif defined(BOX_KERNEL_5X5)
static const unsigned int Radius               = 2;
static const unsigned int Width                = 1 + 2 * Radius;
static const float        Kernel[Width][Width] = {
    {1. / 25, 1. / 25, 1. / 25, 1. / 25, 1. / 25}, {1. / 25, 1. / 25, 1. / 25, 1. / 25, 1. / 25},
    {1. / 25, 1. / 25, 1. / 25, 1. / 25, 1. / 25}, {1. / 25, 1. / 25, 1. / 25, 1. / 25, 1. / 25},
    {1. / 25, 1. / 25, 1. / 25, 1. / 25, 1. / 25},
};

#elif defined(BOX_KERNEL_7X7)
static const unsigned int Radius = 3;
static const unsigned int Width  = 1 + 2 * Radius;

#elif defined(GAUSSIAN_KERNEL_3X3)
static const unsigned int Radius               = 1;
static const unsigned int Width                = 1 + 2 * Radius;
static const float        Kernel1D[Width]      = {0.27901, 0.44198, 0.27901};
static const float        Kernel[Width][Width] = {
    {Kernel1D[0] * Kernel1D[0], Kernel1D[0] * Kernel1D[1], Kernel1D[0] * Kernel1D[2]},
    {Kernel1D[1] * Kernel1D[0], Kernel1D[1] * Kernel1D[1], Kernel1D[1] * Kernel1D[2]},
    {Kernel1D[2] * Kernel1D[0], Kernel1D[2] * Kernel1D[1], Kernel1D[2] * Kernel1D[2]},
};

#elif defined(GAUSSIAN_KERNEL_5X5)
static const unsigned int Radius               = 2;
static const unsigned int Width                = 1 + 2 * Radius;
static const float        Kernel1D[Width]      = {1. / 16, 1. / 4, 3. / 8, 1. / 4, 1. / 16};
static const float        Kernel[Width][Width] = {
    {Kernel1D[0] * Kernel1D[0], Kernel1D[0] * Kernel1D[1], Kernel1D[0] * Kernel1D[2],
     Kernel1D[0] * Kernel1D[3], Kernel1D[0] * Kernel1D[4]},
    {Kernel1D[1] * Kernel1D[0], Kernel1D[1] * Kernel1D[1], Kernel1D[1] * Kernel1D[2],
     Kernel1D[1] * Kernel1D[3], Kernel1D[1] * Kernel1D[4]},
    {Kernel1D[2] * Kernel1D[0], Kernel1D[2] * Kernel1D[1], Kernel1D[2] * Kernel1D[2],
     Kernel1D[2] * Kernel1D[3], Kernel1D[2] * Kernel1D[4]},
    {Kernel1D[3] * Kernel1D[0], Kernel1D[3] * Kernel1D[1], Kernel1D[3] * Kernel1D[2],
     Kernel1D[3] * Kernel1D[3], Kernel1D[3] * Kernel1D[4]},
    {Kernel1D[4] * Kernel1D[0], Kernel1D[4] * Kernel1D[1], Kernel1D[4] * Kernel1D[2],
     Kernel1D[4] * Kernel1D[3], Kernel1D[4] * Kernel1D[4]},
};

#elif defined(GAUSSIAN_KERNEL_7X7)
static const unsigned int Radius          = 3;
static const unsigned int Width           = 1 + 2 * Radius;
static const float        Kernel1D[Width] = {0.00598,  0.060626, 0.241843, 0.383103,
                                      0.241843, 0.060626, 0.00598};

#elif defined(GAUSSIAN_KERNEL_9X9)
static const unsigned int Radius          = 4;
static const unsigned int Width           = 1 + 2 * Radius;
static const float        Kernel1D[Width] = {0.000229, 0.005977, 0.060598, 0.241732, 0.382928,
                                      0.241732, 0.060598, 0.005977, 0.000229};
#endif
} // namespace FilterKernel

static const float InvalidAOCoefficientValue = -1;

bool IsWithinBounds(in int2 index, in int2 dimensions)
{
    return index.x >= 0 && index.y >= 0 && index.x < dimensions.x && index.y < dimensions.y;
}

// Returns an approximate surface dimensions covered in a pixel.
// This is a simplified model assuming pixel to pixel view angles are the same.
// z - linear depth of the surface at the pixel
// ddxy - partial depth derivatives
// tan_a - tangent of a per pixel view angle
float2 ApproximateProjectedSurfaceDimensionsPerPixel(in float z, in float2 ddxy, in float tan_a)
{
    // Surface dimensions for a surface parallel at z.
    float2 dx = tan_a * z;

    // Using Pythagorean theorem approximate the surface dimensions given the ddxy.
    float2 w = sqrt(dx * dx + ddxy * ddxy);

    return w;
}

// Remap partial depth derivatives at z0 from [1,1] pixel offset to a new pixel offset.
float2 RemapDdxy(in float z0, in float2 ddxy, in float2 pixelOffset)
{
    // Perspective correction for non-linear depth interpolation.
    // Ref:
    // https://www.scratchapixel.com/lessons/3d-basic-rendering/rasterization-practical-implementation/visibility-problem-depth-buffer-depth-interpolation
    // Given a linear depth interpolation for finding z at offset q along z0 to z1
    //      z =  1 / (1 / z0 * (1 - q) + 1 / z1 * q)
    // and z1 = z0 + ddxy, where z1 is at a unit pixel offset [1, 1]
    // z can be calculated via ddxy as
    //
    //      z = (z0 + ddxy) / (1 + (1-q) / z0 * ddxy)
    float2 z = (z0 + ddxy) / (1 + ((1 - pixelOffset) / z0) * ddxy);
    return sign(pixelOffset) * (z - z0);
}

uint SmallestPowerOf2GreaterThan(in uint x)
{
    // Set all the bits behind the most significant non-zero bit in x to 1.
    // Essentially giving us the largest value that is smaller than the
    // next power of 2 we're looking for.
    x |= x >> 1;
    x |= x >> 2;
    x |= x >> 4;
    x |= x >> 8;
    x |= x >> 16;

    // Return the next power of two value.
    return x + 1;
}

// Returns float precision for a given float value.
// Values within (value -precision, value + precision) map to the same value.
// Precision = exponentRange/MaxMantissaValue = (2^e+1 - 2^e) / (2^NumMantissaBits)
// Ref: https://blog.demofox.org/2017/11/21/floating-point-precision/
float FloatPrecision(in float x, in uint NumMantissaBits)
{
    // Find the exponent range the value is in.
    uint  nextPowerOfTwo = SmallestPowerOf2GreaterThan(x);
    float exponentRange  = nextPowerOfTwo - (nextPowerOfTwo >> 1);

    float MaxMantissaValue = 1 << NumMantissaBits;

    return exponentRange / MaxMantissaValue;
}

float DepthThreshold(float depth, float2 ddxy, float2 pixelOffset)
{
    float depthThreshold = 0.0;

    if (perspectiveCorrectDepthInterpolation)
    {
        float2 newDdxy = RemapDdxy(depth, ddxy, pixelOffset);
        depthThreshold = dot(1, abs(newDdxy));
    }
    else
    {
        depthThreshold = dot(1, abs(pixelOffset * ddxy));
    }

    return depthThreshold;
}

void AddFilterContribution(inout float weightedValueSum, inout float weightSum, in float value,
                           in float stdDeviation, in float depth, in float3 normal, in float2 ddxy,
                           in uint row, in uint col, in uint2 kernelStep, in uint2 DTid)
{

    int2  pixelOffset   = int2(row - FilterKernel::Radius, col - FilterKernel::Radius) * kernelStep;
    float varianceScale = 1;
    int2  id            = int2(DTid) + pixelOffset;

    if (IsWithinBounds(id, screenSize))
    {
        float  iDepth  = positionSRV[id].z;
        float3 iNormal = normalSRV[id].xyz;
        float  iValue  = occlusionAndHitDistanceSRV[id].x;

        bool iIsValidValue = iValue != InvalidAOCoefficientValue;
        if (!iIsValidValue || iDepth == 0)
        {
            return;
        }

        // Calculate a weight for the neighbor's contribtuion.
        // Ref:[SVGF]
        float w = 0.0;
        {
            // Value based weight.
            // Lower value tolerance for the neighbors further apart. Prevents overbluring sharp
            // value transitions. Ref: [Dammertz2010]
            const float errorOffset        = 0.005f;
            float       valueSigmaDistCoef = 1.0 / length(pixelOffset);
            float       e_x                = -abs(value - iValue) / (valueSigmaDistCoef * valueSigma * stdDeviation + errorOffset);
            float       w_x                = exp(e_x);

            // Normal based weight.
            float w_n = pow(max(0, dot(normal, iNormal)), normalSigma);

            // Depth based weight.
            float w_d = 0.0;
            {
                float2 pixelOffsetForDepth = pixelOffset;

                // Account for sample offset in bilateral downsampled partial depth derivative
                // buffer.
                if (usingBilateralDownsampledBuffers)
                {
                    float2 offsetSign   = sign(pixelOffset);
                    pixelOffsetForDepth = pixelOffset + offsetSign * float2(0.5, 0.5);
                }

                float depthFloatPrecision = FloatPrecision(max(depth, iDepth), depthNumMantissaBits);
                float depthThreshold      = DepthThreshold(depth, ddxy, pixelOffsetForDepth);
                float depthTolerance      = depthSigma * depthThreshold + depthFloatPrecision;
                float delta               = abs(depth - iDepth);
                delta = max(0, delta - depthFloatPrecision); // Avoid distinguising initial values up to
                                                             // the float precision. Gets rid of banding
                                                             // due to low depth precision format.
                w_d = exp(-delta / depthTolerance);

                // Scale down contributions for samples beyond tolerance, but completely disable
                // contribution for samples too far away.
                w_d *= w_d >= depthWeightCutoff;
            }

            // Filter kernel weight.
            float w_h = FilterKernel::Kernel[row][col];

            // Final weight.
            w = w_h * w_n /** w_x */* w_d;
            // w_x is preventing everything from getting a gaussian blur
            //w = w_h * w_n * w_x  * w_d;
        }

        weightedValueSum += w * iValue;
        weightSum += w;
    }
}

[numthreads(16, 16, 1)]
void main(uint2 DTid : SV_DispatchThreadID,
        uint2 Gid  : SV_GroupID)
{
    if (!IsWithinBounds(DTid, screenSize))
    {
        return;
    }

    // Initialize values to the current pixel / center filter kernel value.
    float  value  = occlusionAndHitDistanceSRV[DTid].x;
    float  depth  = positionSRV[DTid].z;
    float3 normal = normalSRV[DTid].xyz;

    bool  isValidValue  = value != InvalidAOCoefficientValue;
    float filteredValue = value;
    float variance      = meanVarianceSRV[DTid].y;

    if (depth != 0)
    {
        float2 ddxy             = partialDistanceDerivatives[DTid];
        float  weightSum        = 0;
        float  weightedValueSum = 0;
        float  stdDeviation     = 1;

        if (isValidValue)
        {
            float w          = FilterKernel::Kernel[FilterKernel::Radius][FilterKernel::Radius];
            weightSum        = w;
            weightedValueSum = weightSum * value;
            stdDeviation     = sqrt(variance);
        }

        // Adaptive kernel size
        // Scale the kernel span based on AO ray hit distance.
        // This helps filter out lower frequency noise, a.k.a. boiling artifacts.
        // Ref: [RTGCH19]
        uint2 kernelStep = 0;
        if (useAdaptiveKernelSize && isValidValue)
        {
            float avgRayHitDistance = occlusionAndHitDistanceSRV[DTid].y;

            float  perPixelViewAngle = (45.0f / screenSize.y) * PI / 180.0;
            float  tan_a             = tan(perPixelViewAngle);
            float2 projectedSurfaceDim = ApproximateProjectedSurfaceDimensionsPerPixel(depth, ddxy, tan_a);

            // Calculate a kernel width as a ratio of hitDistance / projected surface dim per pixel.
            // Apply a non-linear factor based on relative rayHitDistance. This is because
            // average ray hit distance grows large fast if the closeby occluders cover only part of
            // the hemisphere. Having a smaller kernel for such cases helps preserve occlusion
            // detail.
            float t = min(avgRayHitDistance / 22.0, 1); // 22 was selected empirically
            float k = rayHitDistanceToKernelWidthScale * pow(t, rayHitDistanceToKernelSizeScaleExponent);
            kernelStep = max(1, round(k * avgRayHitDistance / projectedSurfaceDim));

            uint2 targetKernelStep = clamp(kernelStep, (minKernelWidth - 1) / 2, (maxKernelWidth - 1) / 2);

            // TODO: additional options to explore
            // - non-uniform X, Y kernel radius cause visible streaking. Use same step across both
            // X, Y? That may overblur one dimension at sharp angles.
            // - use larger kernel on lower tspp.
            // - use varying number of cycles for better spatial coverage over time, depending on
            // the target kernel step. More cycles on larger kernels.
            uint2 adjustedKernelStep = lerp(1, targetKernelStep, kernelRadiusLerfCoef);
            kernelStep               = adjustedKernelStep;
        }

        if (variance >= minVarianceToDenoise)
        {
            // Add contributions from the neighborhood.
            [unroll]
            for (uint r = 0; r < FilterKernel::Width; r++)
            {
                [unroll]
                for (uint c = 0; c < FilterKernel::Width; c++)
                {
                    if (r != FilterKernel::Radius || c != FilterKernel::Radius)
                    {
                        AddFilterContribution(weightedValueSum, weightSum, value, stdDeviation,
                                              depth, normal, ddxy, r, c, kernelStep, DTid);
                    }
                }
            }
        }

        float smallValue = 1e-6f;
        if (weightSum > smallValue)
        {
            filteredValue = weightedValueSum / weightSum;
        }
        else
        {
            filteredValue = InvalidAOCoefficientValue;
        }
    }

    outputFilteredUAV[DTid] = filteredValue;
}