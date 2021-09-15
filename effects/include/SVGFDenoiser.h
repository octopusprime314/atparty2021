/*
 * SVGFDenoiser is part of the ReBoot distribution
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

/**
 *  SVGFDenoiser class is based off of the Microsoft Samples Denoiser.
 */

#pragma once
#include "Light.h"
#include "Shader.h"
#include "ViewEventDistributor.h"
#include "DXLayer.h"
class HLSLShader;

class SVGFDenoiser
{
    ComPtr<ID3D12DescriptorHeap> _descriptorHeap;
    UINT                         _descriptorSize;

    int                          _reflectionMode;
    int                          _shadowMode;
    int                          _viewMode;
    EngineStateFlags             _gameState;

    HLSLShader*                  _motionVectorsShader;
    HLSLShader*                  _meanVarianceShader;
    HLSLShader*                  _atrousWaveletFilterShader;
    HLSLShader*                  _temporalAccumulationSuperSamplingShader;

    RenderTexture*               _motionVectorsUVCoords;
    RenderTexture*               _meanVariance;
    RenderTexture*               _partialDistanceDerivatives;
    RenderTexture*               _atrousWaveletFilter;
    RenderTexture*               _colorHistoryBuffer;
    RenderTexture*               _occlusionHistoryBuffer;
    RenderTexture*               _inTemporalSamplesPerPixel;
    RenderTexture*               _outTemporalSamplesPerPixel;
    RenderTexture*               _debug0UAV;
    RenderTexture*               _debug1UAV;
 
    void _updateGameState(EngineStateFlags state);
    void _updateKeyboard(int key, int x, int y);

  public:
    SVGFDenoiser();
    virtual ~SVGFDenoiser();
    void denoise(ViewEventDistributor* viewEventDistributor,
                 RenderTexture* ambientOcclusionSRV,
                 RenderTexture* positionSRV,
                 RenderTexture* normalSRV);

    void computeMotionVectors(ViewEventDistributor* viewEventDistributor,
                              RenderTexture* positionSRV);

    RenderTexture* getColorHistoryBuffer();
    RenderTexture* getOcclusionHistoryBuffer();
    RenderTexture* getDenoisedResult();
    RenderTexture* getMotionVectors();
};
