#include "HLSLShader.h"
#include "DXLayer.h"
#include "EngineManager.h"
#include "Entity.h"
#include "Light.h"
#include "ResourceManager.h"
#include "ShaderBroker.h"
#include <fstream>
#include <iostream>
#include <vector>
#include <sstream>

// Instead of adding dxil header files just include this for dxil shader reflection
#define DXIL_FOURCC(ch0, ch1, ch2, ch3)                                                            \
    ((uint32_t)(uint8_t)(ch0) | (uint32_t)(uint8_t)(ch1) << 8 | (uint32_t)(uint8_t)(ch2) << 16 |   \
     (uint32_t)(uint8_t)(ch3) << 24)
constexpr uint32_t DFCC_DXIL = DXIL_FOURCC('D', 'X', 'I', 'L');
constexpr uint32_t DFCC_RootSignature = DXIL_FOURCC('R', 'T', 'S', '0');
constexpr uint32_t DFCC_TextureArrays = DXIL_FOURCC('D', 'X', '1', '0');

// You can hit this in a debugger.
// Set to 'true' to printf every shader that is linked or compiled.
static volatile bool g_VerboseShaders = false;

HLSLShader::HLSLShader(std::string pipelineShaderName, std::string fragmentShaderName,
                       std::vector<DXGI_FORMAT>* rtvs)
{
    _isDXR = false;
    // set vertex name
    _pipelineShaderName = pipelineShaderName;
    _isASHeapCreated    = false;
    // build it
    build(rtvs);

    UINT indexBytes = static_cast<UINT>(4) * sizeof(UINT);
    int  indices[]  = {0, 1, 2, 3};
    _indexBuffer    = new ResourceBuffer(indices, indexBytes, DXLayer::instance()->getAttributeBufferCopyCmdList(),
                                      DXLayer::instance()->getDevice());

    _ibv.BufferLocation = _indexBuffer->getGPUAddress();
    _ibv.Format         = DXGI_FORMAT_R32_UINT;
    _ibv.SizeInBytes    = indexBytes;

}

HLSLShader::HLSLShader(std::string pipelineShaderName, bool isDXR)
{
    _pipelineShaderName = pipelineShaderName;
    _isDXR              = isDXR;
}
HLSLShader::~HLSLShader() {}

std::wstring HLSLShader::_stringToLPCWSTR(const std::string& s)
{
    int len;
    int slength  = (int)s.length() + 1;
    len          = MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, 0, 0);
    wchar_t* buf = new wchar_t[len];
    MultiByteToWideChar(CP_ACP, 0, s.c_str(), slength, buf, len);
    std::wstring r(buf);
    delete[] buf;
    return r;
}

std::wstring HLSLShader::getName()
{
    return _stringToLPCWSTR(_pipelineShaderName);
}


