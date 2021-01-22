// Desc: Stage 1 of Temporal Supersampling. Samples temporal cache via motion vectors/reverse reprojection.
// If no valid values have been retrieved from the cache, the tspp is set to 0.

Texture2D<float4> textureSpaceMotionVectorAndDepth : register(t0);
Texture2D<float4> normalSRV                        : register(t1);
Texture2D<float2> partialDistanceDerivatesSRV      : register(t2);
Texture2D<float4> occlusionAndHitDistanceSRV       : register(t3);
Texture2D<float2> occlusionHistoryBufferSRV        : register(t4);
Texture2D<uint>   temporalSamplesPerPixelSRV       : register(t5);
Texture2D<float2> meanVarianceSRV                  : register(t6);

RWTexture2D<uint> temporalSamplesPerPixelUAV : register(u0);
RWTexture2D<float4> debug0UAV : register(u1);
RWTexture2D<float4> debug1UAV : register(u2);

SamplerState ClampSampler : register(s0);

cbuffer globalData : register(b0)
{
    uint2 screenSize;
};

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

namespace CrossBilateral
{
    namespace Normal
    {
        struct Parameters
        {
            float Sigma;
            float SigmaExponent;
        };

        // Get cross bilateral normal based weights.
        float4 GetWeights(
            in float3 TargetNormal,
            in float3 SampleNormals[4],
            in Parameters Params)
        {
            float4 NdotSampleN = float4(
                dot(TargetNormal, SampleNormals[0]),
                dot(TargetNormal, SampleNormals[1]),
                dot(TargetNormal, SampleNormals[2]),
                dot(TargetNormal, SampleNormals[3]));

            // Apply adjustment scale to the dot product. 
            // Values greater than 1 increase tolerance scale 
            // for unwanted inflated normal differences,
            // such as due to low-precision normal quantization.
            NdotSampleN *= Params.Sigma;

            float4 normalWeights = pow(saturate(NdotSampleN), Params.SigmaExponent);

            return normalWeights;
        }
    }

    // Linear depth.
    namespace Depth
    {
        struct Parameters
        {
            float Sigma;
            float WeightCutoff;
            uint NumMantissaBits;
        };
               
        float4 GetWeights(
            in float TargetDepth,
            in float2 Ddxy,
            in float4 SampleDepths,
            in Parameters Params)
        {
            float depthThreshold = dot(1, abs(Ddxy));
            float depthFloatPrecision = FloatPrecision(TargetDepth, Params.NumMantissaBits);

            float depthTolerance = Params.Sigma * depthThreshold + depthFloatPrecision;
            float4 depthWeights = min(depthTolerance / (abs(SampleDepths - TargetDepth) + depthFloatPrecision), 1);
            //depthWeights *= depthWeights >= Params.WeightCutoff;

            return depthWeights;
        }

        float4 GetWeights(
            in float TargetDepth,
            in float2 Ddxy,
            in float4 SampleDepths,
            in float2 SampleOffset, // offset in-between the samples to remap ddxy for.
            in Parameters Params)
        {
            float2 remappedDdxy = RemapDdxy(TargetDepth, Ddxy, SampleOffset);
            return GetWeights(TargetDepth, remappedDdxy, SampleDepths, Params);
        }
    }

    namespace Bilinear
    {
        // TargetOffset - offset from the top left ([0,0]) sample of the quad samples.
        float4 GetWeights(in float2 TargetOffset)
        {
            float4 bilinearWeights =
                float4(
                    (1 - TargetOffset.x) * (1 - TargetOffset.y),
                    TargetOffset.x * (1 - TargetOffset.y),
                    (1 - TargetOffset.x) * TargetOffset.y,
                    TargetOffset.x * TargetOffset.y);

            return bilinearWeights;
        }
    }

    namespace BilinearDepthNormal
    {
        struct Parameters
        {
            Normal::Parameters Normal;
            Depth::Parameters Depth;
        };

        float4 GetWeights(
            in float TargetDepth,
            in float3 TargetNormal,
            in float2 TargetOffset,
            in float2 Ddxy,
            in float4 SampleDepths,
            in float3 SampleNormals[4],
            in float2 SamplesOffset,
            Parameters Params)
        {
            float4 bilinearWeights = Bilinear::GetWeights(TargetOffset);
            float4 depthWeights = Depth::GetWeights(TargetDepth, Ddxy, SampleDepths, SamplesOffset, Params.Depth);
            float4 normalWeights = Normal::GetWeights(TargetNormal, SampleNormals, Params.Normal);

            return bilinearWeights * depthWeights * normalWeights;
        }

