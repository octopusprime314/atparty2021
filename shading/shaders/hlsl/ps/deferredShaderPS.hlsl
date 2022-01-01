#include "../include/structs.hlsl"
#include "../include/math.hlsl"

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
    float pointLightRanges[MAX_LIGHTS];
    float4 pointLightPositions[MAX_LIGHTS];

    float4 sunLightColor;
    float4 sunLightPosition;

    float2 screenSize;
    int    numPointLights;
}

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

float3 GetBRDFLight(float2 uv)
{
    // extract position from depth texture
    float3 hitPosition = mul(float4(decodeLocation(uv), 1.0), inverseView).xyz;
    float3 normal   = normalTexture.Sample(bilinearWrap, uv).xyz;
    float3 albedo   = diffuseTexture.Sample(bilinearWrap, uv).xyz;

    float transmittance = diffuseTexture.Sample(bilinearWrap, uv).w;
    float metallic      = positionTexture.Sample(bilinearWrap, uv).w;
    float roughness     = normalTexture.Sample(bilinearWrap, uv).w;

    float occlusion = ssaoTexture.Sample(bilinearWrap, uv).r;

    float3 cameraPosition = float3(inverseView[3][0], inverseView[3][1], inverseView[3][2]);
    float3 eyeVector      = normalize(hitPosition - cameraPosition);

    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0        = lerp(F0, albedo, metallic);

    // reflectance equation
    float3 Lo = float3(0.0f, 0.0f, 0.0f);

    // calculate per-light radiance
    float3 lightDirection = normalize(hitPosition - sunLightPosition.xyz);
    float3 halfVector     = normalize(eyeVector + lightDirection);
    float  distance       = length(hitPosition - sunLightPosition.xyz);
    float3 radiance       = sunLightColor.xyz * (100 / 8.0);

    // Cook-Torrance BRDF for specular lighting calculations
    float  NDF = DistributionGGX(normal, halfVector, roughness);
    float  G   = GeometrySmith(normal, eyeVector, lightDirection, roughness);
    float3 F   = FresnelSchlick(max(dot(halfVector, eyeVector), 0.0), F0);

    // Specular component of light that is reflected off the surface
    float3 kS = F;
    // Diffuse component of light is left over from what is not reflected and thus refracted
    float3 kD = float3(1.0f, 1.0f, 1.0f) - kS;
    // Metallic prevents any sort of diffuse (refracted) light from occuring.
    // Metallic of 1.0 signals only specular component of light
    kD *= 1.0 - metallic;

    float3 numerator   = NDF * G * F;
    float  denominator =  4.0 * max(dot(normal, eyeVector), 0.0f) * max(dot(normal, lightDirection), 0.0f);
    float3 specular    = numerator / max(denominator, 0.001f);
    float3 diffuse     = kD * albedo / PI;

    float NdotL = max(dot(normal, lightDirection), 0.0f);

    // 1) Add the diffuse and specular light components and multiply them by the overall incident ray's light energy (radiance)
    // and then also multiply by the alignment of the surface normal with the incoming light ray's direction and shadowed intensity.
    // 2) NdotL basically says that more aligned the normal and light direction is, the more the light
    // will be scattered within the surface (diffuse lighting) rather than get reflected (specular)
    // which will get color from the diffuse surface the reflected light hits after the bounce.
    float3 color = (diffuse + specular) * radiance * NdotL;

     // Gamma correction
    float colorScale = 1.0f / 2.2f;
    color            = color / (color + float3(1.0f, 1.0f, 1.0f));
    color            = pow(color, colorScale);
    return color;
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

        color       = GetBRDFLight(uv);
        pixel.color = float4(color, 1.0);
    }

    return pixel;
}