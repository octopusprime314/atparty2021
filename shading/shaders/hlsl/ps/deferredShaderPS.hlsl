#include "../include/structs.hlsl"

Texture2D diffuseTexture  : register(t0);    // Diffuse texture data array
Texture2D normalTexture   : register(t1);    // Normal texture data array
Texture2D depthTexture    : register(t2);    // Depth texture data array
Texture2D positionTexture : register(t3);    // Position texture data array
Texture2D ssaoTexture     : register(t4);

SamplerState bilinearWrap : register(s0);

cbuffer globalData : register(b0)
{
    float4x4 inverseProjection;
    float4x4 inverseView;

    float4 pointLightColors[MAX_LIGHTS];
    float4 pointLightRanges[MAX_LIGHTS / 4];
    float4 pointLightPositions[MAX_LIGHTS];

    float2 screenSize;
    int    numPointLights;
}

#include "../include/pointLightCommon.hlsl"

struct PixelOut
{
    float4 color  : SV_Target0;
    float  depth  : SV_Depth;
};

static const float2 poissonDisk[4] = {
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
    float2(-0.094184101, -0.92938870), float2(0.34495938, 0.29387760)};

static const float shadowEffect = 0.6;
static const float ambient      = 0.3;


float3 decodeLocation(float2 uv)
{
    float4 clipSpaceLocation;
    clipSpaceLocation.xy = mul(uv, 2.0f) - 1.0f;
    // TODO: need to fix cpu
    clipSpaceLocation.y = -clipSpaceLocation.y;
    // dx z clip space is [0,1]
    clipSpaceLocation.z       = depthTexture.Sample(bilinearWrap, uv).r;
    clipSpaceLocation.w       = 1.0f;
    float4 homogenousLocation = mul(clipSpaceLocation, inverseProjection);
    return homogenousLocation.xyz / homogenousLocation.w;
}

PixelOut main(float4 posH : SV_POSITION,
              float2 uv   : UVOUT)
{

    const float bias = 0.005; // removes shadow acne by adding a small bias

    PixelOut pixel = {float4(0.0, 0.0, 0.0, 0.0), 1.0};

    // extract position from depth texture
    float3 position = mul(float4(decodeLocation(uv), 1.0), inverseView).xyz;
    float3 normal   = normalTexture.Sample(bilinearWrap, uv).xyz;
    float3 albedo   = diffuseTexture.Sample(bilinearWrap, uv).xyz;

    float transmittance = diffuseTexture.Sample(bilinearWrap, uv).w;
    float metallic      = positionTexture.Sample(bilinearWrap, uv).w;
    float roughness     = normalTexture.Sample(bilinearWrap, uv).w;

    float occlusion = ssaoTexture.Sample(bilinearWrap, uv).r;

    // blit depth
    pixel.depth = depthTexture.Sample(bilinearWrap, uv).r;

    // Detects if there is no screen space information and then displays skybox!
    if (normal.x == 0.0 && normal.y == 0.0 && normal.z == 0.0)
    {
        pixel.color = float4(0.0, 0.0, 0.0, 0.0);
        // skybox depth trick to have it displayed at the depth boundary
        // precision matters here and must be as close as possible to 1.0
        // the number of 9s can only go to 7 but no less than 4
        pixel.depth = 0.9999999;
    }
    else
    {
        uint   recursionCount = 0;
        float3 color          = albedo;
        //GetBRDFPointLight(albedo, normal, position, roughness, metallic,
        //                                       uint2(0, 0), false, recursionCount);
        pixel.color = float4(color * occlusion, 1.0);
    }

    return pixel;
}