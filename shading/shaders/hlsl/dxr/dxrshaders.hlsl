
struct Payload
{
    float4 color;
};

struct Attributes
{
    float2 barycentrics;
};

[shader("raygeneration")]
void GenericRaygen()
{

    //TraceRay(
    //    MyAccelerationStructure,
    //    SceneConstants.RayFlags,
    //    SceneConstants.InstanceInclusionMask,
    //    SceneConstants.RayContributionToHitGroupIndex,
    //    SceneConstants.MultiplierForGeometryContributionToHitGroupIndex,
    //    SceneConstants.MissShaderIndex,
    //    myRay,
    //    payload);
}

[shader("anyhit")]
void GenericAnyHit(inout Payload payload, in Attributes attr)
{

}

[shader("closesthit")]
void GenericClosestHit(inout Payload payload, in Attributes attr)
{

}

[shader("miss")]
void GenericMiss(inout Payload payload)
{

}