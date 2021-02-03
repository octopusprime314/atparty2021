#include "PathTracerShader.h"
#include "DXLayer.h"
#include "EngineManager.h"
#include "HLSLShader.h"
#include "Logger.h"
#include "RayTracingPipelineShader.h"
#include "Bloom.h"
#include "SSCompute.h"
#include "TextureBroker.h"
#include "Model.h"
#include <iomanip>

PathTracerShader::PathTracerShader(std::string shaderName)
{

    _denoising = false;
    if (_denoising)
    {
        _svgfDenoiser = new SVGFDenoiser();
    }

    // Primary Rays

    // Segments the path tracer into separate passes that help improve coherency
    std::vector<DXGI_FORMAT>* primaryRaysFormats = new std::vector<DXGI_FORMAT>();
    // Albedo in R8G8B8 and maybe transparency in A8 to indicate water or something...
    primaryRaysFormats->push_back(DXGI_FORMAT_R8G8B8A8_UNORM);
    // Position in R32G32B32 and A32 unused
    primaryRaysFormats->push_back(DXGI_FORMAT_R32G32B32A32_FLOAT);
    // Normals in R32G32B32 and roughness in A32 channel
    primaryRaysFormats->push_back(DXGI_FORMAT_R32G32B32A32_FLOAT);

    _primaryRaysShader = new HLSLShader(
        DXR1_1_SHADERS_LOCATION + "primaryRaysShaderCS", "", primaryRaysFormats);

    _albedoPrimaryRays =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                          IOEventDistributor::screenPixelHeight, TextureFormat::RGBA_UNSIGNED_BYTE, "_albedoPrimaryRays");
    _normalPrimaryRays =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                          IOEventDistributor::screenPixelHeight, TextureFormat::RGBA_FLOAT, "_normalPrimaryRays");
    _positionPrimaryRays =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                          IOEventDistributor::screenPixelHeight, TextureFormat::RGBA_FLOAT, "_positionPrimaryRays");
    _occlusionRays =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                          IOEventDistributor::screenPixelHeight, TextureFormat::RGBA_FLOAT, "_occlusionRays");

    _indirectLightRays = new RenderTexture(IOEventDistributor::screenPixelWidth,
                                           IOEventDistributor::screenPixelHeight,
                                           TextureFormat::RGBA_UNSIGNED_BYTE, "_indirectLightRays");

    _indirectLightRaysHistoryBuffer = new RenderTexture(IOEventDistributor::screenPixelWidth,
                                           IOEventDistributor::screenPixelHeight,
                                           TextureFormat::RGBA_UNSIGNED_BYTE, "_indirectLightRaysHistoryBuffer");

    // Sun lighting rays and occlusion

     // Segments the path tracer into separate passes that help improve coherency
    std::vector<DXGI_FORMAT>* sunLightRaysFormats = new std::vector<DXGI_FORMAT>();
    // Albedo in R8G8B8 and maybe transparency in A8 to indicate water or something...
    sunLightRaysFormats->push_back(DXGI_FORMAT_R8G8B8A8_UNORM);
    sunLightRaysFormats->push_back(DXGI_FORMAT_R32G32B32A32_FLOAT);
    sunLightRaysFormats->push_back(DXGI_FORMAT_R16G16_FLOAT);
    sunLightRaysFormats->push_back(DXGI_FORMAT_R8G8B8A8_UNORM);
    sunLightRaysFormats->push_back(DXGI_FORMAT_R8G8B8A8_UNORM);

    _sunLightRaysShader =
        new HLSLShader(DXR1_1_SHADERS_LOCATION + "sunLightRaysShaderCS", "", sunLightRaysFormats);

    _sunLightRays =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                                      IOEventDistributor::screenPixelHeight,
                                      TextureFormat::RGBA_UNSIGNED_BYTE, "_sunlightRays");

    // Reflection rays shading

    std::vector<DXGI_FORMAT>* reflectionRaysFormats = new std::vector<DXGI_FORMAT>();
    // Albedo in R8G8B8 and maybe transparency in A8 to indicate water or something...
    reflectionRaysFormats->push_back(DXGI_FORMAT_R8G8B8A8_UNORM);
    reflectionRaysFormats->push_back(DXGI_FORMAT_R32G32B32A32_FLOAT);

    _reflectionRaysShader = new HLSLShader(DXR1_1_SHADERS_LOCATION + "reflectionRaysShaderCS", "",
                                           reflectionRaysFormats);

    _reflectionRays =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                          IOEventDistributor::screenPixelHeight, TextureFormat::RGBA_UNSIGNED_BYTE, "_reflectionRays");

    _pointLightOcclusion =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                                             IOEventDistributor::screenPixelHeight,
                                             TextureFormat::RGBA_UNSIGNED_BYTE, "_pointLightOcclusion");

    _pointLightOcclusionHistory =
        new RenderTexture(IOEventDistributor::screenPixelWidth, IOEventDistributor::screenPixelHeight,
        TextureFormat::RGBA_UNSIGNED_BYTE, "_pointLightOcclusionHistory");

    // Composite all ray tracing passes into a final render

    std::vector<DXGI_FORMAT>* compositorFormats = new std::vector<DXGI_FORMAT>();
    // Albedo in R8G8B8 and maybe transparency in A8 to indicate water or something...
    compositorFormats->push_back(DXGI_FORMAT_R8G8B8A8_UNORM);

    _compositorShader =
        new HLSLShader(SHADERS_LOCATION + "hlsl/cs/compositorShaderCS", "", compositorFormats);

    _compositor =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                          IOEventDistributor::screenPixelHeight, TextureFormat::RGBA_UNSIGNED_BYTE, "_compositor");

    // Create skybox texture
    TextureBroker* textureManager = TextureBroker::instance();

    using namespace std::placeholders;
    IOEvents::subscribeToKeyboard(this, std::bind(&PathTracerShader::_updateKeyboard, this, _1, _2, _3));
    IOEvents::subscribeToGameState(this, std::bind(&PathTracerShader::_updateGameState, this, _1));

    _gameState.worldEditorModeEnabled = false;
    _gameState.gameModeEnabled        = true;

    _shadowMode     = 1;
    _reflectionMode = 0;
    _viewMode       = 0;
    _frameIndex     = 0;

    auto texBroker = TextureBroker::instance();

    _generatorURNG.seed(1729);

    UINT pixelsInSampleSet1D = 8;
    UINT samplesPerSet       = 64;
    _randomSampler.Reset(samplesPerSet, _numSampleSets,
                            Samplers::HemisphereDistribution::Cosine);

    UINT numSamples = _randomSampler.NumSamples() * _randomSampler.NumSampleSets();

    RayTracingPipelineShader* rtPipeline = EngineManager::getRTPipeline();

    rtPipeline->allocateUploadBuffer(DXLayer::instance()->getDevice().Get(), nullptr,
                                        sizeof(float) * 4 * numSamples, &_hemisphereSamplesUpload,
                                        L"HemisphereSamples");

    _hemisphereSamplesGPUBuffer = new D3DBuffer();

    _hemisphereSamplesGPUBuffer->resource = _hemisphereSamplesUpload;
    _hemisphereSamplesGPUBuffer->count    = numSamples;

    UINT descriptorVB = rtPipeline->createBufferSRV(_hemisphereSamplesGPUBuffer, numSamples,
                                    static_cast<UINT>(sizeof(float) * 4), DXGI_FORMAT_UNKNOWN);

    BYTE* mappedData = nullptr;
    _hemisphereSamplesUpload->Map(0, nullptr, reinterpret_cast<void**>(&mappedData));

    for (UINT i = 0; i < numSamples; i++)
    {
        Vector4 p = Vector4(_randomSampler.GetHemisphereSample3D().x,
                            _randomSampler.GetHemisphereSample3D().y,
                            _randomSampler.GetHemisphereSample3D().z);

        memcpy(&mappedData[i * sizeof(float) * 4], &p, sizeof(float) * 4);
    }

    _hemisphereSamplesUpload->Unmap(0, nullptr);
    mappedData = nullptr;

    auto computeCmdList = DXLayer::instance()->getComputeCmdList();
    D3D12_RESOURCE_BARRIER barrierDesc[7];
    ZeroMemory(&barrierDesc, sizeof(barrierDesc));

    barrierDesc[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrierDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    barrierDesc[6] = barrierDesc[5] = barrierDesc[4] = barrierDesc[3] =
        barrierDesc[2] = barrierDesc[1] = barrierDesc[0];

    barrierDesc[0].Transition.pResource = _reflectionRays->getResource()->getResource().Get();
    barrierDesc[1].Transition.pResource = _occlusionRays->getResource()->getResource().Get();
    barrierDesc[2].Transition.pResource = _sunLightRays->getResource()->getResource().Get();
    barrierDesc[3].Transition.pResource = _albedoPrimaryRays->getResource()->getResource().Get();
    barrierDesc[4].Transition.pResource = _positionPrimaryRays->getResource()->getResource().Get();
    barrierDesc[5].Transition.pResource = _normalPrimaryRays->getResource()->getResource().Get();
    barrierDesc[6].Transition.pResource = _compositor->getResource()->getResource().Get();

    computeCmdList->ResourceBarrier(7, barrierDesc);

    _dxrStateObject = new DXRStateObject(_primaryRaysShader->getRootSignature(),
                                         _reflectionRaysShader->getRootSignature());
}

