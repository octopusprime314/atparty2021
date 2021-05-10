/*
 * TAA is part of the ReBoot distribution (https://github.com/octopusprime314/ReBoot.git).
 * Copyright (c) 2018 Peter Morley.
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
 *  TAA class.  Temporal Anti Aliasing
 */

#pragma once
#include "RenderTexture.h"
#include "SSAOShader.h"
#include "SSCompute.h"
#include "Vector4.h"
#include <vector>
class MRTFrameBuffer;
class ViewEventDistributor;

class TAA
{

    void                 _generateKernelNoise();
    RenderTexture        _renderTexture;
    unsigned int         _noiseTexture;
    //TAAShader*          _taaShader;
    std::vector<Vector4> _taaKernel;
    SSCompute*           _downSample;
    std::vector<Vector4> _taaNoise;
    SSCompute*           _upSample;
    unsigned int         _ssaoFBO;
    AssetTexture*        _noise;
    SSCompute*           _blur;

    float lerp(float a, float b, float f);

  public:
    TAA();
    ~TAA();
    void     computeTAA(MRTFrameBuffer* mrtBuffer, ViewEventDistributor* viewEventDistributor);
    Texture* getNoiseTexture();
    Texture* getTAATexture();
    std::vector<Vector4>& getKernel();
    SSCompute*            getBlur();
};