#include "DXRStateObject.h"
#include "D3D12RaytracingHelpers.hpp"
#include "DXLayer.h"

DXRStateObject::DXRStateObject(ComPtr<ID3D12RootSignature> primaryRaysRootSignature,
                               ComPtr<ID3D12RootSignature> reflectionRaysRootSignature)
{
    auto device = DXLayer::instance()->getDevice();

    ComPtr<ID3D12Device5> dxrDevice;
    device->QueryInterface(IID_PPV_ARGS(&dxrDevice));

    // Create primary ray tracing shaders
    std::string primaryRaysName = SHADERS_LOCATION + "hlsl/dxr/dxrprimaryraysshaders.hlsl";

    HLSLShader primaryRaysShader(primaryRaysName, true);

    ComPtr<IDxcBlob> pResultBlob;
    std::vector<uint8_t> stream;

    primaryRaysShader.buildDXC(pResultBlob, primaryRaysShader.getName(), L"cs_6_5", L"main", stream);

    auto primaryClosestHitShaderImport = L"PrimaryClosestHit";
    auto primaryAnyHitShaderImport     = L"PrimaryAnyHit";
    auto primaryHitGroupExport         = L"PrimaryHitGroup";
    auto primaryMissShaderImport       = L"PrimaryMiss";
    auto primaryRaygenImport           = L"PrimaryRaygen";

    _buildStateObject(primaryRaygenImport,
                      primaryMissShaderImport,
                      primaryHitGroupExport,
                      primaryClosestHitShaderImport,
                      primaryAnyHitShaderImport,
                      _primaryRaysRayGenShaderTable,
                      _primaryRaysMissShaderTable,
                      _primaryRaysHitGroupShaderTable,
                      pResultBlob,
                      primaryRaysRootSignature,
                      _dxrPrimaryRaysStateObject);

    // Create primary ray tracing shaders
    std::string reflectionRaysName = SHADERS_LOCATION + "hlsl/dxr/dxrreflectionraysshaders.hlsl";

    HLSLShader reflectionRaysShader(reflectionRaysName, true);

    reflectionRaysShader.buildDXC(pResultBlob, reflectionRaysShader.getName(), L"cs_6_5", L"main", stream);

    auto reflectionClosestHitShaderImport = L"ReflectionClosestHit";
    auto reflectionAnyHitShaderImport     = L"ReflectionAnyHit";
    auto reflectionHitGroupExport         = L"ReflectionHitGroup";
    auto reflectionMissShaderImport       = L"ReflectionMiss";
    auto reflectionRaygenImport           = L"ReflectionRaygen";

    _buildStateObject(reflectionRaygenImport,
                      reflectionMissShaderImport,
                      reflectionHitGroupExport,
                      reflectionClosestHitShaderImport,
                      reflectionAnyHitShaderImport,
                      _reflectionRaysRayGenShaderTable,
                      _reflectionRaysMissShaderTable,
                      _reflectionRaysHitGroupShaderTable, 
                      pResultBlob,
                      reflectionRaysRootSignature,
                      _dxrReflectionRaysStateObject);
}

