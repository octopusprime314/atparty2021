#pragma once
#include "d3d12.h"
#include "d3d12_1.h"
#include "d3dx12.h"
#include "dxc/dxcapi.h"
#include "dxc/dxcapi.use.h"
#include <D3Dcompiler.h>
#include <map>
#include <string>
#include <vector>
#include <wrl.h>
#include "HLSLShader.h"
#include "DXDefines.h"
#include "ShaderTable.h"

using namespace Microsoft::WRL;

class DXRStateObject
{
    ComPtr<ID3D12StateObject> _dxrPrimaryRaysStateObject;
    ComPtr<ID3D12StateObject> _dxrReflectionRaysStateObject;


    ComPtr<ID3D12Resource>    _primaryRaysRayGenShaderTable;
    ComPtr<ID3D12Resource>    _primaryRaysMissShaderTable;
    ComPtr<ID3D12Resource>    _primaryRaysHitGroupShaderTable;

    ComPtr<ID3D12Resource>    _reflectionRaysRayGenShaderTable;
    ComPtr<ID3D12Resource>    _reflectionRaysMissShaderTable;
    ComPtr<ID3D12Resource>    _reflectionRaysHitGroupShaderTable;

    void                      _buildShaderTables(const wchar_t *            raygenImport,
                                                 const wchar_t *            missShaderImport,
                                                 const wchar_t *            hitGroupExport,
                                                 ComPtr<ID3D12Resource>&    rayGenShaderTableResource,
                                                 ComPtr<ID3D12Resource>&    missShaderTableResource,
                                                 ComPtr<ID3D12Resource>&    hitGroupShaderTableResource,
                                                 ComPtr<ID3D12StateObject>& stateObject);

    void                      _buildStateObject(const wchar_t*              raygenImport,
                                                const wchar_t*              missShaderImport,
                                                const wchar_t*              hitGroupExport,
                                                const wchar_t*              closestHitImport,
                                                const wchar_t*              anyHitImport,
                                                ComPtr<ID3D12Resource>&     rayGenShaderTableResource,
                                                ComPtr<ID3D12Resource>&     missShaderTableResource,
                                                ComPtr<ID3D12Resource>&     hitGroupShaderTableResource,
                                                ComPtr<IDxcBlob>            pResultBlob,
                                                ComPtr<ID3D12RootSignature> rootSignature,
                                                ComPtr<ID3D12StateObject>&  stateObject);
  public:
    DXRStateObject(ComPtr<ID3D12RootSignature> primaryRaysRootSignature,
                   ComPtr<ID3D12RootSignature> reflectionRaysRootSignature);
    void dispatchPrimaryRays();
    void dispatchReflectionRays();
};