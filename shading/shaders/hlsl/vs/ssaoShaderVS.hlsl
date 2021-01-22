sampler   textureSampler : register(s0);
Texture2D normalTexture : register(t0);
Texture2D noiseTexture : register(t1);
Texture2D depthTexture : register(t2);

cbuffer globalData : register(b0)
{
    float3   kernel[64];
    float4x4 projection;
    float4x4 projectionToViewMatrix;
}

void main(uint id : SV_VERTEXID, out float4 outPosition : SV_POSITION, out float2 outUV : UVOUT)
{

    outPosition.x = (float)(id / 2) * 4.0 - 1.0;
    outPosition.y = (float)(id % 2) * 4.0 - 1.0;
    outPosition.z = 0.0;
    outPosition.w = 1.0;

    outUV.x = (float)(id / 2) * 2.0;
    outUV.y = 1.0 - (float)(id % 2) * 2.0;
}
