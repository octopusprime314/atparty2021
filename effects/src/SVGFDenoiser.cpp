#include "SVGFDenoiser.h"
#include "DXLayer.h"
#include "EngineManager.h"
#include "HLSLShader.h"
#include "Logger.h"
#include "RayTracingPipelineShader.h"
#include "Bloom.h"
#include "SSCompute.h"
#include <iomanip>

SVGFDenoiser::SVGFDenoiser()
{
    // Motion Vectors
    std::vector<DXGI_FORMAT>* formats = new std::vector<DXGI_FORMAT>();
    // UV motion vectors in R32G32 and B32A32 unused
    formats->push_back(DXGI_FORMAT_R32G32B32A32_FLOAT);
    formats->push_back(DXGI_FORMAT_R32G32B32A32_FLOAT);
    formats->push_back(DXGI_FORMAT_R32G32B32A32_FLOAT);

    _motionVectorsShader =
        new HLSLShader(SHADERS_LOCATION + "hlsl/cs/motionVectorsCS", "", formats);

    formats->clear();
    formats->push_back(DXGI_FORMAT_R16G16_FLOAT);
    formats->push_back(DXGI_FORMAT_R16G16_FLOAT);

    _meanVarianceShader =
        new HLSLShader(SHADERS_LOCATION + "hlsl/cs/calculateMeanVarianceCS", "", formats);

    formats->clear();
    formats->push_back(DXGI_FORMAT_R16_FLOAT);

    _atrousWaveletFilterShader = new HLSLShader(
        SHADERS_LOCATION + "hlsl/cs/atrousWaveletTransformCrossBilateralFilterShaderCS", "",
        formats);

    formats->clear();
    formats->push_back(DXGI_FORMAT_R8_UINT);
    _temporalAccumulationSuperSamplingShader = new HLSLShader(
        SHADERS_LOCATION + "hlsl/cs/temporalAccumulationSuperSamplingCS", "", formats);

    _motionVectorsUVCoords =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                          IOEventDistributor::screenPixelHeight, TextureFormat::RGBA_FLOAT, "motionVector");

    _colorHistoryBuffer =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                                            IOEventDistributor::screenPixelHeight,
                                            TextureFormat::RGBA_UNSIGNED_BYTE, "colorHistoryBuffer");

    _occlusionHistoryBuffer =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                                                IOEventDistributor::screenPixelHeight,
                                                TextureFormat::R16G16_FLOAT, "_occlusionHistoryBuffer");

    _meanVariance =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                                      IOEventDistributor::screenPixelHeight,
                                      TextureFormat::R16G16_FLOAT, "_meanVariance");

    _atrousWaveletFilter =
    new RenderTexture(IOEventDistributor::screenPixelWidth,
                        IOEventDistributor::screenPixelHeight, TextureFormat::R16_FLOAT, "_atrousWaveletFilter");

    _partialDistanceDerivatives =
        new RenderTexture(IOEventDistributor::screenPixelWidth, IOEventDistributor::screenPixelHeight,
        TextureFormat::R16G16_FLOAT, "_partialDistanceDerivatives");

    _outTemporalSamplesPerPixel =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                        IOEventDistributor::screenPixelHeight, TextureFormat::R8_UINT, "_outTemporalSamplesPerPixel");

    _inTemporalSamplesPerPixel =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                          IOEventDistributor::screenPixelHeight, TextureFormat::R8_UINT, "_inTemporalSamplesPerPixel");

    _debug0UAV =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                          IOEventDistributor::screenPixelHeight, TextureFormat::RGBA_FLOAT, "_debug0UAV");

    _debug1UAV =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                          IOEventDistributor::screenPixelHeight, TextureFormat::RGBA_FLOAT,"_debug1UAV");

    using namespace std::placeholders;
    IOEvents::subscribeToKeyboard(this, std::bind(&SVGFDenoiser::_updateKeyboard, this, _1, _2, _3));
    IOEvents::subscribeToGameState(this, std::bind(&SVGFDenoiser::_updateGameState, this, _1));

    _gameState.worldEditorModeEnabled = false;
    _gameState.gameModeEnabled        = true;

    _shadowMode     = 1;
    _reflectionMode = 0;
    _viewMode       = 0;

    auto                   computeCmdList = DXLayer::instance()->getComputeCmdList();
    D3D12_RESOURCE_BARRIER barrierDesc[2];
    ZeroMemory(&barrierDesc, sizeof(barrierDesc));

    barrierDesc[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrierDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    barrierDesc[1] = barrierDesc[0];

    barrierDesc[0].Transition.pResource = _motionVectorsUVCoords->getResource()->getResource().Get();
    barrierDesc[1].Transition.pResource = _occlusionHistoryBuffer->getResource()->getResource().Get();

    computeCmdList->ResourceBarrier(2, barrierDesc);
}

