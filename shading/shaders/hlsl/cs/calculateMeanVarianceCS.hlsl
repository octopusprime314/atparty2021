
// Calculate Local Mean and Variance via a separable kernel and using wave intrinsics.

Texture2D<float>    inputTextureSRV                   : register(t0);
Texture2D           positionSRV                       : register(t1);
RWTexture2D<float2> outputMeanVarianceTextureUAV      : register(u0);
RWTexture2D<float2> outputPartialDistanceDerivatesUAV : register(u1);

cbuffer globalData : register(b0)
{
    float4x4 inverseView;
    uint2 screenSize;
    uint  kernelWidth;
    uint  kernelRadius;
    uint  pixelStepY;
};

// Group shared memory cache for the row aggregated results.
groupshared uint PackedRowResultCache[16][8];            // 16bit float valueSum, squaredValueSum.
groupshared uint NumValuesCache[16][8]; 

static const float InvalidAOCoefficientValue = -1;

bool IsWithinBounds(in int2 index, in int2 dimensions)
{
    return index.x >= 0 && index.y >= 0 && index.x < dimensions.x && index.y < dimensions.y;
}

float2 HalfToFloat2(in uint val)
{
    float2 result;
    result.x = f16tof32(val);
    result.y = f16tof32(val >> 16);
    return result;
}

uint Float2ToHalf(in float2 val)
{
    uint result = 0;
    result      = f32tof16(val.x);
    result |= f32tof16(val.y) << 16;
    return result;
}

// Load up to 16x16 pixels and filter them horizontally.
// The output is cached in Shared Memory and contains NumRows x 8 results.
void FilterHorizontally(in uint2 Gid, in uint GI)
{
    const uint2 GroupDim = uint2(8, 8);
    const uint NumValuesToLoadPerRowOrColumn = GroupDim.x + (kernelWidth - 1);

    // Processes the thread group as row-major 4x16, where each sub group of 16 threads processes one row.
    // Each thread loads up to 4 values, with the sub groups loading rows interleaved.
    // Loads up to 4x16x4 == 256 input values.
    uint2 GTid4x16_row0 = uint2(GI % 16, GI / 16);
    const int2 KernelBasePixel = (Gid * GroupDim - int(kernelRadius)) * int2(1, pixelStepY);
    const uint NumRowsToLoadPerThread = 4;
    const uint Row_BaseWaveLaneIndex = (WaveGetLaneIndex() / 16) * 16;

    [unroll]
    for (uint i = 0; i < NumRowsToLoadPerThread; i++)
    {
        uint2 GTid4x16 = GTid4x16_row0 + uint2(0, i * 4);
        if (GTid4x16.y >= NumValuesToLoadPerRowOrColumn)
        {
            if (GTid4x16.x < GroupDim.x)
            {
                NumValuesCache[GTid4x16.y][GTid4x16.x] = 0;
            }
            break;
        }

        // Load all the contributing columns for each row.
        int2 pixel = KernelBasePixel + GTid4x16 * int2(1, pixelStepY);
        float value = InvalidAOCoefficientValue;

        // The lane is out of bounds of the GroupDim + kernel, 
        // but could be within bounds of the input texture,
        // so don't read it from the texture.
        // However, we need to keep it as an active lane for a below split sum.
        if (GTid4x16.x < NumValuesToLoadPerRowOrColumn && IsWithinBounds(pixel, screenSize))
        {
            value = inputTextureSRV[pixel];
        }

        // Filter the values for the first GroupDim columns.
        {
            // Accumulate for the whole kernel width.
            float valueSum = 0;
            float squaredValueSum = 0;
            uint numValues = 0;

            // Since a row uses 16 lanes, but we only need to calculate the aggregate for the first half (8) lanes,
            // split the kernel wide aggregation among the first 8 and the second 8 lanes, and then combine them.
            
            // Initialize the first 8 lanes to the first cell contribution of the kernel. 
            // This covers the remainder of 1 in kernelWidth / 2 used in the loop below. 
            if (GTid4x16.x < GroupDim.x && value != InvalidAOCoefficientValue)
            {
                valueSum = value;
                squaredValueSum = value * value;
                numValues++;
            }

            // Get the lane index that has the first value for a kernel in this lane.
            uint Row_KernelStartLaneIndex =
                Row_BaseWaveLaneIndex
                + 1     // Skip over the already accumulated first cell of the kernel.
                + (GTid4x16.x < GroupDim.x
                    ? GTid4x16.x
                    : (GTid4x16.x - GroupDim.x) + kernelRadius);

            for (uint c = 0; c < kernelRadius; c++)
            {
                uint laneToReadFrom = Row_KernelStartLaneIndex + c;
                float cValue = WaveReadLaneAt(value, laneToReadFrom);
                if (cValue != InvalidAOCoefficientValue)
                {
                    valueSum += cValue;
                    squaredValueSum += cValue * cValue;
                    numValues++;
                }
            }
            
            // Combine the sub-results.
            uint laneToReadFrom = min(WaveGetLaneCount() - 1, Row_BaseWaveLaneIndex + GTid4x16.x + GroupDim.x);
            valueSum += WaveReadLaneAt(valueSum, laneToReadFrom);
            squaredValueSum += WaveReadLaneAt(squaredValueSum, laneToReadFrom);
            numValues += WaveReadLaneAt(numValues, laneToReadFrom);

            // Store only the valid results, i.e. first GroupDim columns.
            if (GTid4x16.x < GroupDim.x)
            {
                PackedRowResultCache[GTid4x16.y][GTid4x16.x] = Float2ToHalf(float2(valueSum, squaredValueSum));
                NumValuesCache[GTid4x16.y][GTid4x16.x] = numValues;
            }
        }
    }
}

