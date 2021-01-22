Texture2D inDepthTexture : register(t0);
sampler   textureSampler : register(s0);

struct PixelOut
{
    float depth : SV_Depth;
};

PixelOut main(float4 position : SV_POSITION, float2 uv : UVOUT)
{
    PixelOut pixel = {inDepthTexture.Sample(textureSampler, uv).r};
    return pixel;
}