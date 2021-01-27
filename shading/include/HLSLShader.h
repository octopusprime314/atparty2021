/*
 * HLSLShader is part of the ReBoot distribution (https://github.com/octopusprime314/ReBoot.git).
 * Copyright (c) 2017 Peter Morley.
 *
 * ReBoot is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * ReBoot is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 *  HLSLShader class. Basic shader class that is responsible only for compilation and
 *  storing the hlsl data of the shader
 */

#pragma once
#include "ConstantBuffer.h"
#include "RenderTexture.h"
#include "Shader.h"
#include "d3dx12.h"
#include "dxc/dxcapi.h"
#include "dxc/dxcapi.use.h"
#include <D3Dcompiler.h>
#include <atlcomcli.h>
#include <d3d12.h>
#include <iostream>
#include <string>
#include <vector>
#include <wrl.h>

using namespace Microsoft::WRL;

class Entity;
class Light;
class ShaderBroker;
struct D3DBuffer;

constexpr bool useDxcCompiler = true;

// Simple shader loading class that should be derived from to create more complex shaders
class HLSLShader : public Shader
{

    using InputDescriptors     = std::map<std::string, D3D12_SIGNATURE_PARAMETER_DESC>;
    using ResourceDescriptors  = std::map<std::string, D3D12_SHADER_INPUT_BIND_DESC>;
    using ConstBuffDescriptors = std::map<std::string, std::vector<D3D12_SHADER_VARIABLE_DESC>>;

  protected:
    void         _queryShaderResources(void* shader, uint32_t shaderSize);
    std::wstring _stringToLPCWSTR(const std::string& s);

    ConstBuffDescriptors                   _constBuffDescriptorTable;
    ResourceDescriptors                    _resourceDescriptorTable;
    InputDescriptors                       _inputDescriptorTable;
    std::string                            _pipelineShaderName;
    std::map<std::string, ConstantBuffer*> _constantBuffers;
    bool                                   _isASHeapCreated;
    ComPtr<ID3D12RootSignature>            _rootSignature;
    ResourceBuffer*                        _indexBuffer;
    dxc::DxcDllSupport                     _dllSupport;
    std::vector<D3D12_INPUT_ELEMENT_DESC>  _inputLayout;
    ComPtr<ID3D12PipelineState>            _psoState;
    D3D12_INDEX_BUFFER_VIEW                _ibv;
    UINT                                   _threadGroupSizeX;
    UINT                                   _threadGroupSizeY;
    UINT                                   _threadGroupSizeZ;
    bool                                   _isDXR;

  public:
    HLSLShader(std::string pipelineShaderName, bool isDXR);
    HLSLShader(std::string vertexShaderName, std::string fragmentShaderName = "",
               std::vector<DXGI_FORMAT>* rtvs = nullptr);
    virtual ~HLSLShader();

    void draw(int offset, int instances, int numTriangles);
    void dispatch(int x, int y, int z);
    void updateData(std::string id, void* data, bool isCompute = false);
    void updateData(std::string dataName, int textureUnit, Texture* texture, bool isCompute = false,
                    bool isUAV = false);
    Vector4 getThreadGroupSize();
    void updateData(std::string id, UINT textureUnit, Texture* texture, ImageData imageInfo);
    void updateRTAS(std::string id, ComPtr<ID3D12DescriptorHeap> rtASDescriptorHeap,
                    D3D12_GPU_VIRTUAL_ADDRESS gpuva, bool isCompute = false);
 
    ComPtr<ID3D12RootSignature> getRootSignature();
    std::wstring                getName();

    static void setOM(std::vector<RenderTexture> targets, int width, int height);
    static void releaseOM(std::vector<RenderTexture> targets);
    void        build(std::vector<DXGI_FORMAT>* rtvs);
    void        buildDXC(ComPtr<IDxcBlob>& pResultBlob, std::wstring shaderString,
                         std::wstring shaderProfile, std::wstring entryPoint,
                         std::vector<uint8_t>& stream);

    void bindAttributes(VAO* vao);
    void unbindAttributes(){};
    void unbind();
    void bind();

    void bindAsyncCompute();
    void dispatchAsyncCompute(int x, int y, int z);
    void updateDataAsyncCompute(std::string id, void* data, bool isCompute);
    void updateDataAsyncCompute(std::string dataName, int textureUnit, Texture* texture,
                                bool isCompute, bool isUAV);

    std::map<std::string, UINT> _resourceIndexes;
};