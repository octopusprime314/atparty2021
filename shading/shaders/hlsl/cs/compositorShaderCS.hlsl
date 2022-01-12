#include "../../hlsl/include/NRD.hlsl"

Texture2D indirectLightRaysHistoryBufferSRV : register(t0, space0);
Texture2D indirectSpecularLightRaysHistoryBufferSRV : register(t1, space0);
Texture2D diffusePrimarySurfaceModulation : register(t2, space0);
Texture2D specularPrimarySurfaceModulation : register(t3, space0);

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
    float4 specularUnpacked =
        REBLUR_BackEnd_UnpackRadiance(indirectSpecularLightRaysHistoryBufferSRV[threadId.xy]);
    float4 diffuseUnpacked =
        REBLUR_BackEnd_UnpackRadiance(indirectLightRaysHistoryBufferSRV[threadId.xy]);

    diffuseUnpacked *= diffusePrimarySurfaceModulation[threadId.xy];
    //specularUnpacked *= specularPrimarySurfaceModulation[threadId.xy];

    const float gamma = 2.2;
    // const float exposure = 0.01;
    const float exposure = 1.0;
    // reinhard tone mapping
    float3 mapped = float3(1.0, 1.0, 1.0) - exp(-((specularUnpacked + diffuseUnpacked).xyz) * exposure);
    // gamma correction
    mapped = pow(mapped, float3(1.0, 1.0, 1.0) / float3(gamma, gamma, gamma));

    pathTracerUAV[threadId.xy] = float4(mapped, 1.0);
}