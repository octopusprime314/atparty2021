// Object Declarations
sampler   textureSampler : register(s0);
Texture2D textureMap : register(t0);
Texture2D tex0 : register(t1);
Texture2D tex1 : register(t2);
Texture2D tex2 : register(t3);
Texture2D tex3 : register(t4);
Texture2D alphatex0 : register(t5);

cbuffer objectData : register(b0)
{
    float4x4 model;
    int      id;
    int      isLayeredTexture;
    int      primitiveOffset;
}
cbuffer globalData : register(b1)
{
    float4x4 prevModel;
    float4x4 prevView;
    float4x4 view;
    float4x4 projection;
    float4x4 normal;
}

void main(in float3 inPosition
        : POSITION, in float3     inNormal
        : NORMAL, in float2       inUV
        : UV, out float4          outPosition
        : SV_POSITION, out float3 outNormal
        : NORMALOUT, out float2   outUV
        : UVOUT)
{

    float4x4 mv  = mul(model, view);
    float4x4 mvp = mul(mv, projection);
    outPosition  = mul(float4(inPosition, 1.0f), mvp);
    outNormal    = mul(float4(inNormal, 1.0f), normal).rgb;
    outUV        = float2(inUV.x, 1.0f - inUV.y);
}