void HLSLShader::buildDXC(ComPtr<IDxcBlob>& pResultBlob, std::wstring shaderString,
                          std::wstring shaderProfile, std::wstring entryPoint,
                          std::vector<uint8_t>& stream)
{

    HRESULT                     initDLL = S_FALSE;
    ComPtr<IDxcOperationResult> dxcResult;
    CComPtr<IDxcBlobEncoding>   pSource;
    CComPtr<IDxcLibrary>        pLibrary;
    CComPtr<IDxcCompiler2>      dxcCompiler;
    TCHAR                       fullPath[MAX_PATH];

    if (!_dllSupport.IsEnabled())
    {
        initDLL = _dllSupport.Initialize();
    }

    _dllSupport.CreateInstance(CLSID_DxcLibrary, &pLibrary);

    std::wstring      strippedName  = shaderString.substr(0, shaderString.find_last_of('/') + 1);
    std::wstring      name = shaderString.substr(0, shaderString.find_last_of('.')) + L".cso";
    
    HRESULT result = S_OK;
    bool compileAtRuntime = false;

    if (compileAtRuntime == false)
    {
        ComPtr<IDxcBlobEncoding> precompiledBlob;

        std::ifstream in(name, std::ifstream::ate | std::ifstream::binary);
        int           fileSize = in.tellg();
        in.close();
        if (fileSize == -1)
        {
            return;
        }

        stream.resize(fileSize);

        std::ifstream csoFile(name, std::ifstream::binary);
        csoFile.read(reinterpret_cast<char*>(stream.data()), fileSize);

        pLibrary->CreateBlobWithEncodingFromPinned(stream.data(), fileSize, 0,
                                                   &precompiledBlob);

        pResultBlob = precompiledBlob.Get();

        csoFile.close();
    }
    else
    {
        LPCWSTR args[] = {
            L"-Od",
            L"/Zi",
            /*L"/Zss", */ L"-Qembed_debug",
        };

        // Implements a header file handler to share code in shaders
        CComPtr<IDxcIncludeHandler> includeHandler;
        pLibrary->CreateIncludeHandler(&includeHandler);

        CComPtr<IDxcBlob> shaderPDBData = nullptr;
        LPWSTR            debugName     = L"";

        ComPtr<IDxcBlobEncoding> containerBlob;

        pLibrary->CreateBlobFromFile(shaderString.c_str(), nullptr, &pSource);
        _dllSupport.CreateInstance(CLSID_DxcCompiler, &dxcCompiler);

        dxcCompiler->CompileWithDebug(pSource, shaderString.c_str(), entryPoint.c_str(),
                                      shaderProfile.c_str(), args, sizeof(args) / sizeof(LPCWSTR*),
                                      nullptr, 0, includeHandler, &dxcResult, &debugName,
                                      &shaderPDBData);

        dxcResult->GetResult(&pResultBlob);
    }

    HMODULE               dxil_module = ::LoadLibrary("dxil.dll");
    DxcCreateInstanceProc dxil_create_func =
        (DxcCreateInstanceProc)GetProcAddress(dxil_module, "DxcCreateInstance");

    ComPtr<IDxcValidator> validator;
    dxil_create_func(CLSID_DxcValidator, __uuidof(IDxcValidator), (void**)&validator);

    ComPtr<IDxcOperationResult> validationResult;
    validator->Validate(pResultBlob.Get(), DxcValidatorFlags_InPlaceEdit, &validationResult);

    HRESULT validateStatus;
    validationResult->GetStatus(&validateStatus);
    if (validateStatus != S_OK)
    {
        ComPtr<IDxcBlobEncoding> printBlob, printBlobUtf8;
        validationResult->GetErrorBuffer(&printBlob);
        pLibrary->GetBlobAsUtf8(printBlob.Get(), printBlobUtf8.GetAddressOf());

        std::string errorString;
        if (printBlobUtf8)
        {
            errorString = reinterpret_cast<const char*>(printBlobUtf8->GetBufferPointer());
        }
        OutputDebugStringA(errorString.c_str());
    }

    struct DxilMinimalHeader
    {
        UINT32 four_cc;
        UINT32 hash_digest[4];
    };
    DxilMinimalHeader* header = reinterpret_cast<DxilMinimalHeader*>(pResultBlob->GetBufferPointer());
    bool               has_digest = false;
    has_digest |= header->hash_digest[0] != 0x0;
    has_digest |= header->hash_digest[1] != 0x0;
    has_digest |= header->hash_digest[2] != 0x0;
    has_digest |= header->hash_digest[3] != 0x0;

    if (has_digest == false)
    {
        return;
    }

    if (_isDXR)
    {
        return;
    }

    CComPtr<IDxcBlobEncoding>        pDisassembleBlob;
    ID3D12ShaderReflection*          reflectionInterface;
    CComPtr<IDxcContainerReflection> pReflection;
    UINT32                           shaderIdx;

    _dllSupport.CreateInstance(CLSID_DxcContainerReflection, &pReflection);
    pReflection->Load(pResultBlob.Get());
    pReflection->FindFirstPartKind(DFCC_DXIL, &shaderIdx);
    pReflection->GetPartReflection(shaderIdx, __uuidof(ID3D12ShaderReflection),
                                   (void**)&reflectionInterface);

    reflectionInterface->GetThreadGroupSize(&_threadGroupSizeX, &_threadGroupSizeY, &_threadGroupSizeZ);

    for (int i = 0; result == S_OK; i++)
    {
        D3D12_SIGNATURE_PARAMETER_DESC varDesc;
        result = reflectionInterface->GetInputParameterDesc(i, &varDesc);
        if (result == S_OK)
        {
            _inputDescriptorTable[varDesc.SemanticName] = varDesc;
        }
    }
    result = S_OK;
    for (int i = 0; result == S_OK; i++)
    {
        D3D12_SHADER_INPUT_BIND_DESC resourceDesc;
        result = reflectionInterface->GetResourceBindingDesc(i, &resourceDesc);

        if (result == S_OK)
        {
            auto constBuff = reflectionInterface->GetConstantBufferByName(resourceDesc.Name);
            if (constBuff != nullptr)
            {
                ID3D12ShaderReflectionVariable* constBufferVar = nullptr;
                for (int j = 0;; j++)
                {
                    constBufferVar                         = constBuff->GetVariableByIndex(j);
                    D3D12_SHADER_VARIABLE_DESC* ref        = new D3D12_SHADER_VARIABLE_DESC();
                    auto                        isVariable = constBufferVar->GetDesc(ref);
                    if (isVariable != S_OK)
                    {
                        break;
                    }
                    else
                    {
                        bool found = false;
                        for (auto subIndex : _constBuffDescriptorTable[resourceDesc.Name])
                        {
                            if (std::string(subIndex.Name).compare(std::string(ref->Name)) == 0)
                            {
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                        {
                            _constBuffDescriptorTable[resourceDesc.Name].push_back(*ref);
                        }
                    }
                }
            }
            _resourceDescriptorTable[resourceDesc.Name] = resourceDesc;
        }
    }

    //// Use this to inspect dxil generation
    //dxcCompiler->Disassemble(pResultBlob.Get(), &pDisassembleBlob);
    //std::string disassembleString((const char*)pDisassembleBlob->GetBufferPointer());
    //OutputDebugStringA((disassembleString + "\n").c_str());
}

Vector4 HLSLShader::getThreadGroupSize()
{
    return Vector4(static_cast<float>(_threadGroupSizeX),
                   static_cast<float>(_threadGroupSizeY),
                   static_cast<float>(_threadGroupSizeZ));
}

void HLSLShader::build(std::vector<DXGI_FORMAT>* rtvs)
{

    auto device = DXLayer::instance()->getDevice();

    std::string fileName = _pipelineShaderName;
    fileName.append(".hlsl");
    auto shaderString = _stringToLPCWSTR(fileName);

    uint32_t vsByteSize = 0;
    void*    vsBuffer   = nullptr;
    HRESULT  vsResult   = !S_OK;
    uint32_t psByteSize = 0;
    void*    psBuffer   = nullptr;
    HRESULT  psResult   = !S_OK;
    uint32_t csByteSize = 0;
    void*    csBuffer   = nullptr;
    HRESULT  csResult   = !S_OK;

    ComPtr<ID3DBlob>  compiledVS;
    ComPtr<ID3DBlob>  compiledPS;
    ComPtr<ID3DBlob>  compiledCS;
    ComPtr<IDxcBlob> compiledDXILVS;
    ComPtr<IDxcBlob> compiledDXILPS;
    ComPtr<IDxcBlob> compiledDXILCS;
    std::vector<uint8_t> streamVS;
    std::vector<uint8_t> streamPS;
    std::vector<uint8_t> streamCS;

    bool isVSPS = _pipelineShaderName.find("VS") != std::string::npos;

    std::string baseName   = _pipelineShaderName.substr(_pipelineShaderName.find_last_of('/'));
    baseName[baseName.length() - 2] = 'P';
    std::string psStringFileName = _pipelineShaderName.substr(0, _pipelineShaderName.substr(0, _pipelineShaderName.find_last_of('/')).find_last_of('/'));
    std::wstring psFileName;
    if (isVSPS)
    {
        psStringFileName += "/ps" + baseName + ".hlsl";
        psFileName = _stringToLPCWSTR(psStringFileName);
    }

    if (useDxcCompiler && isVSPS)
    {
        std::wstring profile = L"vs_6_3";

        buildDXC(compiledDXILVS, shaderString, profile, L"main", streamVS);

        if (compiledDXILVS != nullptr)
        {
            vsByteSize = compiledDXILVS->GetBufferSize();
            vsBuffer   = compiledDXILVS->GetBufferPointer();
            vsResult   = S_OK;
        }
    }
    else if (useDxcCompiler == false)
    {

        ComPtr<ID3DBlob> errorsVS;
        vsResult =
            D3DCompileFromFile(shaderString.c_str(), 0, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main",
                               "vs_5_0", 0, 0, compiledVS.GetAddressOf(), errorsVS.GetAddressOf());

        if (vsResult != S_OK)
        {
            OutputDebugStringA((char*)errorsVS->GetBufferPointer());
        }
        else
        {
            vsByteSize = compiledVS->GetBufferSize();
            vsBuffer   = compiledVS->GetBufferPointer();
            _queryShaderResources(vsBuffer, vsByteSize);
        }
    }

    if (useDxcCompiler && isVSPS)
    {
        std::wstring profile = L"ps_6_3";

        buildDXC(compiledDXILPS, psFileName.c_str(), profile, L"main", streamPS);

        if (compiledDXILPS != nullptr)
        {
            psByteSize = compiledDXILPS->GetBufferSize();
            psBuffer   = compiledDXILPS->GetBufferPointer();
            psResult   = S_OK;
        }
    }
    else if(useDxcCompiler == false)
    {
        ComPtr<ID3DBlob> errorsPS;
        psResult =
            D3DCompileFromFile(shaderString.c_str(), 0, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main",
                               "ps_5_0", 0, 0, compiledPS.GetAddressOf(), errorsPS.GetAddressOf());

        if (psResult != S_OK)
        {
            OutputDebugStringA((char*)errorsPS->GetBufferPointer());
        }
        else
        {
            psByteSize = compiledPS->GetBufferSize();
            psBuffer   = compiledPS->GetBufferPointer();
            _queryShaderResources(psBuffer, psByteSize);
        }
    }

    if (useDxcCompiler && _pipelineShaderName.find("CS") != std::string::npos)
    {

        std::wstring profile = L"cs_6_3";

        buildDXC(compiledDXILCS, shaderString, profile, L"main", streamCS);

        if (compiledDXILCS != nullptr)
        {
            csByteSize = compiledDXILCS->GetBufferSize();
            csBuffer   = compiledDXILCS->GetBufferPointer();
            csResult   = S_OK;
        }
    }
    else if (useDxcCompiler == false)
    {
        ComPtr<ID3DBlob> errorsCS;
        csResult =
            D3DCompileFromFile(shaderString.c_str(), 0, D3D_COMPILE_STANDARD_FILE_INCLUDE, "main",
                               "cs_5_0", 0, 0, compiledCS.GetAddressOf(), errorsCS.GetAddressOf());

        if (csResult != S_OK)
        {
            OutputDebugStringA((char*)errorsCS->GetBufferPointer());
        }
        else
        {
            csByteSize = compiledCS->GetBufferSize();
            csBuffer   = compiledCS->GetBufferPointer();
            _queryShaderResources(csBuffer, csByteSize);
        }
    }

    CD3DX12_DESCRIPTOR_RANGE*                 srvTableRange     = nullptr;
    CD3DX12_DESCRIPTOR_RANGE*                 samplerTableRange = nullptr;
    std::vector<CD3DX12_ROOT_PARAMETER>       rootParameters{_resourceDescriptorTable.size()};
    std::map<D3D_SHADER_INPUT_TYPE, uint32_t> heapCounters;
    int                                       i                  = 0;
    int                                       rootParameterIndex = 0;

    // all b0 aka constant buffer and root constants must be in this loop
    for (auto resource : _resourceDescriptorTable)
    {
        if (resource.second.Type == D3D_SHADER_INPUT_TYPE::D3D_SIT_CBUFFER)
        {

            UINT bytes = 0;
            for (auto shaderInput : _constBuffDescriptorTable[resource.second.Name])
            {
                bytes = max(bytes, shaderInput.StartOffset + shaderInput.Size);
            }

            if (std::string(resource.second.Name).compare("objectData") == 0)
            { // use root constants for per model objects

                rootParameters[resource.second.uID + rootParameterIndex].InitAsConstants(
                    bytes / 4, resource.second.uID + rootParameterIndex);
                _constantBuffers[resource.second.Name] =
                    new ConstantBuffer(device, _constBuffDescriptorTable[resource.second.Name]);
                _resourceIndexes[resource.second.Name] = resource.second.uID + rootParameterIndex;
            }
            else
            {

                rootParameters[resource.second.uID + rootParameterIndex].InitAsConstantBufferView(
                    resource.second.uID + rootParameterIndex);
                _constantBuffers[resource.second.Name] =
                    new ConstantBuffer(device, _constBuffDescriptorTable[resource.second.Name]);
                _resourceIndexes[resource.second.Name] = resource.second.uID + rootParameterIndex;
            }
            i++;
            heapCounters[resource.second.Type]++;
        }
    }
    // all s0 aka sampler shader resources must be in this loop
    rootParameterIndex = static_cast<int>(_resourceIndexes.size());
    /*for (auto resource : _resourceDescriptorTable)
    {
        if (resource.second.Type == D3D_SHADER_INPUT_TYPE::D3D_SIT_SAMPLER && csResult != S_OK)
        {
            samplerTableRange = new CD3DX12_DESCRIPTOR_RANGE();
            samplerTableRange->Init(D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, 1, 0);
            rootParameters[resource.second.uID + rootParameterIndex].InitAsDescriptorTable(
                1, samplerTableRange, D3D12_SHADER_VISIBILITY_ALL);
            _resourceIndexes[resource.second.Name] = resource.second.uID + rootParameterIndex;
            i++;
        }
    }*/
    // all t0 aka SRV shader resources must be in this loop
    rootParameterIndex = static_cast<int>(_resourceIndexes.size());
    for (auto resource : _resourceDescriptorTable)
    {
        if (resource.second.Type == D3D_SHADER_INPUT_TYPE::D3D_SIT_TEXTURE ||
            resource.second.Type == D3D_SHADER_INPUT_TYPE::D3D_SIT_STRUCTURED ||
            resource.second.Type == D3D_SHADER_INPUT_TYPE::D3D_SIT_BYTEADDRESS)
        {
            srvTableRange = new CD3DX12_DESCRIPTOR_RANGE();

            // Bind count of 0 indicates a bindless/boundless resource
            if (resource.second.BindCount == 0)
            {
                // This is specific to bindless resource access in a global root signature
                // so use the maximum bottom level size multiplied by 4 textures per material
                const UINT descriptorTableEntries = MaxBLASSRVsForRayTracing;
                // Add 256 potential slots for this resource
                srvTableRange->Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1 + descriptorTableEntries,
                                    resource.second.BindPoint);
            }
            else
            {
                srvTableRange->Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, resource.second.BindCount,
                                    resource.second.BindPoint);
            }
            if (csResult == S_OK)
            {
                srvTableRange->RegisterSpace = resource.second.Space;
                rootParameters[resource.second.BindPoint + rootParameterIndex]
                    .InitAsDescriptorTable(
                    1, srvTableRange);
            }
            else
            {
                srvTableRange->RegisterSpace = resource.second.Space;
                rootParameters[resource.second.BindPoint + rootParameterIndex]
                    .InitAsDescriptorTable(
                    1, srvTableRange, D3D12_SHADER_VISIBILITY_ALL);
            }

            _resourceIndexes[resource.second.Name] = resource.second.BindPoint + rootParameterIndex;
            i++;
            heapCounters[resource.second.Type]++;
        }
        // SRV or Raytracing acceleration structure aka 12
        else if (resource.second.Type == 12)
        {

            rootParameters[resource.second.BindPoint + rootParameterIndex].InitAsShaderResourceView(0);

            _resourceIndexes[resource.second.Name] = resource.second.BindPoint + rootParameterIndex;
            i++;
            heapCounters[resource.second.Type]++;
        }
    }
    // all u0 aka UAV shader resources must be in this loop
    rootParameterIndex = static_cast<int>(_resourceIndexes.size());
    for (auto resource : _resourceDescriptorTable)
    {
        if (resource.second.Type == D3D_SHADER_INPUT_TYPE::D3D_SIT_UAV_RWTYPED)
        {
            srvTableRange = new CD3DX12_DESCRIPTOR_RANGE();
            srvTableRange->Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, resource.second.uID);
            rootParameters[resource.second.uID + rootParameterIndex].InitAsDescriptorTable(
                1, srvTableRange);
            _resourceIndexes[resource.second.Name] = resource.second.uID + rootParameterIndex;
            i++;
            heapCounters[resource.second.Type]++;
        }
    }

    ComPtr<ID3DBlob>            pOutBlob, pErrorBlob;
    CD3DX12_ROOT_SIGNATURE_DESC descRootSignature;
    D3D12_STATIC_SAMPLER_DESC   samplerDesc = {};
    /*if (csResult == S_OK)
    {*/

        bool foundStaticSampler = false;
        for (auto resource : _resourceDescriptorTable)
        {
            if (resource.second.Type == D3D_SHADER_INPUT_TYPE::D3D_SIT_SAMPLER)
            {
                foundStaticSampler = true;
            }
        }

        if (foundStaticSampler)
        {

            samplerDesc.Filter           = D3D12_FILTER_MIN_MAG_LINEAR_MIP_POINT;
            samplerDesc.AddressU         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            samplerDesc.AddressV         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            samplerDesc.AddressW         = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            samplerDesc.MipLODBias       = 0.0f;
            samplerDesc.ComparisonFunc   = D3D12_COMPARISON_FUNC_NEVER;
            samplerDesc.MinLOD           = 0.0f;
            samplerDesc.MaxLOD           = D3D12_FLOAT32_MAX;
            samplerDesc.MaxAnisotropy    = 0;
            samplerDesc.BorderColor      = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
            samplerDesc.ShaderRegister   = 0;
            samplerDesc.RegisterSpace    = 0;
            samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            descRootSignature.Init(static_cast<UINT>(_resourceIndexes.size()), &rootParameters[0],
                                   1, &samplerDesc,
                                   D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        }
        else
        {
            descRootSignature.Init(static_cast<UINT>(_resourceIndexes.size()), &rootParameters[0],
                                   0, nullptr,
                                   D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        }
    //}
    //else
    //{
    //    descRootSignature.Init(static_cast<UINT>(_resourceIndexes.size()), &rootParameters[0], 0,
    //                           nullptr,
    //                           D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
    //}

    D3D12SerializeRootSignature(&descRootSignature, D3D_ROOT_SIGNATURE_VERSION_1,
                                pOutBlob.GetAddressOf(), pErrorBlob.GetAddressOf());

    if (pErrorBlob)
    {
        OutputDebugStringA((char*)pErrorBlob->GetBufferPointer());
    }

    device->CreateRootSignature(1, pOutBlob->GetBufferPointer(), pOutBlob->GetBufferSize(),
                                IID_PPV_ARGS(_rootSignature.GetAddressOf()));

    if (csResult != S_OK)
    {
        _inputLayout.push_back({"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
                                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});

        _inputLayout.push_back({"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12,
                                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});

        _inputLayout.push_back({"UV", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24,
                                D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso;
        ZeroMemory(&pso, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
        pso.InputLayout    = {_inputLayout.data(), static_cast<UINT>(_inputLayout.size())};
        pso.pRootSignature = _rootSignature.Get();
        if (vsResult == S_OK)
        {
            pso.VS = {vsBuffer, vsByteSize};
        }
        if (psResult == S_OK)
        {
            pso.PS = {psBuffer, psByteSize};
        }
        auto rasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        // disabled culling
        rasterizerState.CullMode  = D3D12_CULL_MODE_NONE;
        pso.RasterizerState       = rasterizerState;
        pso.BlendState            = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        pso.DepthStencilState     = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        pso.SampleMask            = UINT_MAX;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

        bool foundDepthStencil = false;
        if (rtvs != nullptr)
        {
            int j = 0;
            for (auto rtv : *rtvs)
            {
                if (rtv == DXGI_FORMAT_D32_FLOAT || rtv == DXGI_FORMAT_R32_TYPELESS)
                {
                    foundDepthStencil = true;
                }
                else
                {
                    pso.RTVFormats[j++] = rtv;
                    pso.NumRenderTargets++;
                }
            }
        }
        else
        {
            pso.NumRenderTargets = 0;
        }

        pso.SampleDesc.Quality = 0;
        pso.SampleDesc.Count   = 1;

        if (foundDepthStencil)
        {
            pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        }
        else
        {
            pso.DSVFormat = DXGI_FORMAT_UNKNOWN;
        }

        HRESULT result = device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&_psoState));
        _psoState->SetName(_stringToLPCWSTR(baseName).c_str());
    }
    else
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pso;
        ZeroMemory(&pso, sizeof(D3D12_COMPUTE_PIPELINE_STATE_DESC));
        pso.pRootSignature = _rootSignature.Get();
        if (csResult == S_OK)
        {
            pso.CS = {csBuffer, csByteSize};
        }
        pso.Flags      = D3D12_PIPELINE_STATE_FLAG_NONE;
        HRESULT result = device->CreateComputePipelineState(&pso, IID_PPV_ARGS(&_psoState));
        _psoState->SetName(_stringToLPCWSTR(baseName).c_str());
    }
}

ComPtr<ID3D12RootSignature> HLSLShader::getRootSignature()
{
    return _rootSignature;
}

void HLSLShader::setOM(std::vector<RenderTexture> targets, int width, int height)
{

    auto device  = DXLayer::instance()->getDevice();
    auto cmdList = DXLayer::instance()->getCmdList();

    D3D12_VIEWPORT viewPort = {0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height),
                               0.0f, 1.0f};

    // Scissor rectangle

    D3D12_RECT rectScissor = {0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};

    CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle;
    D3D12_CPU_DESCRIPTOR_HANDLE*  handles     = new D3D12_CPU_DESCRIPTOR_HANDLE[targets.size() - 1];
    int                           handleIndex = 0;
    bool                          containsDepthStencil = false;
    UINT                          rtvCount             = 0;
    for (auto buffer : targets)
    {
        if (buffer.getFormat() == DXGI_FORMAT_D32_FLOAT ||
            buffer.getFormat() == DXGI_FORMAT_R32_TYPELESS)
        {

            dsvHandle = buffer.getCPUHandle();
            buffer.bindTarget(D3D12_RESOURCE_STATE_DEPTH_WRITE);
            containsDepthStencil = true;
        }
        else
        {
            handles[handleIndex++] = buffer.getCPUHandle();
            buffer.bindTarget(D3D12_RESOURCE_STATE_RENDER_TARGET);
            rtvCount++;
        }
    }

    const float clear[] = {0.0f, 0.0f, 0.0f, 1.000f};

    cmdList->RSSetViewports(1, &viewPort);
    cmdList->RSSetScissorRects(1, &rectScissor);

    if (containsDepthStencil)
    {
        cmdList->OMSetRenderTargets(rtvCount, handles, false, &dsvHandle);
    }
    else
    {
        cmdList->OMSetRenderTargets(rtvCount, handles, false, nullptr);
    }

    int rtvIndex = 0;
    for (int i = 0; i < targets.size(); i++)
    {

        // Clear target
        if (targets[i].getFormat() == DXGI_FORMAT_D32_FLOAT ||
            targets[i].getFormat() == DXGI_FORMAT_R32_TYPELESS)
        {

            cmdList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, NULL, 0);
        }
        else
        {
            cmdList->ClearRenderTargetView(handles[rtvIndex++], clear, NULL, 0);
        }
    }
}