PathTracerShader::~PathTracerShader() {}

void PathTracerShader::_updateGameState(EngineStateFlags state) { _gameState = state; }

void PathTracerShader::_updateKeyboard(int key, int x, int y)
{
    if (_gameState.worldEditorModeEnabled)
    {
        return;
    }
}

RenderTexture* PathTracerShader::getCompositedFrame()
{
    return _compositor;
}

void PathTracerShader::runShader(std::vector<Light*>&  lights,
                                 ViewEventDistributor* viewEventDistributor)
{
    RayTracingPipelineShader* rtPipeline = EngineManager::getRTPipeline();

    DXLayer::instance()->setTimeStamp();

    rtPipeline->buildAccelerationStructures();

    HLSLShader* shader = static_cast<HLSLShader*>(_shader);
    auto        cmdList = DXLayer::instance()->getCmdList();

    UINT threadGroupWidth  = 8.0;
    UINT threadGroupHeight = 8.0;

    DXLayer::instance()->setTimeStamp();

    // Clear occlusion, point light, sun light and reflection UAVs
    float zeroValues[] = {0.0, 0.0, 0.0, 0.0};
    float oneValues[] = {1.0, 0.0, 0.0, 0.0};
    cmdList->ClearUnorderedAccessViewFloat(
        _occlusionRays->getUAVGPUHandle(),
        _occlusionRays->getUAVCPUHandle(),
        _occlusionRays->getResource()->getResource().Get(), oneValues, 0, nullptr);

    cmdList->ClearUnorderedAccessViewFloat(
        _reflectionRays->getUAVGPUHandle(),
        _reflectionRays->getUAVCPUHandle(),
        _reflectionRays->getResource()->getResource().Get(), zeroValues, 0,
        nullptr);

    cmdList->ClearUnorderedAccessViewFloat(
        _sunLightRays->getUAVGPUHandle(),
        _sunLightRays->getUAVCPUHandle(),
        _sunLightRays->getResource()->getResource().Get(), zeroValues, 0,
        nullptr);

    // Primary rays
    cmdList->BeginEvent(0, L"Primary Rays", sizeof(L"Primary Rays"));

    shader = _primaryRaysShader;

    shader->bind();
    shader->updateData("albedoUAV", 0, _albedoPrimaryRays, true, true);
    shader->updateData("positionUAV", 0, _positionPrimaryRays, true, true);
    shader->updateData("normalUAV", 0, _normalPrimaryRays, true, true);

    auto cameraView        = viewEventDistributor->getView();
    auto inverseCameraView = cameraView.inverse();

    auto cameraProj        = viewEventDistributor->getProjection();
    auto inverseCameraProj = cameraProj.inverse();

    shader->updateData("inverseView", inverseCameraView.getFlatBuffer(), true);
    shader->updateData("viewTransform", cameraView.getFlatBuffer(), true);

    auto deltaCameraView      = viewEventDistributor->getPrevCameraView() - cameraView;
    Vector4 deltaCameraVector = deltaCameraView * Vector4(1.0, 1.0, 1.0);
    bool   isCameraMoving     = deltaCameraVector.getMagnitude() >= 0.5 ? true : false;

    float screenSize[] = {static_cast<float>(IOEventDistributor::screenPixelWidth),
                            static_cast<float>(IOEventDistributor::screenPixelHeight)};

    shader->updateData("screenSize", screenSize, true);
    int texturesPerMaterial = Material::TexturesPerMaterial;
    shader->updateData("texturesPerMaterial", &texturesPerMaterial, true);

    //// Get skybox texture
    //TextureBroker* textureManager = TextureBroker::instance();
    //auto           skyBoxTexture  = textureManager->getTexture(TEXTURE_LOCATION + "skybox-day");
    //shader->updateData("skyboxTexture", 0, skyBoxTexture, true);

    shader->updateRTAS("rtAS", rtPipeline->getRTASDescHeap(), rtPipeline->getRTASGPUVA(), true);

    rtPipeline->updateTextureUnbounded(shader->_resourceIndexes["diffuseTexture"], 0, nullptr, 0, true);
    rtPipeline->updateStructuredAttributeBufferUnbounded(shader->_resourceIndexes["vertexBuffer"], nullptr, true);
    rtPipeline->updateStructuredIndexBufferUnbounded(shader->_resourceIndexes["indexBuffer"], nullptr, true);

    rtPipeline->updateAndBindMaterialBuffer(shader->_resourceIndexes, true);
    rtPipeline->updateAndBindAttributeBuffer(shader->_resourceIndexes, true);
    rtPipeline->updateAndBindNormalMatrixBuffer(shader->_resourceIndexes, true);
    rtPipeline->updateAndBindUniformMaterialBuffer(shader->_resourceIndexes, true);

    if (EngineManager::getGraphicsLayer() == GraphicsLayer::DXR_1_1_PATHTRACER)
    {
        shader->dispatch(ceilf(screenSize[0] / static_cast<float>(threadGroupWidth)),
                         ceilf(screenSize[1] / static_cast<float>(threadGroupHeight)), 1);
    }
    else if (EngineManager::getGraphicsLayer() == GraphicsLayer::DXR_1_0_PATHTRACER)
    {
        _dxrStateObject->dispatchPrimaryRays();
    }

    shader->unbind();

    cmdList->EndEvent();

    // For some reason the barriers need to be performed when a root signature is bound????  Why? Makes no sense but fixes nsight
    D3D12_RESOURCE_BARRIER barrierDesc[12];
    ZeroMemory(&barrierDesc, sizeof(barrierDesc));

    barrierDesc[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc[0].Transition.pResource   =_albedoPrimaryRays->getResource()->getResource().Get();
    barrierDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrierDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_GENERIC_READ;

    barrierDesc[2] = barrierDesc[1] = barrierDesc[0];
    barrierDesc[1].Transition.pResource = _positionPrimaryRays->getResource()->getResource().Get();
    barrierDesc[2].Transition.pResource = _normalPrimaryRays->getResource()->getResource().Get();

    cmdList->ResourceBarrier(3, barrierDesc);

    // Process point lights
    PointLightList pointLightList;
    EngineManager::instance()->processLights(lights, viewEventDistributor, pointLightList, RandomInsertAndRemoveEntities);

    // Process directional light
    Vector4 sunLightColor  = Vector4(1.0, 1.0, 1.0);
    float   sunLightRange  = 100000.0;
    Vector4 sunLightPos    = cameraView * Vector4(700.0, 700.0, 0.0);
    float   sunLightRadius = 100.0f;

    for (auto& light : lights)
    {
        // If shadowed directional then we know we have the sun :)
        if (light->getType() == LightType::SHADOWED_DIRECTIONAL)
        {
            // Point lights need to remain stationary so move lights with camera space changes
            sunLightPos   = light->getPosition();
            sunLightColor = light->getColor();
            // sunLightRange = light->getRange();
            break;
        }
    }

    std::uniform_int_distribution<UINT> seedDistribution(0, UINT_MAX);
    UINT seed                  = seedDistribution(_generatorURNG);
    UINT numSamplesPerSet      = 64;
    UINT numSampleSets         = 83;
    UINT numPixelsPerDimPerSet = 8;

    DXLayer::instance()->setTimeStamp();

    if (_denoising)
    {
        _svgfDenoiser->computeMotionVectors(viewEventDistributor, _positionPrimaryRays);
    }

    cmdList->BeginEvent(0, L"Sun light Rays", sizeof(L"Sun light Rays"));

    //// Sun light rays

    //shader = _sunLightRaysShader;

    //shader->bind();

    //shader->updateData("albedoSRV", 0, _albedoPrimaryRays, true, false);
    //shader->updateData("positionSRV", 0, _positionPrimaryRays, true, false);
    //shader->updateData("normalSRV", 0, _normalPrimaryRays, true, false);

    //auto resourceBindings  = shader->_resourceIndexes;
    //ID3D12DescriptorHeap* descriptorHeaps[] = {rtPipeline->getDescHeap().Get()};
    //cmdList->SetDescriptorHeaps(1, descriptorHeaps);
    //
    //cmdList->SetComputeRootDescriptorTable(resourceBindings["sampleSets"],
    //                                       _hemisphereSamplesGPUBuffer->gpuDescriptorHandle);

    //auto              texBroker        = TextureBroker::instance();
    //const std::string noiseTextureName = "../assets/textures/noise/fluidnoise.png";
    //auto noiseTexture = texBroker->getTexture(noiseTextureName);

    ////shader->updateData("noiseSRV", 0, noiseTexture, true, false);

    //shader->updateData("sunLightUAV", 0, _sunLightRays, true, true);
    //shader->updateData("occlusionUAV", 0, _occlusionRays, true, true);
    //shader->updateData("occlusionHistoryUAV", 0, _svgfDenoiser->getOcclusionHistoryBuffer(), true, true);
    //shader->updateData("indirectLightRaysUAV", 0, _indirectLightRays, true, true);
    //shader->updateData("indirectLightRaysHistoryUAV", 0, _indirectLightRaysHistoryBuffer, true, true);

    //shader->updateData("debug0UAV", 0, _debug0, true, true);
    //shader->updateData("debug1UAV", 0, _debug1, true, true);
    //shader->updateData("debug2UAV", 0, _debug2, true, true);

    //shader->updateData("inverseView", inverseCameraView.getFlatBuffer(), true);

    //shader->updateRTAS("rtAS", rtPipeline->getRTASDescHeap(), rtPipeline->getRTASGPUVA(), true);

    //rtPipeline->updateTextureUnbounded(shader->_resourceIndexes["diffuseTexture"], 0, nullptr, 0, true);
    //rtPipeline->updateStructuredBufferUnbounded(shader->_resourceIndexes["vertexBuffer"], nullptr, true);

    //rtPipeline->updateAndBindMaterialBuffer(shader->_resourceIndexes);
    //rtPipeline->updateAndBindAttributeBuffer(shader->_resourceIndexes);
    //rtPipeline->updateAndBindNormalMatrixBuffer(shader->_resourceIndexes);

    //shader->updateData("numPointLights", &pointLights, true);
    //shader->updateData("pointLightColors", lightColorsArray, true);
    //shader->updateData("pointLightRanges", lightRangesArray, true);
    //shader->updateData("pointLightPositions", lightPosArray, true);

    //shader->updateData("sunLightColor", sunLightColor.getFlatBuffer(), true);
    //shader->updateData("sunLightRange", &sunLightRange, true);
    //shader->updateData("sunLightPosition", sunLightPos.getFlatBuffer(), true);
    //shader->updateData("sunLightRadius", &sunLightRadius, true);
    //shader->updateData("screenSize", screenSize, true);
    //shader->updateData("texturesPerMaterial", &texturesPerMaterial, true);

    //if (isCameraMoving)
    //{
    //    _frameIndex = 0;
    //}
    //shader->updateData("frameIndex", &_frameIndex, true);

    //float timeAsFloat = static_cast<float>(MasterClock::instance()->getGameTime()) / 1000.0f;
    //shader->updateData("time", &timeAsFloat, true);

    //shader->updateData("seed", &seed, true);
    //shader->updateData("numSamplesPerSet", &numSamplesPerSet, true);
    //shader->updateData("numSampleSets", &numSampleSets, true);
    //shader->updateData("numPixelsPerDimPerSet", &numPixelsPerDimPerSet, true);


    //shader->dispatch(
    //    ceilf(static_cast<float>(_sunLightRays->getWidth()) / threadGroupWidth),
    //    ceilf(static_cast<float>(_sunLightRays->getHeight()) / threadGroupHeight),
    //    1);

    //shader->unbind();

    cmdList->EndEvent();
    cmdList->BeginEvent(0, L"Reflection Rays", sizeof(L"Reflection Rays"));

    DXLayer::instance()->setTimeStamp();

    // Reflection rays

    shader = _reflectionRaysShader;

    shader->bind();
    shader->updateData("albedoSRV", 0, _albedoPrimaryRays, true, false);
    shader->updateData("positionSRV", 0, _positionPrimaryRays, true, false);
    shader->updateData("normalSRV", 0, _normalPrimaryRays, true, false);

    shader->updateData("reflectionUAV", 0, _reflectionRays, true, true);
    shader->updateData("pointLightOcclusionUAV", 0, _pointLightOcclusion, true, true);
    shader->updateData("pointLightOcclusionHistoryUAV", 0, _pointLightOcclusionHistory, true, true);
    shader->updateData("debugUAV", 0, _occlusionRays, true, true);

    shader->updateRTAS("rtAS", rtPipeline->getRTASDescHeap(), rtPipeline->getRTASGPUVA(), true);

    shader->updateData("inverseView", inverseCameraView.getFlatBuffer(), true);

    rtPipeline->updateTextureUnbounded(shader->_resourceIndexes["diffuseTexture"], 0, nullptr, 0, true);
    rtPipeline->updateStructuredAttributeBufferUnbounded(shader->_resourceIndexes["vertexBuffer"], nullptr, true);
    rtPipeline->updateStructuredIndexBufferUnbounded(shader->_resourceIndexes["indexBuffer"], nullptr, true);

    rtPipeline->updateAndBindMaterialBuffer(shader->_resourceIndexes, true);
    rtPipeline->updateAndBindAttributeBuffer(shader->_resourceIndexes, true);
    rtPipeline->updateAndBindNormalMatrixBuffer(shader->_resourceIndexes, true);
    rtPipeline->updateAndBindUniformMaterialBuffer(shader->_resourceIndexes, true);

    shader->updateData("numPointLights", &pointLightList.lightCount, true);
    shader->updateData("pointLightColors", pointLightList.lightColorsArray, true);
    shader->updateData("pointLightRanges", pointLightList.lightRangesArray, true);
    shader->updateData("pointLightPositions", pointLightList.lightPosArray, true);

    shader->updateData("texturesPerMaterial", &texturesPerMaterial, true);

    shader->updateData("screenSize", screenSize, true);
    shader->updateData("sunLightColor", sunLightColor.getFlatBuffer(), true);
    shader->updateData("sunLightRange", &sunLightRange, true);
    shader->updateData("sunLightPosition", sunLightPos.getFlatBuffer(), true);
    shader->updateData("sunLightRadius", &sunLightRadius, true);

    //auto resourceBindings  = shader->_resourceIndexes;
    //ID3D12DescriptorHeap* descriptorHeaps[] = {rtPipeline->getDescHeap().Get()};
    //cmdList->SetDescriptorHeaps(1, descriptorHeaps);
    //
    //cmdList->SetComputeRootDescriptorTable(resourceBindings["sampleSets"],
    //                                      _hemisphereSamplesGPUBuffer->gpuDescriptorHandle);

    shader->updateData("seed", &seed, true);
    shader->updateData("numSamplesPerSet", &numSamplesPerSet, true);
    shader->updateData("numSampleSets", &numSampleSets, true);
    shader->updateData("numPixelsPerDimPerSet", &numPixelsPerDimPerSet, true);

    if (EngineManager::getGraphicsLayer() == GraphicsLayer::DXR_1_1_PATHTRACER)
    {
        shader->dispatch(ceilf(screenSize[0] / static_cast<float>(threadGroupWidth)),
                         ceilf(screenSize[1] / static_cast<float>(threadGroupHeight)), 1);
    }
    else if (EngineManager::getGraphicsLayer() == GraphicsLayer::DXR_1_0_PATHTRACER)
    {
        _dxrStateObject->dispatchReflectionRays();
    }

    shader->unbind();

    ZeroMemory(&barrierDesc, sizeof(barrierDesc));
    barrierDesc[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc[0].Transition.pResource =
        _sunLightRays->getResource()->getResource().Get();
    barrierDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrierDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_GENERIC_READ;

    barrierDesc[3] = barrierDesc[2] = barrierDesc[1] = barrierDesc[0];
    barrierDesc[1].Transition.pResource = _reflectionRays->getResource()->getResource().Get();
    barrierDesc[2].Transition.pResource = _occlusionRays->getResource()->getResource().Get();
    
    if (_denoising)
    {
        barrierDesc[3].Transition.pResource = _svgfDenoiser->getOcclusionHistoryBuffer()->getResource()->getResource().Get();

        cmdList->ResourceBarrier(4, barrierDesc);
    }
    else
    {
        cmdList->ResourceBarrier(3, barrierDesc);
    }


    cmdList->EndEvent();

    DXLayer::instance()->setTimeStamp();

    if (_denoising)
    {
        // Denoise pass for just ambient occlusion rays for now
        _svgfDenoiser->denoise(viewEventDistributor,
                                _occlusionRays,
                                _positionPrimaryRays,
                                _normalPrimaryRays);
    }

    cmdList->BeginEvent(0, L"Compositor", sizeof(L"Compositor"));

    // Composite all ray passes into a final render

    shader = _compositorShader;

    shader->bind();
    shader->updateData("reflectionSRV", 0, _reflectionRays, true, false);
    shader->updateData("sunLightSRV", 0, _sunLightRays, true, false);
    shader->updateData("pathTracerUAV", 0, _compositor, true, true);

    shader->updateData("reflectionMode", &_reflectionMode, true);
    shader->updateData("shadowMode", &_shadowMode, true);
    shader->updateData("screenSize", screenSize, true);
    shader->updateData("viewMode", &_viewMode, true);

    shader->dispatch(ceilf(screenSize[0] / static_cast<float>(threadGroupWidth)),
                     ceilf(screenSize[1] / static_cast<float>(threadGroupHeight)), 1);

    shader->unbind();

    ZeroMemory(&barrierDesc, sizeof(barrierDesc));
    barrierDesc[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc[0].Transition.pResource   = _albedoPrimaryRays->getResource()->getResource().Get();
    barrierDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_GENERIC_READ;
    barrierDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    barrierDesc[6] = barrierDesc[5] = barrierDesc[4] = barrierDesc[3] = barrierDesc[2] = barrierDesc[1] = barrierDesc[0];
    barrierDesc[1].Transition.pResource = _positionPrimaryRays->getResource()->getResource().Get();
    barrierDesc[2].Transition.pResource = _normalPrimaryRays->getResource()->getResource().Get();
    barrierDesc[3].Transition.pResource = _reflectionRays->getResource()->getResource().Get();
    barrierDesc[4].Transition.pResource = _occlusionRays->getResource()->getResource().Get();
    barrierDesc[5].Transition.pResource = _sunLightRays->getResource()->getResource().Get();
    
    if (_denoising)
    {
        barrierDesc[6].Transition.pResource = _svgfDenoiser->getOcclusionHistoryBuffer()->getResource()->getResource().Get();
        cmdList->ResourceBarrier(7, barrierDesc);
    }
    else
    {
        cmdList->ResourceBarrier(6, barrierDesc);
    }


    cmdList->EndEvent();

    EngineManager* engMan = EngineManager::instance();
    Bloom*         bloom  = engMan->getBloomShader();
    SSCompute*     add    = engMan->getAddShader();

    add->uavBarrier();

    cmdList->BeginEvent(0, L"Bloom Pass", sizeof(L"Bloom Pass"));

    // Compute bloom from finalized render target
    bloom->compute(_compositor);


    // THE FINAL ADD TO THE COMPOSITION RENDER TARGET CAUSES CORRUPTION
    // IN THE FORM OF OVERLAYING A RANDOM COLLECTION TEXTURE IN THE TOP
    // LEFT CORNER OF THE SCREEN...LOOKS LIKE DOWNSAMPLING OR SOME PASS
    // OF THE BLOOM SHADERS ARE BEING WRITTEN TO THE FINAL COMPOSITION???

    //ZeroMemory(&barrierDesc, sizeof(barrierDesc));
    //barrierDesc[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    //barrierDesc[0].Transition.pResource =
    //    bloom->getTexture()->getResource()->getResource().Get();
    //barrierDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    //barrierDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    //barrierDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    //cmdList->ResourceBarrier(1, barrierDesc);

    //// Adds bloom data back into the composite render target
    //add->compute(bloom->getTexture(), _compositor);

    //ZeroMemory(&barrierDesc, sizeof(barrierDesc));
    //barrierDesc[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    //barrierDesc[0].Transition.pResource =
    //    bloom->getTexture()->getResource()->getResource().Get();
    //barrierDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    //barrierDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    //barrierDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    //cmdList->ResourceBarrier(1, barrierDesc);

    add->uavBarrier();

    cmdList->EndEvent();

    DXLayer::instance()->setTimeStamp();

    _frameIndex++;
}
