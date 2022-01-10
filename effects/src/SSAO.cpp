#include "SSAO.h"
#include "DXLayer.h"
#include "EngineManager.h"
#include "HLSLShader.h"
#include "MRTFrameBuffer.h"
#include "ShaderBroker.h"
#include "ViewEventDistributor.h"
#include <random>

SSAO::SSAO()
    : _renderTexture(IOEventDistributor::screenPixelWidth, IOEventDistributor::screenPixelHeight,
                     TextureFormat::R_FLOAT),

      _ssaoShader(static_cast<SSAOShader*>(ShaderBroker::instance()->getShader("ssaoShader")))
{

    _blur = new SSCompute("blurShader", IOEventDistributor::screenPixelWidth / 4,
                          IOEventDistributor::screenPixelHeight / 4, TextureFormat::R_FLOAT);

    _downSample = new SSCompute("downsample", IOEventDistributor::screenPixelWidth / 4,
                                IOEventDistributor::screenPixelHeight / 4, TextureFormat::R_FLOAT);

    _upSample = new SSCompute("upsample", IOEventDistributor::screenPixelWidth,
                              IOEventDistributor::screenPixelHeight, TextureFormat::R_FLOAT);
    //_generateKernelNoise();
}

SSAO::~SSAO() {}

// helper lerp, move to math function utility someday
float SSAO::lerp(float a, float b, float f) { return a + f * (b - a); }

void SSAO::_generateKernelNoise()
{
    // random floats between 0.0 - 1.0
    std::uniform_real_distribution<float> randomFloats(0.0, 1.0);
    std::default_random_engine            generator;

    for (unsigned int i = 0; i < 64; ++i)
    {
        Vector4 sample(randomFloats(generator) * 2.0f - 1.0f, randomFloats(generator) * 2.0f - 1.0f,
                       randomFloats(generator), 1.0f);
        sample.normalize();

        sample      = sample * randomFloats(generator);
        float scale = static_cast<float>(i) / 64.0f;

        scale  = lerp(0.1f, 1.0f, scale * scale);
        sample = sample * scale;

        _ssaoKernel.push_back(sample);
    }

    for (unsigned int i = 0; i < 16; i++)
    {

        Vector4 noise(randomFloats(generator) * 2.0f - 1.0f, randomFloats(generator) * 2.0f - 1.0f,
                      0.0f, 1.0f);

        _ssaoNoise.push_back(noise);
    }


    _noise = new AssetTexture(&_ssaoNoise[0], 4, 4, DXLayer::instance()->getAttributeBufferCopyCmdList(),
                                DXLayer::instance()->getDevice());
}

void SSAO::computeSSAO(MRTFrameBuffer* mrtBuffer, ViewEventDistributor* viewEventDistributor)
{

    HLSLShader::setOM({_renderTexture}, IOEventDistributor::screenPixelWidth,
                        IOEventDistributor::screenPixelHeight);

    _ssaoShader->runShader(this, mrtBuffer, viewEventDistributor);

    HLSLShader::releaseOM({_renderTexture});

    auto cmdList      = DXLayer::instance()->getCmdList();
    auto renderTarget = _renderTexture.getResource()->getResource();

    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                    renderTarget.Get(), D3D12_RESOURCE_STATE_COMMON,
                                    D3D12_RESOURCE_STATE_GENERIC_READ));

    // Downsample by a 1/4
    _downSample->compute(&_renderTexture);

    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                    renderTarget.Get(), D3D12_RESOURCE_STATE_GENERIC_READ,
                                    D3D12_RESOURCE_STATE_COMMON));

    cmdList->ResourceBarrier(
        1, &CD3DX12_RESOURCE_BARRIER::UAV(
                _downSample->getTexture()->getResource()->getResource().Get()));

    // Blur in downsampled
    _blur->compute(_downSample->getTexture());

    auto blurTarget = _blur->getTexture()->getResource()->getResource();
    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(blurTarget.Get()));

    // upsample back to original
    _upSample->compute(_blur->getTexture());

    auto upSampleTarget = _upSample->getTexture()->getResource()->getResource();

    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(upSampleTarget.Get()));
}

Texture* SSAO::getNoiseTexture() { return _noise; }

std::vector<Vector4>& SSAO::getKernel() { return _ssaoKernel; }

SSCompute* SSAO::getBlur() { return _upSample; }

Texture* SSAO::getSSAOTexture() { return _upSample->getTexture(); }