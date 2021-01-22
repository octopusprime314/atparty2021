Texture2D inTexture : register(t0);
sampler   textureSampler : register(s0);

cbuffer globalData : register(b0) { int conversionType; }

struct PixelOut
{
    float4 color : SV_Target0;
};

PixelOut main(float4 position : SV_POSITION, float2 uv : UVOUT)
{
    PixelOut pixel = {0.0, 0.0, 0.0, 1.0};

    if (conversionType == 0)
    {
        pixel.color = float4(inTexture.Sample(textureSampler, uv).rgb, 1.0);
    }
    else if (conversionType == 1)
    {
        pixel.color = float4(inTexture.Sample(textureSampler, uv).rrr, 1.0);
    }
    return pixel;
}