void DXRStateObject::_buildStateObject(const wchar_t*              raygenImport,
                                       const wchar_t*              missShaderImport,
                                       const wchar_t*              hitGroupExport,
                                       const wchar_t*              closestHitImport,
                                       const wchar_t*              anyHitImport,
                                       ComPtr<ID3D12Resource>&     rayGenShaderTableResource,
                                       ComPtr<ID3D12Resource>&     missShaderTableResource,
                                       ComPtr<ID3D12Resource>&     hitGroupShaderTableResource,
                                       ComPtr<IDxcBlob>            pResultBlob,
                                       ComPtr<ID3D12RootSignature> rootSignature,
                                       ComPtr<ID3D12StateObject>&  stateObject)
{
    auto device = DXLayer::instance()->getDevice();

    ComPtr<ID3D12Device5> dxrDevice;
    device->QueryInterface(IID_PPV_ARGS(&dxrDevice));

    CD3D12_STATE_OBJECT_DESC raytracingPipeline{D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};
    // This contains the shaders and their entrypoints for the state object.
    // Since shaders are not considered a subobject, they need to be passed in via DXIL library
    // subobjects.
    auto                  lib = raytracingPipeline.CreateSubobject<CD3D12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE libdxil = CD3DX12_SHADER_BYTECODE((unsigned char*)pResultBlob->GetBufferPointer(), pResultBlob->GetBufferSize());
    lib->SetDXILLibrary(&libdxil);

    // Define which shader exports to surface from the library.
    // If no shader exports are defined for a DXIL library subobject, all shaders will be surfaced.
    // In this sample, this could be omitted for convenience since the sample uses all shaders in
    // the library.
    {
        lib->DefineExport(raygenImport);
        lib->DefineExport(closestHitImport);
        lib->DefineExport(anyHitImport);
        lib->DefineExport(missShaderImport);
    }

    // Triangle hit group
    auto primaryHitGroup = raytracingPipeline.CreateSubobject<CD3D12_HIT_GROUP_SUBOBJECT>();
    primaryHitGroup->SetClosestHitShaderImport(closestHitImport);
    primaryHitGroup->SetHitGroupExport(hitGroupExport);
    primaryHitGroup->SetAnyHitShaderImport(anyHitImport);
    primaryHitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

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
    UINT maxRecursionDepth = 10;
    
    pipelineConfig->Config(maxRecursionDepth);

    dxrDevice->CreateStateObject(raytracingPipeline, IID_PPV_ARGS(&stateObject));

    _buildShaderTables(raygenImport, missShaderImport, hitGroupExport, rayGenShaderTableResource,
                       missShaderTableResource, hitGroupShaderTableResource, stateObject);
}

void DXRStateObject::_buildShaderTables(const wchar_t *            raygenImport,
                                        const wchar_t *            missShaderImport,
                                        const wchar_t*             hitGroupExport,
                                        ComPtr<ID3D12Resource>&    rayGenShaderTableResource,
                                        ComPtr<ID3D12Resource>&    missShaderTableResource,
                                        ComPtr<ID3D12Resource>&    hitGroupShaderTableResource,
                                        ComPtr<ID3D12StateObject>& stateObject)
{
    auto device = DXLayer::instance()->getDevice();

    ComPtr<ID3D12Device5> dxrDevice;
    device->QueryInterface(IID_PPV_ARGS(&dxrDevice));

    void* rayGenShaderIdentifier;
    void* missShaderIdentifier;
    void* hitGroupShaderIdentifier;

    auto GetShaderIdentifiers = [&](auto* stateObjectProperties)
    {
        rayGenShaderIdentifier   = stateObjectProperties->GetShaderIdentifier(raygenImport);
        missShaderIdentifier     = stateObjectProperties->GetShaderIdentifier(missShaderImport);
        hitGroupShaderIdentifier = stateObjectProperties->GetShaderIdentifier(hitGroupExport);
    };

    // Get shader identifiers.
    UINT                                         shaderIdentifierSize;
    ComPtr<ID3D12StateObjectPropertiesPrototype> stateObjectProperties;

    stateObject.As(&stateObjectProperties);
    GetShaderIdentifiers(stateObjectProperties.Get());
    shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

    // Ray gen shader table
    {
        UINT        numShaderRecords = 1;
        UINT        shaderRecordSize = shaderIdentifierSize;
        ShaderTable rayGenShaderTable(dxrDevice.Get(), numShaderRecords, shaderRecordSize, L"RayGenShaderTable");
        rayGenShaderTable.pushBack(ShaderRecord(rayGenShaderIdentifier, shaderIdentifierSize));
        rayGenShaderTableResource = rayGenShaderTable.GetResource();
    }

    // Miss shader table
    {
        UINT        numShaderRecords = 1;
        UINT        shaderRecordSize = shaderIdentifierSize;
        ShaderTable missShaderTable(dxrDevice.Get(), numShaderRecords, shaderRecordSize, L"MissShaderTable");
        missShaderTable.pushBack(ShaderRecord(missShaderIdentifier, shaderIdentifierSize));
        missShaderTableResource = missShaderTable.GetResource();
    }

    // Hit group shader table
    {

        UINT        numShaderRecords = 1;
        UINT        shaderRecordSize = shaderIdentifierSize;
        ShaderTable hitGroupShaderTable(dxrDevice.Get(), numShaderRecords, shaderRecordSize, L"HitGroupShaderTable");
        hitGroupShaderTable.pushBack(ShaderRecord(hitGroupShaderIdentifier, shaderIdentifierSize));
        hitGroupShaderTableResource = hitGroupShaderTable.GetResource();
    }
}


