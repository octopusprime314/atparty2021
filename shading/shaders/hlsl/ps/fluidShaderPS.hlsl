// Object Declarations
Texture2D noiseTexture : register(t0);
sampler   textureSampler : register(s0);

cbuffer objectData : register(b0)
{
    float4x4 model;
    float    time;
    float3   lowLevelColor;
    float3   deepLevelColor;
    float3   specularColor;
}

cbuffer globalData : register(b1)
{
    float4x4 inverseViewNoTrans;
    float4x4 projection;
    float4x4 view;
    float4x4 normal;
}

static const float coastToWaterFadeDepth = 0.10;
static const float largeWaveHeight       = 0.50; // change to adjust the "heavy" waves
static const float largeWaveSize         = 4.;   // factor to adjust the large wave size
static const float smallWaveHeight       = .6;   // change to adjust the small random waves
static const float smallWaveSize         = .5;   // factor to ajust the small wave size
static const float waterSoftLightFact =
    15.; // range [1..200] (should be << smaller than glossy-fact)
static const float waterGlossyLightFact = 120.; // range [1..200]
static const float particleAmount       = 70.;

// calculate random value
float hash(float n) { return frac(sin(n) * 43758.5453123); }

// 2d noise function
float noise1(in float2 x)
{
    float2 p = floor(x);
    float2 f = smoothstep(0.0, 1.0, frac(x));
    float  n = p.x + p.y * 57.0;
    return lerp(lerp(hash(n + 0.0), hash(n + 1.0), f.x), lerp(hash(n + 57.0), hash(n + 58.0), f.x),
                f.y);
}

float noise(float2 p)
{
    return noiseTexture.Sample(textureSampler, p * float2(1. / 256., 1. / 256.)).x;
}

float heightMap(float2 p)
{

#if USETEXTUREHEIGHT
    float f = 0.15 + textureLod(iChannel2, p * 0.6, 0.0).r * 2.;
#else
    float2x2 m = float2x2(0.9563 * 1.4, -0.2924 * 1.4, 0.2924 * 1.4, 0.9563 * 1.4);
    p          = p * 6.;
    float f;
    f = 0.6000 * noise1(p);
    p = mul(p, m) * 1.1;
    f += 0.2500 * noise1(p);
    p = mul(p, m) * 1.32;
    f += 0.1666 * noise1(p);
    p = mul(p, m) * 1.11;
    f += 0.0834 * noise(p);
    p = mul(p, m) * 1.12;
    f += 0.0634 * noise(p);
    p = mul(p, m) * 1.13;
    f += 0.0444 * noise(p);
    p = mul(p, m) * 1.14;
    f += 0.0274 * noise(p);
    p = mul(p, m) * 1.15;
    f += 0.0134 * noise(p);
    p = mul(p, m) * 1.16;
    f += 0.0104 * noise(p);
    p = mul(p, m) * 1.17;
    f += 0.0084 * noise(p);

    const float FLAT_LEVEL = 0.525;
    if (f < FLAT_LEVEL)
    {
        f = f;
    }
    else
    {
        // makes a smooth coast-increase
        f = pow((f - FLAT_LEVEL) / (1. - FLAT_LEVEL), 2.) * (1. - FLAT_LEVEL) * 2.0 + FLAT_LEVEL;
    }
#endif
    return clamp(f, 0., 10.);
}

static const float2x2 m = float2x2(0.72, -1.60, 1.60, 0.72);

float waterMap(float2 p, float height)
{

    float2 p2     = p * largeWaveSize;
    float2 shift1 = 0.001 * float2(time * 160.0 * 2.0, time * 120.0 * 2.0);
    float2 shift2 = 0.001 * float2(time * 190.0 * 2.0, -time * 130.0 * 2.0);

    // coarse crossing 'ocean' waves...
    float f = 0.6000 * noise(p);
    f += 0.2500 * noise(mul(p, m));
    f += 0.1666 * noise(mul(mul(p, m), m));
    float wave =
        sin(p2.x * 0.622 + p2.y * 0.622 + shift2.x * 4.269) * largeWaveHeight * f * height * height;

    p *= smallWaveSize;
    f         = 0.;
    float amp = 1.0;
    float s   = 0.5;

    for (int i = 0; i < 9; i++)
    {
        p = mul(p, m) * .947;
        f -= amp * abs(sin((noise(p + shift1 * s) - .5) * 2.));
        amp = amp * .59;
        s *= -1.329;
    }

    return wave + f * smallWaveHeight;
}