SVGFDenoiser::~SVGFDenoiser() {}

void SVGFDenoiser::_updateGameState(EngineStateFlags state) { _gameState = state; }

RenderTexture* SVGFDenoiser::getColorHistoryBuffer() { return _colorHistoryBuffer; }
RenderTexture* SVGFDenoiser::getOcclusionHistoryBuffer() { return _occlusionHistoryBuffer; }
RenderTexture* SVGFDenoiser::getDenoisedResult() { return _atrousWaveletFilter; }

void SVGFDenoiser::_updateKeyboard(int key, int x, int y)
{
    if (_gameState.worldEditorModeEnabled)
    {
        return;
    }
}

void SVGFDenoiser::computeMotionVectors(ViewEventDistributor* viewEventDistributor,
                                        RenderTexture*        positionSRV)
{
    RayTracingPipelineShader* rtPipeline = EngineManager::getRTPipeline();

    auto cmdList = DXLayer::instance()->getCmdList();

    // Clear motion vector UAV
    float clearValues[] = {0.0, 0.0, 0.0, 0.0};
    cmdList->ClearUnorderedAccessViewFloat(
        _motionVectorsUVCoords->getUAVGPUHandle(), _motionVectorsUVCoords->getUAVCPUHandle(),
        _motionVectorsUVCoords->getResource()->getResource().Get(), clearValues, 0, nullptr);

    // Motion Vectors
    cmdList->BeginEvent(0, L"Motion Vectors", sizeof(L"Motion Vectors"));

    HLSLShader* shader = _motionVectorsShader;

    shader->bind();
    // SRVs
    shader->updateData("positionSRV",      0, positionSRV,               true, false);

    // UAVs
    shader->updateData("motionVectorsUAV", 0, _motionVectorsUVCoords, true, true);

    auto cameraView            = viewEventDistributor->getView();
    auto cameraProj            = viewEventDistributor->getProjection();
    auto inverseCameraView     = cameraView.inverse();
    auto inverseCameraProj     = cameraProj.inverse();

    shader->updateData("inverseView", inverseCameraView.getFlatBuffer(), true);
    shader->updateData("inverseProj", inverseCameraProj.getFlatBuffer(), true);
    shader->updateData("prevFrameCameraPosition", viewEventDistributor->getPrevCameraPos().getFlatBuffer(), true);

    auto prevCameraView = cameraProj * viewEventDistributor->getPrevCameraView();
    shader->updateData("prevFrameViewProj", prevCameraView.getFlatBuffer(), true);

    float screenSize[] = {static_cast<float>(IOEventDistributor::screenPixelWidth),
                          static_cast<float>(IOEventDistributor::screenPixelHeight)};

    shader->updateData("screenSize", screenSize, true);

    shader->updateData("prevInstanceWorldMatrixTransforms", rtPipeline->getPrevInstanceTransforms(), true);
    shader->updateData("instanceWorldToObjectSpaceMatrixTransforms", rtPipeline->getWorldToObjectTransforms(), true);

    auto threadGroupSize = shader->getThreadGroupSize();

    shader->dispatch(ceilf(static_cast<float>(_motionVectorsUVCoords->getWidth())  / threadGroupSize.getx()),
                     ceilf(static_cast<float>(_motionVectorsUVCoords->getHeight()) / threadGroupSize.gety()),
                     threadGroupSize.getz());

    shader->unbind();

    cmdList->EndEvent();
}

