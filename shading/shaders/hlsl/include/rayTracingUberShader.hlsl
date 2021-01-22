struct SceneConstantBuffer
{
    float4x4 projectionToWorld;
    float4   cameraPosition;
    float4   lightDirection;
    float4x4 projection;
    float4x4 lightView;
};
struct Vertex
{
    float3 position;
    float3 normal;
};
struct Viewport
{
    float left;
    float top;
    float right;
    float bottom;
};
struct RayGenConstantBuffer
{
    Viewport viewport;
    Viewport stencil;
};
struct RayPayload
{
    float4 color;
};

RaytracingAccelerationStructure     Scene : register(t0, space0);
RWTexture2D<float4>                 RenderTarget : register(u0);
ByteAddressBuffer                   Indices : register(t1, space0);
StructuredBuffer<Vertex>            Vertices : register(t2, space0);
ConstantBuffer<SceneConstantBuffer> sceneCB : register(b0);

// Retrieve hit world position.
float3 HitWorldPosition() { return WorldRayOrigin() + RayTCurrent() * WorldRayDirection(); }

// Retrieve attribute at a hit position interpolated from vertex attributes using the hit's
// barycentrics.
float3 HitAttribute(float3 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr)
{
    return vertexAttribute[0] + attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
           attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D
// grid.
inline void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
    float2 xy        = ((float2)index) + 0.5f; // center in the middle of the pixel.
    float2 screenPos = (xy / DispatchRaysDimensions().xy) * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a ray.  
    float4 world = mul(float4(screenPos, 0, 1), sceneCB.projectionToWorld);

    world.xyz /= world.w;

    // Orthographic matrix
    origin    = world.xyz;
    direction = sceneCB.lightDirection.xyz;
}

[shader("raygeneration")] void MyRaygenShader() {
    float3 rayDir;
    float3 origin;

    GenerateCameraRay(DispatchRaysIndex(), origin, rayDir);

    // Trace the ray.
    // Set the ray's extents.
    RayDesc ray;
    ray.Origin    = origin;
    ray.Direction = rayDir;

    // Set TMin to a non-zero small val(ue to avoid aliasing issues due to floating - point errors.
    // TMin should be kept small to prevent missing geometry at close contact areas.
    ray.TMin           = 0.001;
    ray.TMax           = 100000000.0f;
    RayPayload payload = {float4(0, 0, 0, 0)};

    TraceRay(Scene, RAY_FLAG_FORCE_OPAQUE, ~0, 0, 0, 0, ray, payload);

    // Write the raytraced color to the output texture.
    RenderTarget[DispatchRaysIndex().xy] = payload.color;
}

[shader("closesthit")] void MyClosestHitShader(inout RayPayload                         payload,
                                              in BuiltInTriangleIntersectionAttributes attr)
{
    float3   hitPosition   = HitWorldPosition();
    float4x4 lightViewProj = mul(sceneCB.lightView, sceneCB.projection);
    float4   clipSpace     = mul(float4(hitPosition, 1), lightViewProj);
    payload.color          = float4(clipSpace.z, clipSpace.z, clipSpace.z, 1.0);
}

[shader("miss")]
void MyMissShader(inout RayPayload payload) {
    float4 background = float4(0.0f, 1.0f, 0.0f, 1.0f);
    payload.color     = background;
}
