// Object Declarations
Texture2D deferredTexture : register(t0);
Texture2D velocityTexture : register(t1);
sampler   textureSampler : register(s0);


float4 main(float4 posH : SV_POSITION, float2 uv : UVOUT) : SV_Target
{

    float4 result = float4(0.0, 0.0, 0.0, 0.0);
    // Divide by 8.0 to prevent overblurring
    float2 velocityVector = velocityTexture.Sample(textureSampler, uv).xy / 8.0;
    float2 texCoords      = uv;

    result += deferredTexture.Sample(textureSampler, texCoords) * 0.4;
    texCoords -= velocityVector;
    result += deferredTexture.Sample(textureSampler, texCoords) * 0.3;
    texCoords -= velocityVector;
    result += deferredTexture.Sample(textureSampler, texCoords) * 0.2;
    texCoords -= velocityVector;
    result += deferredTexture.Sample(textureSampler, texCoords) * 0.1;

    return float4(float3(result.rgb), 1.0);
}