void SVGFDenoiser::denoise(ViewEventDistributor* viewEventDistributor,
                           RenderTexture*        ambientOcclusionSRV,
                           RenderTexture*        positionSRV,
                           RenderTexture*        normalSRV)
{
    RayTracingPipelineShader* rtPipeline = EngineManager::getRTPipeline();

    auto cmdList = DXLayer::instance()->getCmdList();

    // Mean Variance
    cmdList->BeginEvent(0, L"Mean Variance", sizeof(L"Mean Variance"));

    HLSLShader* shader = _meanVarianceShader;

    shader->bind();
    // SRVs
    shader->updateData("inputTextureSRV", 0, ambientOcclusionSRV, true, false);
    shader->updateData("positionSRV", 0, positionSRV, true, false);

    // UAVs
    shader->updateData("outputMeanVarianceTextureUAV", 0, _meanVariance, true, true);
    shader->updateData("outputPartialDistanceDerivatesUAV", 0, _partialDistanceDerivatives, true, true);

    unsigned int screenSize[] = {static_cast<unsigned int>(IOEventDistributor::screenPixelWidth ),
                                 static_cast<unsigned int>(IOEventDistributor::screenPixelHeight)};
    shader->updateData("screenSize", screenSize, true);

    UINT kernelWidth  = 9;
    UINT kernelRadius = 4;
    UINT pixelStepY   = 1;
    shader->updateData("kernelWidth", &kernelWidth, true);
    shader->updateData("kernelRadius", &kernelRadius, true);
    shader->updateData("pixelStepY", &pixelStepY, true);

    auto cameraView  = viewEventDistributor->getView();
    auto inverseCameraView = cameraView.inverse();
    shader->updateData("inverseView", inverseCameraView.getFlatBuffer(), true);

    auto threadGroupSize = shader->getThreadGroupSize();

    shader->dispatch(
        ceilf(static_cast<float>(_meanVariance->getWidth()) / threadGroupSize.getx()),
        ceilf(static_cast<float>(_meanVariance->getHeight()) / threadGroupSize.gety()),
        threadGroupSize.getz());

    shader->unbind();

    cmdList->EndEvent();

    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(nullptr));



    
    // Temporal super sampling caching
    cmdList->BeginEvent(0, L"Temporal super sampling caching", sizeof(L"Temporal super sampling caching"));

    shader = _temporalAccumulationSuperSamplingShader;

    shader->bind();
    // SRVs
    shader->updateData("textureSpaceMotionVectorAndDepth", 0, _motionVectorsUVCoords, true, false);
    shader->updateData("normalSRV", 0, normalSRV, true, false);
    shader->updateData("partialDistanceDerivatesSRV", 0, _partialDistanceDerivatives, true, false);
    //shader->updateData("positionSRV", 0, positionSRV, true, false);
    shader->updateData("occlusionAndHitDistanceSRV", 0, ambientOcclusionSRV, true, false);
    shader->updateData("occlusionHistoryBufferSRV", 0, _occlusionHistoryBuffer, true, false);
    shader->updateData("meanVarianceSRV", 0, _meanVariance, true, false);
    shader->updateData("temporalSamplesPerPixelSRV", 0, _inTemporalSamplesPerPixel, true, false);


    // UAVs
    shader->updateData("temporalSamplesPerPixelUAV", 0, _outTemporalSamplesPerPixel, true, true);
    shader->updateData("debug0UAV", 0, _debug0UAV, true, true);
    shader->updateData("debug1UAV", 0, _debug1UAV, true, true);

    shader->updateData("screenSize", screenSize, true);

    threadGroupSize = shader->getThreadGroupSize();

    shader->dispatch(ceilf(static_cast<float>(_meanVariance->getWidth()) / threadGroupSize.getx()),
                     ceilf(static_cast<float>(_meanVariance->getHeight()) / threadGroupSize.gety()),
                     threadGroupSize.getz());

    shader->unbind();

    cmdList->EndEvent();

    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(nullptr));

    cmdList->CopyResource(_inTemporalSamplesPerPixel->getResource()->getResource().Get(),
                          _outTemporalSamplesPerPixel->getResource()->getResource().Get());

    // Atrous Wavelet Filter
    cmdList->BeginEvent(0, L"Atrous Wavelet Filter", sizeof(L"Atrous Wavelet Filter"));

    shader = _atrousWaveletFilterShader;

    shader->bind();
    // SRVs
    shader->updateData("occlusionAndHitDistanceSRV", 0, ambientOcclusionSRV, true, false);
    shader->updateData("meanVarianceSRV", 0, _meanVariance, true, false);
    shader->updateData("normalSRV", 0, normalSRV, true, false);
    shader->updateData("positionSRV", 0, positionSRV, true, false);
    shader->updateData("partialDistanceDerivatives", 0, _partialDistanceDerivatives, true, false);

    // UAVs
    shader->updateData("outputFilteredUAV", 0, _atrousWaveletFilter, true, true);

    float depthWeightCutoff = 0.2;
    bool  usingBilateralDownsampledBuffers = false;
    int   useAdaptiveKernelSize = true;
    float kernelRadiusLerfCoef = 0.666667;
    int   minKernelWidth = 3;
    int   maxKernelWidth = 28;
    float rayHitDistanceToKernelWidthScale = 0.02;
    float rayHitDistanceToKernelSizeScaleExponent = 2;
    int   perspectiveCorrectDepthInterpolation = 1;
    float minVarianceToDenoise = 0;
    float valueSigma  = 1;
    float depthSigma  = 1;
    float normalSigma = 64;
    int   depthNumMantissaBits = 10;
    
    shader->updateData("screenSize", screenSize, true);
    shader->updateData("depthWeightCutoff", &depthWeightCutoff, true);
    shader->updateData("usingBilateralDownsampledBuffers", &usingBilateralDownsampledBuffers, true);
    shader->updateData("useAdaptiveKernelSize", &useAdaptiveKernelSize, true);
    shader->updateData("kernelRadiusLerfCoef", &kernelRadiusLerfCoef, true);
    shader->updateData("minKernelWidth", &minKernelWidth, true);
    shader->updateData("maxKernelWidth", &maxKernelWidth, true);
    shader->updateData("rayHitDistanceToKernelWidthScale", &rayHitDistanceToKernelWidthScale, true);
    shader->updateData("rayHitDistanceToKernelSizeScaleExponent", &rayHitDistanceToKernelSizeScaleExponent, true);
    shader->updateData("perspectiveCorrectDepthInterpolation", &perspectiveCorrectDepthInterpolation, true);
    shader->updateData("minVarianceToDenoise", &minVarianceToDenoise, true);
    shader->updateData("valueSigma", &valueSigma, true);
    shader->updateData("depthSigma", &depthSigma, true);
    shader->updateData("normalSigma", &normalSigma, true);
    shader->updateData("depthNumMantissaBits", &depthNumMantissaBits, true);

    threadGroupSize = shader->getThreadGroupSize();

    shader->dispatch(ceilf(static_cast<float>(_atrousWaveletFilter->getWidth()) / threadGroupSize.getx()),
                     ceilf(static_cast<float>(_atrousWaveletFilter->getHeight()) / threadGroupSize.gety()),
                     threadGroupSize.getz());

    shader->unbind();

    cmdList->EndEvent();

    cmdList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::UAV(nullptr));
}