        float4 GetWeights(
            in float TargetDepth,
            in float3 TargetNormal,
            in float2 TargetOffset,
            in float2 Ddxy,
            in float4 SampleDepths,
            in float3 SampleNormals[4],
            Parameters Params)
        {
            float4 bilinearWeights = Bilinear::GetWeights(TargetOffset);
            float4 depthWeights = Depth::GetWeights(TargetDepth, Ddxy, SampleDepths, Params.Depth);
            float4 normalWeights = Normal::GetWeights(TargetNormal, SampleNormals, Params.Normal);

            return bilinearWeights * depthWeights * normalWeights;
        }
    }
}

bool IsWithinBounds(in int2 index, in int2 dimensions)
{
    return index.x >= 0 && index.y >= 0 && index.x < dimensions.x && index.y < dimensions.y;
}

float4 BilateralResampleWeights(in float TargetDepth, in float3 TargetNormal, in float4 SampleDepths, in float3 SampleNormals[4], in float2 TargetOffset, in uint2 TargetIndex, in int2 sampleIndices[4], in float2 Ddxy)
{
    bool4 isWithinBounds = bool4(
        IsWithinBounds(sampleIndices[0], screenSize),
        IsWithinBounds(sampleIndices[1], screenSize),
        IsWithinBounds(sampleIndices[2], screenSize),
        IsWithinBounds(sampleIndices[3], screenSize));
 
    CrossBilateral::BilinearDepthNormal::Parameters params;
    params.Depth.Sigma = 1;
    params.Depth.WeightCutoff = 0.5;
    params.Depth.NumMantissaBits = 24;
    params.Normal.Sigma = 1.1;      // Bump the sigma a bit to add tolerance for slight geometry misalignments and/or format precision limitations.
    params.Normal.SigmaExponent = 32; 

    float4 bilinearDepthNormalWeights = CrossBilateral::BilinearDepthNormal::GetWeights(
        TargetDepth,
        TargetNormal,
        TargetOffset,
        Ddxy,
        SampleDepths,
        SampleNormals,
        params);

    float4 weights = isWithinBounds * bilinearDepthNormalWeights;

    return weights;
}

static const float InvalidAOCoefficientValue = -1;

