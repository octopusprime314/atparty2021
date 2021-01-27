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

using namespace Microsoft::WRL;

class DXRStateObject
{
    ComPtr<ID3D12StateObject> _dxrStateObject;

  public:
    DXRStateObject(ComPtr<ID3D12RootSignature> rootSignature);
};