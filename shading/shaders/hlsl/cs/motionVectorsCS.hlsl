#include "../include/structs.hlsl"

Texture2D     currPositionSRV    : register(t0, space0);
Buffer<float> prevInstanceWorldMatrixTransforms : register(t1, space0);
Buffer<float> instanceWorldToObjectSpaceMatrixTransforms : register(t2, space0);
//StructuredBuffer<Vertex> vertexBuffer[] : register(t1, space1);

RWTexture2D<float4> motionVectorsUAV : register(u0);
//RWTexture2D<float4> prevPosUAV       : register(u1);
//RWTexture2D<float4> currPosUAV       : register(u2);


cbuffer globalData : register(b0)
{
    float4x4 inverseView;
    float4x4 inverseProj;
    float4x4 prevFrameViewProj;

    float4x4 prevFrameView;
    float4x4 currFrameView;

    //float3x3 prevInstanceNormalMatrixTransforms[256];
    float4   prevFrameCameraPosition;
    float2   screenSize;
}

#define FLT_MAX asfloat(0x7F7FFFFF)


float4x3 get4x3Matrix(Buffer<float> buffer, int instanceBufferIndex)
{

    int matrixOffset = instanceBufferIndex * 12;

    float4x3 model = {float3(buffer[NonUniformResourceIndex(matrixOffset)],
                             buffer[NonUniformResourceIndex(matrixOffset + 4)],
                             buffer[NonUniformResourceIndex(matrixOffset + 8)]),
                      float3(buffer[NonUniformResourceIndex(matrixOffset + 1)],
                             buffer[NonUniformResourceIndex(matrixOffset + 5)],
                             buffer[NonUniformResourceIndex(matrixOffset + 9)]),
                      float3(buffer[NonUniformResourceIndex(matrixOffset + 2)],
                             buffer[NonUniformResourceIndex(matrixOffset + 6)],
                             buffer[NonUniformResourceIndex(matrixOffset + 10)]),
                      float3(buffer[NonUniformResourceIndex(matrixOffset + 3)],
                             buffer[NonUniformResourceIndex(matrixOffset + 7)],
                             buffer[NonUniformResourceIndex(matrixOffset + 11)])};

    return model;
}

// Generate camera's forward direction ray
inline float3 GenerateForwardCameraRayDirection(in float4x4 projectionToWorldWithCameraAtOrigin)
{
    float2 screenPos = float2(0, 0);

    // Unproject the pixel coordinate into a world positon.
    float4 world = mul(projectionToWorldWithCameraAtOrigin, float4(screenPos, 0, 1));
    return normalize(world.xyz);
}
float2 ClipSpaceToTexturePosition(in float4 clipSpacePosition)
{
    // Perspective divide to get Normal Device Coordinates: {[-1,1], [-1,1], (0, 1]}
    float3 NDCposition = clipSpacePosition.xyz / clipSpacePosition.w;
    // Invert Y for DirectX-style coordinates.
    NDCposition.y = -NDCposition.y;
    // [-1,1] -> [0, 1]
    float2 texturePosition = (NDCposition.xy + 1.0) * 0.5f;

    return texturePosition;
}

float GetPlaneConstant(in float3 planeNormal, in float3 pointOnThePlane)
{
    // Given a plane equation N * P + d = 0
    // d = - N * P
    return -dot(planeNormal, pointOnThePlane);
}

bool IsPointOnTheNormalSideOfPlane(in float3 P, in float3 planeNormal, in float3 pointOnThePlane)
{
    float d = GetPlaneConstant(planeNormal, pointOnThePlane);
    return dot(P, planeNormal) + d > 0;
}

float3 ReflectPointThroughPlane(in float3 P, in float3 planeNormal, in float3 pointOnThePlane)
{
    //           |
    //           |
    //  P ------ C ------ R
    //           |
    //           |
    // Given a point P, plane with normal N and constant d, the projection point C of P onto plane
    // is: C = P + t*N
    //
    // Then the reflected point R of P through the plane can be computed using t as:
    // R = P + 2*t*N

    // Given C = P + t*N, and C lying on the plane,
    // C*N + d = 0
    // then
    // C = - d/N
    // -d/N = P + t*N
    // 0 = d + P*N + t*N*N
    // t = -(d + P*N) / N*N

    float  d = GetPlaneConstant(planeNormal, pointOnThePlane);
    float3 N = planeNormal;
    float  t = -(d + dot(P, N)) / dot(N, N);

    return P + 2 * t * N;
}

