SamplerState bilinearWrap : register(s0);
Texture2D normalTexture : register(t0);
Texture2D noiseTexture : register(t1);
Texture2D depthTexture : register(t2);

cbuffer globalData : register(b0)
{
    float3   kernel[64];
    float4x4 projection;
    float4x4 projectionToViewMatrix;
}

float3 decodeLocation(float2 uv)
{
    float4 clipSpaceLocation;
    clipSpaceLocation.xy = mul(uv, 2.0f) - 1.0f;
    // TODO: need to fix cpu
    clipSpaceLocation.y = -clipSpaceLocation.y;
    // dx z clip space is [0,1]
    clipSpaceLocation.z       = depthTexture.Sample(bilinearWrap, uv).r;
    clipSpaceLocation.w       = 1.0f;
    float4 homogenousLocation = mul(clipSpaceLocation, projectionToViewMatrix);
    return homogenousLocation.xyz / homogenousLocation.w;
}

// tile noise texture over screen based on screen dimensions divided by noise size
static const float2 noiseScale = float2(1920.0 / 4.0, 1080.0 / 4.0);

struct PixelOut
{
    float4 color : SV_Target;
};

PixelOut main(float4 posH : SV_POSITION, float2 uv : UVOUT)
{
    PixelOut pixel      = {float4(0.0, 0.0, 0.0, 0.0)};
    int      kernelSize = 64;
    float    radius     = 5.0;
    float    bias       = 0.05;
    float3   fragPos    = decodeLocation(uv);

    if (fragPos.x != 0.0 && fragPos.y != 0.0 && fragPos.z != 0.0)
    {

        float    occlusion = 0.0;
        float3   normal    = normalTexture.Sample(bilinearWrap, uv).rgb;
        float3   randomVec = noiseTexture.SampleLevel(bilinearWrap, uv * noiseScale, 0).xyz;
        float3   tangent   = normalize(randomVec - (normal * dot(randomVec, normal)));
        float3   bitangent = cross(normal, tangent);
        float3x3 TBN       = float3x3(tangent, bitangent, normal);

        for (int i = 0; i < kernelSize; i = i + int(radius))
        {
            // get sample position from tangent to view-space
            float3 sampleValue = mul(kernel[i], TBN);
            sampleValue        = fragPos + (sampleValue * radius);
            float4 offset      = float4(sampleValue, 1.0);
            // from view to clip-space
            offset = mul(offset, projection);
            // perspective divide
            offset.xyz /= offset.w;
            // transform to range 0.0 - 1.0
            offset.xyz        = offset.xyz * 0.5 + 0.5;
            offset.y          = -offset.y;
            float sampleDepth = decodeLocation(offset.xy).z;
            float rangeCheck  = smoothstep(0.0, 1.0, radius / abs(fragPos.z - sampleDepth));
            occlusion += (sampleDepth < sampleValue.z + bias ? 1.0 : 0.0) * rangeCheck;
        }
        occlusion     = 1.0 - (occlusion / kernelSize);
        pixel.color.r = occlusion;
        return pixel;
    }
    else
    {
        return pixel;
    }
}