[numthreads(8, 8, 1)]
void main(uint2 DTid : SV_DispatchThreadID)
{
    float3 _normal = normalSRV[DTid].xyz;
    float  _depth = textureSpaceMotionVectorAndDepth[DTid].z;
    float2 textureSpaceMotionVector = textureSpaceMotionVectorAndDepth[DTid].xy;

    if (_depth == 0 || textureSpaceMotionVector.x > 1e2f)
    {
        temporalSamplesPerPixelUAV[DTid] = 0;
        return;
    }

    float2 invScreenSpace = (1.0f / float2(screenSize.x, screenSize.y));
    float2 texturePos = (DTid.xy + 0.5f) * invScreenSpace;
    float2 cacheFrameTexturePos = texturePos - textureSpaceMotionVector;

    // Find the nearest integer index smaller than the texture position.
    // The floor() ensures the that value sign is taken into consideration.
    int2 topLeftCacheFrameIndex = floor(cacheFrameTexturePos * screenSize - 0.5);
    float2 adjustedCacheFrameTexturePos = (topLeftCacheFrameIndex + 0.5) * invScreenSpace;

    float2 cachePixelOffset = cacheFrameTexturePos * screenSize - 0.5 - topLeftCacheFrameIndex;

    const int2 srcIndexOffsets[4] = { {0, 0}, {1, 0}, {0, 1}, {1, 1} };

    int2 cacheIndices[4] = {
        topLeftCacheFrameIndex + srcIndexOffsets[0],
        topLeftCacheFrameIndex + srcIndexOffsets[1],
        topLeftCacheFrameIndex + srcIndexOffsets[2],
        topLeftCacheFrameIndex + srcIndexOffsets[3] };

    float3 cacheNormals[4];
    float4 vCacheDepths = float4(0.0, 0.0, 0.0, 0.0);
    {
        float4 normalsX = normalSRV.GatherRed(ClampSampler, adjustedCacheFrameTexturePos).wzxy;
        float4 normalsY = normalSRV.GatherGreen(ClampSampler, adjustedCacheFrameTexturePos).wzxy;
        float4 normalsZ = normalSRV.GatherBlue(ClampSampler, adjustedCacheFrameTexturePos).wzxy;
        float4 depths = occlusionHistoryBufferSRV.GatherGreen(ClampSampler, adjustedCacheFrameTexturePos).wzxy;

        [unroll]
        for (int i = 0; i < 4; i++)
        {
            cacheNormals[i] = float3(normalsX[i], normalsY[i], normalsZ[i]);
            vCacheDepths[i] = depths[i];
        }
    }

    //debug0UAV[DTid].xyz = cacheNormals[0]; // cacheNormals[0].xyz;
    //debug1UAV[DTid] = vCacheDepths;

    float2 ddxy = partialDistanceDerivatesSRV[DTid];

    float4 weights;
    weights = BilateralResampleWeights(_depth, _normal, vCacheDepths, cacheNormals, cachePixelOffset, DTid, cacheIndices, ddxy);

    debug0UAV[DTid].xyz = cacheNormals[0];
    debug1UAV[DTid]     = weights;

    // Invalidate weights for invalid values in the cache.
    float4 vCacheValues = occlusionHistoryBufferSRV.GatherRed(ClampSampler, adjustedCacheFrameTexturePos).wzxy;
    weights = vCacheValues != InvalidAOCoefficientValue ? weights : 0;
    float weightSum = dot(1, weights);

    float cachedValue = InvalidAOCoefficientValue;
    float cachedValueSquaredMean = 0;
    float cachedRayHitDepth = 0;

    uint tspp = 0;
    bool areCacheValuesValid = weightSum > 1e-3f;


    if (areCacheValuesValid)
    {
        uint4 vCachedTspp = temporalSamplesPerPixelSRV.GatherRed(ClampSampler, adjustedCacheFrameTexturePos).wzxy;
        // Enforce tspp of at least 1 for reprojection for valid values.
        // This is because the denoiser will fill in invalid values with filtered 
        // ones if it can. But it doesn't increase tspp.
        vCachedTspp = max(1, vCachedTspp);


        float4 nWeights = weights / weightSum;   // Normalize the weights.


        // Scale the tspp by the total weight. This is to keep the tspp low for 
        // total contributions that have very low reprojection weight. While its preferred to get 
        // a weighted value even for reprojections that have low weights but still
        // satisfy consistency tests, the tspp needs to be kept small so that the Target calculated values
        // are quickly filled in over a few frames. Otherwise, bad estimates from reprojections,
        // such as on disocclussions of surfaces on rotation, are kept around long enough to create 
        // visible streaks that fade away very slow.
        // Example: rotating camera around dragon's nose up close. 
        float TsppScale = 1; // TODO saturate(weightSum); 

        float cachedTspp = TsppScale * dot(nWeights, vCachedTspp);
        tspp = round(cachedTspp);

        
        if (tspp > 0)
        {
            float4 vCacheValues = occlusionAndHitDistanceSRV.GatherRed(ClampSampler, adjustedCacheFrameTexturePos).wzxy;
            cachedValue = dot(nWeights, vCacheValues);

            float4 vCachedValueSquaredMean = meanVarianceSRV.GatherRed(ClampSampler, adjustedCacheFrameTexturePos).wzxy;
            cachedValueSquaredMean = dot(nWeights, vCachedValueSquaredMean);

            float4 vCachedRayHitDepths = occlusionAndHitDistanceSRV.GatherGreen(ClampSampler, adjustedCacheFrameTexturePos).wzxy;
            cachedRayHitDepth = dot(nWeights, vCachedRayHitDepths);

            debug0UAV[DTid].w = vCacheValues.x + cachedValue.x + cachedValueSquaredMean.x + cachedRayHitDepth.x;
        }

    }
    else
    {
        // No valid values can be retrieved from the cache.
        // TODO: try a greater cache footprint to find useful samples,
        //   For example a 3x3 pixel cache footprint or use lower mip cache input.
        tspp = 0;
    }
    temporalSamplesPerPixelUAV[DTid] = tspp;
}