// Reflects a point across a planar mirror.
// Returns FLT_MAX if the input point is already behind the mirror.
float3 ReflectFrontPointThroughPlane(in float3 p, in float3 mirrorSurfacePoint,
                                     in float3 mirrorNormal)
{
    if (!IsPointOnTheNormalSideOfPlane(p, mirrorNormal, mirrorSurfacePoint))
    {
        return FLT_MAX;
    }

    return ReflectPointThroughPlane(p, mirrorNormal, mirrorSurfacePoint);
}

// Calculate a texture space motion vector from previous to current frame.
float2 CalculateMotionVector(in float3 hitPosition, out float depth, in uint2 threadId)
{
    float3 prevHitViewPosition = hitPosition + prevFrameCameraPosition.xyz;
    float3 prevCameraDirection = GenerateForwardCameraRayDirection(mul(inverseView, inverseProj));
    //depth                      = dot(prevHitViewPosition, prevCameraDirection);
    depth = length(currPositionSRV[threadId].xyz + prevFrameCameraPosition.xyz);

    // Calculate screen space position of the hit in the previous frame.
    float4 prevClipSpacePosition = mul(float4(hitPosition, 1.0), prevFrameViewProj);
    float2 prevTexturePosition   = ClipSpaceToTexturePosition(prevClipSpacePosition);

    // Center in the middle of the pixel.
    float2 xy                  = float2(threadId.xy) + 0.5f;
    float2 currTexturePosition = xy / screenSize;

    return prevTexturePosition - currTexturePosition;
}

float3 GetWorldHitPositionInPreviousFrame(in float3 hitObjectPosition, in uint instanceIndex/*,
                                          in uint3 vertexIndices,
                                          in BuiltInTriangleIntersectionAttributes attr,
                                          out float3x4 prevInstanceTransform*/)
{
    // Calculate hit object position of the hit in the previous frame.
    float3 prevHitObjectPosition;

    // Handle vertex deltas if updateable bvh, TODO
    if (false)
    {
        //float3 prevVertices[3] = {vertexBuffer[0].pos,
        //                          vertexBuffer[1].pos,
        //                          vertexBuffer[2].pos};
        //
        //prevHitObjectPosition = GetHitPosition(_vertices, attr);
    }
    // non-vertex animated geometry
    else
    {
        prevHitObjectPosition = hitObjectPosition;
    }

    // Transform the hit object position to world space.
    float4x3 prevInstanceTransform = get4x3Matrix(prevInstanceWorldMatrixTransforms, instanceIndex);
    float3   prevPosition = mul(float4(prevHitObjectPosition, 1.0), prevInstanceTransform).xyz;

    return prevPosition;
}

[numthreads(8, 8, 1)]

void main(int3 threadId : SV_DispatchThreadID)
{
    if (currPositionSRV[threadId.xy].w == -1.0)
    {
        motionVectorsUAV[threadId.xy].xyz = float3(0.0, 0.0, 0.0);
        return;
    }
    // Calculates motion vectors for non animated vertices aka doesn't support updateable/refit bvh
    uint   instanceIndex     = (uint)currPositionSRV[threadId.xy].w;
    float3 hitObjectPosition = mul(float4(currPositionSRV[threadId.xy].xyz, 1.0),
            get4x3Matrix(instanceWorldToObjectSpaceMatrixTransforms, instanceIndex));

    float3 prevVirtualHitPosition = GetWorldHitPositionInPreviousFrame(hitObjectPosition, instanceIndex);

    float  depth;
    float2 motionVector2D = CalculateMotionVector(prevVirtualHitPosition, depth, threadId.xy);

    float4 prevFramePos = float4(prevVirtualHitPosition.xyz, 1.0);
    float4 currFramePos = float4(currPositionSRV[threadId.xy].xyz, 1.0);
    float3 worldSpaceMotionVector3D = prevFramePos.xyz - currFramePos.xyz;

    motionVectorsUAV[threadId.xy].xyz = worldSpaceMotionVector3D;
}
