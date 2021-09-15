Texture2D inDepthTexture : register(t0);
SamplerState bilinearWrap : register(s0);

struct PixelOut
{
    float depth : SV_Depth;
};

PixelOut main(float4 position : SV_POSITION, float2 uv : UVOUT)
{
    PixelOut pixel = {inDepthTexture.Sample(bilinearWrap, uv).r};
    return pixel;
}