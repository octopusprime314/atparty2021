
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

void main(uint id
        : SV_VERTEXID, out float4 outPosition
        : SV_POSITION, out float2 outUV
        : UVOUT, out float3       outPositonInverseMV
        : POSOUT)
{

    // there are five possible fire types in the pixel shader
    // choosing which fire type is in intervals of 0.2 in the u coordinate
    // and the v coordinates will always be between 0 and 0.5 for now
    float  minU   = fireType * 0.2f;
    float  maxU   = minU + 0.2f;
    float3 vertex = float3(0.0, 0.0, 0.0);

    if (id == 0)
    {
        vertex = float3(-1.0, 1.0, 0.0);
        outUV  = float2(minU, 0.5);
    }
    else if (id == 1)
    {
        vertex = float3(-1.0, -1.0, 0.0);
        outUV  = float2(minU, 0.0);
    }
    else if (id == 2)
    {
        vertex = float3(1.0, 1.0, 0.0);
        outUV  = float2(maxU, 0.5);
    }
    else if (id == 3)
    {
        vertex = float3(1.0, -1.0, 0.0);
        outUV  = float2(maxU, 0.0);
    }

    // The vertex is first transformed by the model and world, then
    // the view/camera and finally the projection matrix
    // The order in which transformation matrices affect the vertex
    // is in the order from right to left
    float4x4 inverse         = mul(inverseViewNoTrans, model);
    float4x4 imv             = mul(inverse, view);
    float4x4 imvp            = mul(imv, projection);
    float4   transformedVert = mul(float4(vertex.xyz, 1.0f), imvp);
    outPosition              = transformedVert;
    outPositonInverseMV      = mul(float4(vertex.xyz, 1.0), imv).xyz;
}
