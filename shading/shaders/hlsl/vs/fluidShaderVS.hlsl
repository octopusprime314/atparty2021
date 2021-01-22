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

void main(uint id : SV_VERTEXID, out float4 outPosition : SV_POSITION, out float2 outUV : UVOUT)
{
    float3 vertex = float3(0.0, 0.0, 0.0);

    if (id == 0)
    {
        vertex = float3(-1.0, 1.0, 0.0);
        outUV  = float2(0.0, 1.0);
    }
    else if (id == 1)
    {
        vertex = float3(-1.0, -1.0, 0.0);
        outUV  = float2(0.0, 0.0);
    }
    else if (id == 2)
    {
        vertex = float3(1.0, 1.0, 0.0);
        outUV  = float2(1.0, 1.0);
    }
    else if (id == 3)
    {
        vertex = float3(1.0, -1.0, 0.0);
        outUV  = float2(1.0, 0.0);
    }

    // The vertex is first transformed by the model and world, then
    // the view/camera and finally the projection matrix
    // The order in which transformation matrices affect the vertex
    // is in the order from right to left
    float4x4 mv              = mul(model, view);
    float4x4 mvp             = mul(mv, projection);
    float4   transformedVert = mul(float4(vertex.xyz, 1.0f), mvp);
    outPosition              = transformedVert;
}