float nautic(float2 p)
{
    p *= 18.;
    float f   = 0.;
    float amp = 1.0;
    float s   = 0.5;
    for (int i = 0; i < 3; i++)
    {
        p = mul(p, m) * 1.2;
        f += amp * abs(smoothstep(0., 1., noise(p + time * s)) - .5);
        amp *= 0.5;
        s *= -1.227;
    }
    return pow(1. - f, 5.);
}

float particles(float2 p)
{

    p *= 200.0f;
    float f   = 0.0f;
    float amp = 1.0f;
    float s   = 1.5f;
    for (int i = 0; i < 3; i++)
    {
        p = mul(p, m) * 1.2;
        f += amp * noise(p + time * s);
        amp = amp * .5;
        s *= -1.227;
    }
    return pow(f * 0.35, 7.0) * particleAmount;
}

struct MRT
{
    float4 color : SV_Target0;
    float4 normal : SV_Target1;
    float4 id : SV_Target2;
};

MRT main(float4 posH : SV_POSITION, float2 iUV : UVOUT)
{

    MRT output;

    float3 col = float3(1.0, 1.0, 1.0);

    float3 light = float3(0.0, 0.0, 0.0); // position of the sun is in the center of the scene....
    float2 uv    = iUV - float2(0.5, 0.5);
    float  WATER_LEVEL = 0.94; // Water level (range: 0.0 - 2.0)
    float  height      = heightMap(uv);
    float  waveheight  = clamp(WATER_LEVEL * 3. - 1.5, 0., 1.);
    float  level = WATER_LEVEL + .2 * waterMap(uv * 15. + float2(time * .1, time * .1), waveheight);
    float2 dif   = float2(.0, .01);
    float2 pos   = uv * 15. + float2(time * .01, time * .01);
    float  h1    = waterMap(pos - dif, waveheight);
    float  h2    = waterMap(pos + dif, waveheight);
    float  h3    = waterMap(pos - dif.yx, waveheight);
    float  h4    = waterMap(pos + dif.yx, waveheight);
    float3 normwater =
        normalize(float3(h3 - h4, h1 - h2, .125)); // norm-vector of the 'bumpy' water-plane
    uv += normwater.xy * .002 * (level - height);
    float  coastfade  = 1.0;
    float  coastfade2 = 1.0;
    float  intensity  = col.r * .2126 + col.g * .7152 + col.b * .0722;
    float3 watercolor =
        lerp(lowLevelColor * intensity, deepLevelColor, smoothstep(0., 1., coastfade2));
    float3 r0        = float3(uv, WATER_LEVEL);
    float3 rd        = normalize(r0 - light); // ray-direction to the light from water-position
    float  grad      = dot(normwater, rd);    // dot-product of norm-vector and light-direction
    float  specular  = pow(grad, waterSoftLightFact);   // used for soft highlights
    float  specular2 = pow(grad, waterGlossyLightFact); // used for glossy highlights
    float  gradpos   = dot(float3(0., 0., 1.), rd);
    // used for diffusity (some darker corona around light's specular reflections...)
    float specular1  = smoothstep(0., 1., pow(gradpos, 5.));
    float watershade = 1.0; // test against shadow here
    watercolor *= 2.2 + watershade;
    watercolor += (.2 + .8 * watershade) * ((grad - 1.0) * .5 + specular) * .25;
    watercolor /= (1. + specular1 * 1.25);
    watercolor += watershade * specular2 * specularColor;
    watercolor += watershade * coastfade * (1. - coastfade2) *
                  (float3(.5, .6, .7) * nautic(uv) + float3(1., 1., 1.) * particles(uv));
    col = lerp(col, watercolor, coastfade);
    // switch z and y normal
    // Transform normal coordinate in with the normal matrix
    normwater     = float3((mul(float4(normwater, 0.0), normal)).xyz);
    output.color  = float4(col, 1.0);
    output.normal = float4(normalize(normwater), 1.0);
    output.id     = float4(0.0, 0.0, 0.0, 0.0);

    return output;
}
