/*
 * PathTracerShader is part of the ReBoot distribution
 * (https://github.com/octopusprime314/ReBoot.git). Copyright (c) 2017 Peter Morley.
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

#pragma once
#include "DXLayer.h"
#include "Light.h"
#include "SVGFDenoiser.h"
#include "Shader.h"
#include "ViewEventDistributor.h"
#include "Sampler.h"
#include "ShaderBase.h"
#include "DXRStateObject.h"

class HLSLShader;

class PathTracerShader : public ShaderBase
{
    ComPtr<ID3D12DescriptorHeap> _descriptorHeap;
    UINT                         _descriptorSize;
    int                          _reflectionMode;
    int                          _shadowMode;
    int                          _viewMode;
    EngineStateFlags             _gameState;
    HLSLShader*                  _primaryRaysShader;
    HLSLShader*                  _sunLightRaysShader;
    HLSLShader*                  _reflectionRaysShader;
    HLSLShader*                  _indirectPointLightRaysShader;
    HLSLShader*                  _compositorShader;
    RenderTexture*               _albedoPrimaryRays;
    RenderTexture*               _normalPrimaryRays;
    RenderTexture*               _positionPrimaryRays;
    RenderTexture*               _viewZPrimaryRays;
    RenderTexture*               _reflectionRays;
    RenderTexture*               _occlusionRays;
    RenderTexture*               _denoisedOcclusionRays;
    RenderTexture*               _pointLightOcclusion;
    RenderTexture*               _pointLightOcclusionHistory;
    RenderTexture*               _sunLightRays;
    RenderTexture*               _indirectLightRays;
    RenderTexture*               _indirectLightRaysHistoryBuffer;
    RenderTexture*               _indirectSpecularLightRays;
    RenderTexture*               _indirectSpecularLightRaysHistoryBuffer;
    RenderTexture*               _diffusePrimarySurfaceModulation;
    RenderTexture*               _specularRefraction;
    RenderTexture*               _specularRefractionHistoryBuffer;
    RenderTexture*               _compositor;
    ComPtr<ID3D12Resource>       _hemisphereSamplesUpload;
    D3DBuffer*                   _hemisphereSamplesGPUBuffer;
    bool                         _useSegmentedPathTracer = true;
    SVGFDenoiser*                _svgfDenoiser;
    UINT                         _frameIndex;
    UINT                         _numSampleSets = 83;
    Samplers::MultiJittered      _randomSampler;
    std::mt19937                 _generatorURNG;
    bool                         _denoising;
    DXRStateObject*              _dxrStateObject;

    void _updateGameState(EngineStateFlags state);
    void _updateKeyboard(int key, int x, int y);

  public:
    PathTracerShader(std::string shaderName);
    virtual ~PathTracerShader();
    void           runShader(std::vector<Light*>&  lights,
                             ViewEventDistributor* viewEventDistributor);
    RenderTexture* getCompositedFrame();
};
