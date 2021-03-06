
// Object Declarations
Texture2D          readTexture : register(t0);
RWTexture2D<float> writeTexture : register(u0);

cbuffer objectData : register(b0)
{
    float4x4 model;
    float    time;
    int      fireType;
    float3   fireColor;
}

cbuffer globalData : register(b1)
{
    float4x4 inverseViewNoTrans;
    float4x4 projection;
    float4x4 view;
}


// procedural noise from IQ
float2 hash(float2 p)
{
    p = float2(dot(p, float2(127.1, 311.7)), dot(p, float2(269.5, 183.3)));

    return -1.0 + 2.0 * frac(sin(p) * 43758.5453123);
}

float noise(in float2 p)
{
    const float K1 = 0.366025404; // (sqrt(3)-1)/2;
    const float K2 = 0.211324865; // (3-sqrt(3))/6;

    float2 i = floor(p + (p.x + p.y) * K1);

    float2 a = p - i + (i.x + i.y) * K2;
    float2 o = (a.x > a.y) ? float2(1.0, 0.0) : float2(0.0, 1.0);
    float2 b = a - o + K2;
    float2 c = a - 1.0 + 2.0 * K2;

    float3 h = max(0.5 - float3(dot(a, a), dot(b, b), dot(c, c)), 0.0);

    float3 n =
        h * h * h * h * float3(dot(a, hash(i + 0.0)), dot(b, hash(i + o)), dot(c, hash(i + 1.0)));

    return dot(n, float3(70.0, 70.0, 70.0));
}

float fbm(float2 uv)
{
    float2x2 m = float2x2(1.6, 1.2, -1.2, 1.6);
    float    f = 0.5000 * noise(uv);
    uv         = mul(uv, m);
    f += 0.2500 * noise(uv);
    uv = mul(uv, m);
    f += 0.1250 * noise(uv);
    uv = mul(uv, m);
    f += 0.0625 * noise(uv);
    uv = mul(uv, m);
    f  = 0.5 + 0.5 * f;
    return f;
}

struct PixelOut
{
    float4 color : SV_Target;
};

PixelOut main(float4 posH : SV_POSITION, float2 iUV : UVOUT, float3 iPos : POSOUT)
{

    PixelOut pixel;

    float2 uv = iUV;
    float2 q  = float2(uv.x, uv.y);

    q.x *= 5.;
    q.y *= 2.;

    float strength = floor(q.x + 1.);
    float T3       = max(3., 1.25 * strength) * time;
    q.x            = (q.x % 1.0) - 0.5;
    q.y -= 0.25;
    float n = fbm(strength * q - float2(0, T3));
    float c =
        1. -
        16. * pow(max(0., length(q * float2(1.8 + q.y * 1.5, .75)) - n * max(0., q.y + .25)), 1.2);
    float c1 = n * c * (1.5 - pow(2.50 * uv.y, 4.));
    c1       = clamp(c1, 0., 1.);

    float3 col = float3(1.5 * c1, 1.5 * c1 * c1 * c1, c1 * c1 * c1 * c1 * c1 * c1);

    float a     = c * (1. - pow((uv.y), 3.));
    pixel.color = float4(lerp(float3(0., 0., 0.), col.xxx * fireColor, a), 1.0);

    // pixel.color = float4(1.0, 0.0, 0.0, 1.0);
    if (!(pixel.color.r > 0.25 || pixel.color.g > 0.25 || pixel.color.b > 0.25))
    {
        discard;
    }
    return pixel;
}