void DXRStateObject::dispatchPrimaryRays()
{
    auto cmdList = DXLayer::instance()->getCmdList();

    D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = {};

    dispatchRaysDesc.HitGroupTable.StartAddress             = _primaryRaysHitGroupShaderTable->GetGPUVirtualAddress();
    dispatchRaysDesc.HitGroupTable.SizeInBytes              = _primaryRaysHitGroupShaderTable->GetDesc().Width;
    dispatchRaysDesc.HitGroupTable.StrideInBytes            = dispatchRaysDesc.HitGroupTable.SizeInBytes;
    dispatchRaysDesc.MissShaderTable.StartAddress           = _primaryRaysMissShaderTable->GetGPUVirtualAddress();
    dispatchRaysDesc.MissShaderTable.SizeInBytes            = _primaryRaysMissShaderTable->GetDesc().Width;
    dispatchRaysDesc.MissShaderTable.StrideInBytes          = dispatchRaysDesc.MissShaderTable.SizeInBytes;
    dispatchRaysDesc.RayGenerationShaderRecord.StartAddress = _primaryRaysRayGenShaderTable->GetGPUVirtualAddress();
    dispatchRaysDesc.RayGenerationShaderRecord.SizeInBytes  = _primaryRaysRayGenShaderTable->GetDesc().Width;
    dispatchRaysDesc.Width                                  = IOEventDistributor::screenPixelWidth;
    dispatchRaysDesc.Height                                 = IOEventDistributor::screenPixelHeight;
    dispatchRaysDesc.Depth                                  = 1;
   
    cmdList->SetPipelineState1(_dxrPrimaryRaysStateObject.Get());

    cmdList->DispatchRays(&dispatchRaysDesc);
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(nullptr));

}


void DXRStateObject::dispatchReflectionRays()
{
    auto cmdList = DXLayer::instance()->getCmdList();

    D3D12_DISPATCH_RAYS_DESC dispatchRaysDesc = {};

    dispatchRaysDesc.HitGroupTable.StartAddress             = _reflectionRaysHitGroupShaderTable->GetGPUVirtualAddress();
    dispatchRaysDesc.HitGroupTable.SizeInBytes              = _reflectionRaysHitGroupShaderTable->GetDesc().Width;
    dispatchRaysDesc.HitGroupTable.StrideInBytes            = dispatchRaysDesc.HitGroupTable.SizeInBytes;
    dispatchRaysDesc.MissShaderTable.StartAddress           = _reflectionRaysMissShaderTable->GetGPUVirtualAddress();
    dispatchRaysDesc.MissShaderTable.SizeInBytes            = _reflectionRaysMissShaderTable->GetDesc().Width;
    dispatchRaysDesc.MissShaderTable.StrideInBytes          = dispatchRaysDesc.MissShaderTable.SizeInBytes;
    dispatchRaysDesc.RayGenerationShaderRecord.StartAddress = _reflectionRaysRayGenShaderTable->GetGPUVirtualAddress();
    dispatchRaysDesc.RayGenerationShaderRecord.SizeInBytes  = _reflectionRaysRayGenShaderTable->GetDesc().Width;
    dispatchRaysDesc.Width                                  = IOEventDistributor::screenPixelWidth;
    dispatchRaysDesc.Height                                 = IOEventDistributor::screenPixelHeight;
    dispatchRaysDesc.Depth                                  = 1;
   
    cmdList->SetPipelineState1(_dxrReflectionRaysStateObject.Get());

    cmdList->DispatchRays(&dispatchRaysDesc);
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(nullptr));

}
