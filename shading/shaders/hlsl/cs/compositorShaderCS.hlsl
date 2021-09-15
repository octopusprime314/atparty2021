#include "../../hlsl/include/NRD.hlsl"

Texture2D reflectionSRV : register(t0, space0);
Texture2D sunLightSRV   : register(t1, space0);
Texture2D shadowSRV  : register(t2, space0);
Texture2D colorHistorySRV : register(t3, space0);

RWTexture2D<float4> pathTracerUAV       : register(u0);

SamplerState bilinearWrap : register(s0);

cbuffer globalData : register(b0)
{
    float2 screenSize;
    int    reflectionMode;
    int    shadowMode;
    int    viewMode;
}

[numthreads(8, 8, 1)]

void main(int3 threadId : SV_DispatchThreadID,
        int3 threadGroupThreadId : SV_GroupThreadID)
{
    float4 reflection    = reflectionSRV[threadId.xy];
    float4 sunLighting   = sunLightSRV[threadId.xy];

    /*if (viewMode == 0)
    {*/
        pathTracerUAV[threadId.xy] = (reflection + sunLighting)/* * 0.000001*/;
        //pathTracerUAV[threadId.xy] += float4(SIGMA_BackEnd_UnpackShadow(shadowSRV[threadId.xy]).x, 0,0,0);
    //}
    //else
    //{
    //    //pathTracerUAV[threadId.xy] = (reflection + sunLighting) * occlusionSRV[threadId.xy].r;
    //    //pathTracerUAV[threadId.xy] = occlusionSRV[threadId.xy].r;
    //    pathTracerUAV[threadId.xy] = colorHistorySRV[threadId.xy];
    //}
}