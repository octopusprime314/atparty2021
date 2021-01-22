
// Basically pragma once to prevent redefinition of shared files
#ifndef __RANDOM_RAYS_HLSL__
#define __RANDOM_RAYS_HLSL__

float3 GetRandomRayDirection(in uint2 srcRayIndex, in float3 surfaceNormal, in uint2 textureDim, in uint raySampleIndexOffset)
{
    // Calculate coordinate system for the hemisphere.
    float3 u, v, w;
    w = surfaceNormal;

    // Get a vector that's not parallel to w.
    float3 right = 0.3f * w + float3(-0.72f, 0.56f, -0.34f);
    v = normalize(cross(w, right));
    u = cross(v, w);

    // Calculate offsets to the pregenerated sample set.
    uint sampleSetJump;     // Offset to the start of the sample set
    uint sampleJump;        // Offset to the first sample for this pixel within a sample set.
    {
        // Neighboring samples NxN share a sample set, but use different samples within a set.
        // Sharing a sample set lets the pixels in the group get a better coverage of the hemisphere 
        // than if each pixel used a separate sample set with less samples pregenerated per set.

        // Get a common sample set ID and seed shared across neighboring pixels.
        uint numSampleSetsInX = (textureDim.x + numPixelsPerDimPerSet - 1) / numPixelsPerDimPerSet;
        uint2 sampleSetId = srcRayIndex / numPixelsPerDimPerSet;

        // Get a common hitPosition to adjust the sampleSeed by. 
        // This breaks noise correlation on camera movement which otherwise results 
        // in noise pattern swimming across the screen on camera movement.
        uint2 pixelZeroId = sampleSetId * numPixelsPerDimPerSet;
        float3 pixelZeroHitPosition = positionSRV[pixelZeroId].xyz; 
        uint sampleSetSeed = (sampleSetId.y * numSampleSetsInX + sampleSetId.x) * hash(pixelZeroHitPosition) + seed;
        uint RNGState = RNG::SeedThread(sampleSetSeed);

        sampleSetJump = RNG::Random(RNGState, 0, numSampleSets - 1) * numSamplesPerSet;

        // Get a pixel ID within the shared set across neighboring pixels.
        uint2 pixeIDPerSet2D = srcRayIndex % numPixelsPerDimPerSet;
        uint pixeIDPerSet = pixeIDPerSet2D.y * numPixelsPerDimPerSet + pixeIDPerSet2D.x;

        // Randomize starting sample position within a sample set per neighbor group 
        // to break group to group correlation resulting in square alias.
        uint numPixelsPerSet = numPixelsPerDimPerSet * numPixelsPerDimPerSet;
        sampleJump = pixeIDPerSet + RNG::Random(RNGState, 0, numPixelsPerSet - 1) + raySampleIndexOffset;
    }

    // Load a pregenerated random sample from the sample set.
    float3 sample       = sampleSets[sampleSetJump + (sampleJump % numSamplesPerSet)].value;
    float3 rayDirection = normalize(sample.x * u + sample.y * v + sample.z * w);

    return rayDirection;
}

#endif