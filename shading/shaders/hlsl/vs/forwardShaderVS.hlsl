// SRV data
Texture2D   textureMap : register(t0);
Texture2D   cameraDepthTexture : register(t1);
Texture2D   mapDepthTexture : register(t2);
TextureCube depthMap : register(t3);
sampler     textureSampler : register(s0);

// Constant Buffer data
cbuffer globalData : register(b0)
{
    float3   pointLightPositions[20];
    float3   pointLightColors[20];
    float    pointLightRanges[20];
    float4x4 lightMapViewMatrix;
    float4x4 viewToModelMatrix;
    float4x4 lightViewMatrix;
    int      numPointLights;
    float3   lightDirection;
    float4x4 normalMatrix;
    float4x4 projection;
    float4x4 prevModel;
    float4x4 prevView;
    int      views;
    float4x4 view;
    float4x4 model;
}

static const float2 poissonDisk[4] = {
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
    float2(-0.094184101, -0.92938870), float2(0.34495938, 0.29387760)};

static const float pointLightShadowEffect = 0.2;
static const float shadowEffect           = 0.6;
static const float ambient                = 0.3;

void main(float3 inPosition
        : POSITION, float3        inNormal
        : NORMAL, float2          inUV
        : UV, out float4          outPosition
        : SV_POSITION, out float3 outNormal
        : NORMALOUT, out float2   outUV
        : UVOUT)
{

    float4x4 mv  = mul(model, view);
    float4x4 mvp = mul(mv, projection);
    outPosition  = mul(float4(inPosition, 1.0f), mvp);
    outUV        = inUV;
    outNormal    = mul(float4(inNormal, 1.0f), normalMatrix).rgb;
}