void FilterVertically(uint2 DTid, in uint2 GTid)
{
    float valueSum = 0;
    float squaredValueSum = 0;
    uint numValues = 0;

    uint2 pixel = uint2(DTid.x, DTid.y * pixelStepY);

    float4 val1, val2;
    // Accumulate for the whole kernel.
    for (uint r = 0; r < kernelWidth; r++)
    {
        uint rowID = GTid.y + r;
        uint rNumValues = NumValuesCache[rowID][GTid.x];

        if (rNumValues > 0)
        {
            float2 unpackedRowSum = HalfToFloat2(PackedRowResultCache[rowID][GTid.x]);
            float rValueSum = unpackedRowSum.x;
            float rSquaredValueSum = unpackedRowSum.y;

            valueSum += rValueSum;
            squaredValueSum += rSquaredValueSum;
            numValues += rNumValues;
        }
    }

    // Calculate mean and variance.
    float invN = 1.f / max(numValues, 1);
    float mean = invN * valueSum;

    // Apply Bessel's correction to the estimated variance, multiply by N/N-1, 
    // since the true population mean is not known; it is only estimated as the sample mean.
    float besselCorrection = numValues / float(max(numValues, 2) - 1);
    float variance = besselCorrection * (invN * squaredValueSum - mean * mean);

    variance = max(0, variance);    // Ensure variance doesn't go negative due to imprecision.

    outputMeanVarianceTextureUAV[pixel] = numValues > 0 ? float2(mean, variance) : InvalidAOCoefficientValue;
}

uint GetIndexOfValueClosestToTheReference(in float refValue, in float2 vValues)
{
    float2 delta = abs(refValue - vValues);

    uint outIndex = delta[1] < delta[0] ? 1 : 0;

    return outIndex;
}

[numthreads(8, 8, 1)]
void main(uint2 Gid : SV_GroupID,
        uint2 GTid : SV_GroupThreadID,
        uint GI : SV_GroupIndex,
        uint2 DTid : SV_DispatchThreadID)
{
    FilterHorizontally(Gid, GI);
    GroupMemoryBarrierWithGroupSync();

    FilterVertically(DTid, GTid);

    // Calculate partial distance derivatives for spatial denoising thresholds
    //                x
    //        ----------------->
    //    |    x     [top]     x
    // y  |  [left]   DTiD   [right]
    //    v    x    [bottom]   x
    //
    uint2 top    = clamp(DTid.xy + uint2(0, -1), uint2(0, 0), screenSize.xy - uint2(-1, -1));
    uint2 bottom = clamp(DTid.xy + uint2(0, 1),  uint2(0, 0), screenSize.xy - uint2(-1, -1));
    uint2 left   = clamp(DTid.xy + uint2(-1, 0), uint2(0, 0), screenSize.xy - uint2(-1, -1));
    uint2 right  = clamp(DTid.xy + uint2(1, 0),  uint2(0, 0), screenSize.xy - uint2(-1, -1));

    float3 cameraPosition = float3(inverseView[3][0], inverseView[3][1], inverseView[3][2]);

    float centerValue          = length(positionSRV[DTid.xy].xyz - cameraPosition);
    float2 backwardDifferences = centerValue - float2(length(positionSRV[left].xyz - cameraPosition),
                                                      length(positionSRV[top].xyz - cameraPosition));
    float2 forwardDifferences =
        float2(length(positionSRV[right].xyz - cameraPosition),
               length(positionSRV[bottom].xyz - cameraPosition)) - centerValue;

    // Calculates partial derivatives as the min of absolute backward and forward differences.

    // Find the absolute minimum of the backward and foward differences in each axis
    // while preserving the sign of the difference.
    float2 ddx = float2(backwardDifferences.x, forwardDifferences.x);
    float2 ddy = float2(backwardDifferences.y, forwardDifferences.y);

    uint2  minIndex = {GetIndexOfValueClosestToTheReference(0, ddx),
                       GetIndexOfValueClosestToTheReference(0, ddy)};

    float2 ddxy     = float2(ddx[minIndex.x], ddy[minIndex.y]);

    // Clamp ddxy to a reasonable value to avoid ddxy going over surface boundaries
    // on thin geometry and getting background/foreground blended together on blur.
    float  maxDdxy = 1;
    float2 _sign   = sign(ddxy);
    ddxy           = _sign * min(abs(ddxy), maxDdxy);

    outputPartialDistanceDerivatesUAV[DTid] = abs(ddxy);
}