void HLSLShader::releaseOM(std::vector<RenderTexture> targets)
{
    for (auto buffer : targets)
    {
        if (buffer.getFormat() == DXGI_FORMAT_D32_FLOAT ||
            buffer.getFormat() == DXGI_FORMAT_R32_TYPELESS)
        {
            buffer.unbindTarget(D3D12_RESOURCE_STATE_DEPTH_WRITE);
        }
        else
        {
            buffer.unbindTarget(D3D12_RESOURCE_STATE_RENDER_TARGET);
        }
    }
}

void HLSLShader::_queryShaderResources(void* shader, uint32_t shaderSize)
{

    ID3D12ShaderReflection* reflectionInterface;
    D3DReflect(shader, shaderSize, IID_ID3D12ShaderReflection, (void**)&reflectionInterface);

    HRESULT result = S_OK;

    for (int i = 0; result == S_OK; i++)
    {
        D3D12_SIGNATURE_PARAMETER_DESC varDesc;
        result = reflectionInterface->GetInputParameterDesc(i, &varDesc);
        if (result == S_OK)
        {
            _inputDescriptorTable[varDesc.SemanticName] = varDesc;
        }
    }
    result = S_OK;
    for (int i = 0; result == S_OK; i++)
    {
        D3D12_SHADER_INPUT_BIND_DESC resourceDesc;
        result = reflectionInterface->GetResourceBindingDesc(i, &resourceDesc);
        if (result == S_OK)
        {
            auto constBuff = reflectionInterface->GetConstantBufferByName(resourceDesc.Name);
            if (constBuff != nullptr)
            {
                ID3D12ShaderReflectionVariable* constBufferVar = nullptr;
                for (int j = 0;; j++)
                {
                    constBufferVar                         = constBuff->GetVariableByIndex(j);
                    D3D12_SHADER_VARIABLE_DESC* ref        = new D3D12_SHADER_VARIABLE_DESC();
                    auto                        isVariable = constBufferVar->GetDesc(ref);
                    if (isVariable != S_OK)
                    {
                        break;
                    }
                    else
                    {
                        bool found = false;
                        for (auto subIndex : _constBuffDescriptorTable[resourceDesc.Name])
                        {
                            if (std::string(subIndex.Name).compare(std::string(ref->Name)) == 0)
                            {
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                        {
                            _constBuffDescriptorTable[resourceDesc.Name].push_back(*ref);
                        }
                    }
                }
            }
            _resourceDescriptorTable[resourceDesc.Name] = resourceDesc;
        }
    }
}

void HLSLShader::updateData(std::string id, void* data, bool isCompute)
{

    auto cmdList = DXLayer::instance()->getCmdList();
    auto cmdListIndex = DXLayer::instance()->getCmdListIndex();
    bool found        = false;
    for (auto constBuffEntry : _constBuffDescriptorTable)
    {
        for (auto entry : constBuffEntry.second)
        {
            std::string identifier = id;
            size_t      index      = identifier.find("[");
            if (index != std::string::npos)
            {
                identifier = identifier.substr(0, index);
            }
            if (std::string(entry.Name).compare(identifier) == 0)
            {
                _constantBuffers[constBuffEntry.first]->update(
                    cmdList, cmdListIndex, data, constBuffEntry.first,
                    _resourceIndexes[constBuffEntry.first],
                    entry.Size, entry.StartOffset, isCompute);

                found = true;
                break;
            }
        }

        if (found)
        {
            break;
        }
    }
}

void HLSLShader::updateRTAS(std::string id, ComPtr<ID3D12DescriptorHeap> rtASDescriptorHeap,
                            D3D12_GPU_VIRTUAL_ADDRESS gpuva, bool isCompute)
{

    auto cmdList = DXLayer::instance()->getCmdList();

    cmdList->SetDescriptorHeaps(1, rtASDescriptorHeap.GetAddressOf());

    if (isCompute)
    {
        cmdList->SetComputeRootShaderResourceView(_resourceIndexes[id], gpuva);
    }
    else
    {
        cmdList->SetGraphicsRootShaderResourceView(_resourceIndexes[id], gpuva);
    }
}

void HLSLShader::updateData(std::string dataName, int textureUnit, Texture* texture, bool isCompute,
                            bool isUAV)
{
    auto cmdList = DXLayer::instance()->getCmdList();
    texture->bindToDXShader(cmdList, _resourceIndexes[dataName], _resourceIndexes, isCompute,
                            isUAV);
}

void HLSLShader::updateDataAsyncCompute(std::string dataName, int textureUnit, Texture* texture, bool isCompute, bool isUAV)
{
    auto cmdList = DXLayer::instance()->getComputeCmdList();
    texture->bindToDXShader(cmdList, _resourceIndexes[dataName], _resourceIndexes, isCompute,
                            isUAV);
}

void HLSLShader::updateData(std::string id, UINT textureUnit, Texture* texture, ImageData imageInfo)
{
    auto cmdList = DXLayer::instance()->getCmdList();
    texture->bindToDXShader(cmdList, _resourceIndexes[id], _resourceIndexes, false);
}

void HLSLShader::updateDataAsyncCompute(std::string id, void* data, bool isCompute)
{
    auto cmdList = DXLayer::instance()->getComputeCmdList();
    auto cmdListIndex = 0;
    for (auto constBuffEntry : _constBuffDescriptorTable)
    {
        for (auto entry : constBuffEntry.second)
        {
            std::string identifier = id;
            size_t      index      = identifier.find("[");
            if (index != std::string::npos)
            {
                identifier = identifier.substr(0, index);
            }
            if (std::string(entry.Name).compare(identifier) == 0)
            {
                _constantBuffers[constBuffEntry.first]->update(
                    cmdList, cmdListIndex, data, constBuffEntry.first,
                    _resourceIndexes[constBuffEntry.first],
                    entry.Size, entry.StartOffset, isCompute);
            }
        }
    }
}

void HLSLShader::bind()
{
    auto cmdList = DXLayer::instance()->getCmdList();
    cmdList->SetPipelineState(_psoState.Get());
    if (_inputLayout.size() > 0)
    {
        cmdList->SetGraphicsRootSignature(_rootSignature.Get());
    }
    else
    {
        cmdList->SetComputeRootSignature(_rootSignature.Get());
    }
}

void HLSLShader::bindAsyncCompute()
{
    auto cmdList = DXLayer::instance()->getComputeCmdList();
    cmdList->SetPipelineState(_psoState.Get());
    if (_inputLayout.size() > 0)
    {
        cmdList->SetGraphicsRootSignature(_rootSignature.Get());
    }
    else
    {
        cmdList->SetComputeRootSignature(_rootSignature.Get());
    }
}

void HLSLShader::unbind() {}

void HLSLShader::bindAttributes(VAO* vao, bool bindVertexBuffer)
{
    auto cmdList = DXLayer::instance()->getCmdList();

    if (vao != nullptr)
    {
        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        cmdList->IASetIndexBuffer(&(vao->getIndexBuffer()));
        if (bindVertexBuffer)
        {
            D3D12_VERTEX_BUFFER_VIEW vertexBuffers[] = {vao->getVertexBuffer()};
            cmdList->IASetVertexBuffers(0, 1, vertexBuffers);
        }
    }
    else
    {

        cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        cmdList->IASetIndexBuffer(&_ibv);
    }
}

void HLSLShader::dispatch(int x, int y, int z)
{
    DXLayer::instance()->getCmdList()->Dispatch(x, y, z);
}

void HLSLShader::dispatchAsyncCompute(int x, int y, int z)
{
    DXLayer::instance()->getComputeCmdList()->Dispatch(x, y, z);
}

void HLSLShader::draw(int offset, int instances, int numTriangles)
{
    DXLayer::instance()->getCmdList()->DrawIndexedInstanced(numTriangles, 1, offset, 0, 0);
}
