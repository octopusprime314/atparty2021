#include "DXRStateObject.h"
#include "D3D12RaytracingHelpers.hpp"
#include "DXLayer.h"

DXRStateObject::DXRStateObject(ComPtr<ID3D12RootSignature> rootSignature)
{
    auto device = DXLayer::instance()->getDevice();

    ComPtr<ID3D12Device5> dxrDevice;
    device->QueryInterface(IID_PPV_ARGS(&dxrDevice));

   
    // Create ray tracing shaders
    std::string name = SHADERS_LOCATION + "hlsl/dxr/dxrshaders.hlsl";

    HLSLShader shader(name, true);

    ComPtr<IDxcBlob> pResultBlob;
    std::vector<uint8_t> stream;

    shader.buildDXC(pResultBlob, shader.getName(), L"cs_6_5", L"main", stream);

    CD3D12_STATE_OBJECT_DESC raytracingPipeline{D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};
    // This contains the shaders and their entrypoints for the state object.
    // Since shaders are not considered a subobject, they need to be passed in via DXIL library
    // subobjects.
    auto                  lib = raytracingPipeline.CreateSubobject<CD3D12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE((unsigned char*)pResultBlob->GetBufferPointer(), pResultBlob->GetBufferSize());
    lib->SetDXILLibrary(&libdxil);

    auto closestHitShaderImport = L"GenericClosestHit";
    auto anyHitShaderImport     = L"GenericAnyHit";
    auto hitGroupExport         = L"GenericHitGroup";
    auto missShaderImport       = L"GenericMiss";
    auto raygenImport           = L"GenericRaygen";

    // Define which shader exports to surface from the library.
    // If no shader exports are defined for a DXIL library subobject, all shaders will be surfaced.
    // In this sample, this could be omitted for convenience since the sample uses all shaders in
    // the library.
    {
        lib->DefineExport(raygenImport);
        lib->DefineExport(closestHitShaderImport);
        lib->DefineExport(anyHitShaderImport);
        lib->DefineExport(missShaderImport);
    }

    // Triangle hit group
    // A hit group specifies closest hit, any hit and intersection shaders to be executed when a ray
    // intersects the geometry's triangle/AABB. In this sample, we only use triangle geometry with a
    // closest hit shader, so others are not set.
    auto hitGroup = raytracingPipeline.CreateSubobject<CD3D12_HIT_GROUP_SUBOBJECT>();
    hitGroup->SetClosestHitShaderImport(closestHitShaderImport);
    hitGroup->SetHitGroupExport(hitGroupExport);
    hitGroup->SetAnyHitShaderImport(anyHitShaderImport);
    hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    // Shader config
    // Defines the maximum sizes in bytes for the ray payload and attribute structure.
    auto shaderConfig = raytracingPipeline.CreateSubobject<CD3D12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    UINT payloadSize   = 4 * sizeof(float); // float4 color
    UINT attributeSize = 2 * sizeof(float); // float2 barycentrics
    shaderConfig->Config(payloadSize, attributeSize);

    // Global root signature
    // This is a root signature that is shared across all raytracing shaders invoked during a
    // DispatchRays() call.
    auto globalRootSignature = raytracingPipeline.CreateSubobject<CD3D12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    globalRootSignature->SetRootSignature(rootSignature.Get());

    // Pipeline config
    // Defines the maximum TraceRay() recursion depth.
    auto pipelineConfig = raytracingPipeline.CreateSubobject<CD3D12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    
    // PERFOMANCE TIP: Set max recursion depth as low as needed
    // as drivers may apply optimization strategies for low recursion depths.
    UINT maxRecursionDepth = 1; // ~ primary rays only.
    
    pipelineConfig->Config(maxRecursionDepth);

    dxrDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&_dxrStateObject));
}
