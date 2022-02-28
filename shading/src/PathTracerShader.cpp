#include "PathTracerShader.h"
#include "DXLayer.h"
#include "EngineManager.h"
#include "HLSLShader.h"
#include "Logger.h"
#include "ResourceManager.h"
#include "Bloom.h"
#include "SSCompute.h"
#include "TextureBroker.h"
#include "Model.h"
#include <iomanip>

#include "NRD.h"
#include "NRIDescs.hpp"
#include "Extensions/NRIHelper.h"
#include "NRDIntegration.hpp"
#include "NRIDeviceCreation.h"
#include "NRIWrapperD3D12.h"

#include <chrono>

static NrdIntegration _NRD(CMD_LIST_NUM);

struct NriInterface : public nri::CoreInterface,
                      public nri::HelperInterface,
                      public nri::WrapperD3D12Interface
{
};

NriInterface _NRI;
static nri::Device* _nriDevice;

PathTracerShader::PathTracerShader(std::string shaderName)
{
    _denoising = true;
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
    // Normals in R8G8B8 and roughness in A8 channel
    primaryRaysFormats->push_back(DXGI_FORMAT_R8G8B8A8_UNORM);

    _primaryRaysShader = new HLSLShader(
        DXR1_1_SHADERS_LOCATION + "primaryRaysShaderCS", "", primaryRaysFormats);

    _albedoPrimaryRays =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                          IOEventDistributor::screenPixelHeight, TextureFormat::RGBA_UNSIGNED_BYTE, "_albedoPrimaryRays");
    _normalPrimaryRays =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                          IOEventDistributor::screenPixelHeight, TextureFormat::RGBA_UNSIGNED_BYTE, "_normalPrimaryRays");
    _positionPrimaryRays =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                          IOEventDistributor::screenPixelHeight, TextureFormat::RGBA_FLOAT, "_positionPrimaryRays");

    _viewZPrimaryRays = new RenderTexture(IOEventDistributor::screenPixelWidth,
                                          IOEventDistributor::screenPixelHeight,
                                          TextureFormat::R_FLOAT, "_viewZPrimaryRays");

    _occlusionRays =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                          IOEventDistributor::screenPixelHeight, TextureFormat::R16G16_FLOAT, "_occlusionRays");

    _denoisedOcclusionRays =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                          IOEventDistributor::screenPixelHeight, TextureFormat::RGBA_UNSIGNED_BYTE, "_denoisedOcclusionRays");

    _indirectLightRays = new RenderTexture(IOEventDistributor::screenPixelWidth,
                                           IOEventDistributor::screenPixelHeight,
                                           TextureFormat::R16G16B16A16_FLOAT, "_indirectLightRays");

    _indirectLightRaysHistoryBuffer = new RenderTexture(IOEventDistributor::screenPixelWidth,
                                           IOEventDistributor::screenPixelHeight,
                                           TextureFormat::R16G16B16A16_FLOAT, "_indirectLightRaysHistoryBuffer");

    _indirectSpecularLightRays = new RenderTexture(IOEventDistributor::screenPixelWidth,
                                           IOEventDistributor::screenPixelHeight,
                                           TextureFormat::R16G16B16A16_FLOAT, "_indirectSpecularLightRays");

    _indirectSpecularLightRaysHistoryBuffer = new RenderTexture(IOEventDistributor::screenPixelWidth,
                                           IOEventDistributor::screenPixelHeight,
                                           TextureFormat::R16G16B16A16_FLOAT, "_indirectSpecularLightRaysHistoryBuffer");

    _diffusePrimarySurfaceModulation = new RenderTexture(IOEventDistributor::screenPixelWidth,
                                           IOEventDistributor::screenPixelHeight,
                                           TextureFormat::R16G16B16A16_FLOAT, "_diffusePrimarySurfaceModulation");

    _specularPrimarySurfaceModulation = new RenderTexture(IOEventDistributor::screenPixelWidth,
                                           IOEventDistributor::screenPixelHeight,
                                           TextureFormat::R16G16B16A16_FLOAT, "_specularPrimarySurfaceModulation");

    // Sun lighting rays and occlusion

     // Segments the path tracer into separate passes that help improve coherency
    std::vector<DXGI_FORMAT>* sunLightRaysFormats = new std::vector<DXGI_FORMAT>();
    // Albedo in R8G8B8 and maybe transparency in A8 to indicate water or something...
    sunLightRaysFormats->push_back(DXGI_FORMAT_R8G8B8A8_UNORM);

    _sunLightRays =
        new RenderTexture(IOEventDistributor::screenPixelWidth,
                                      IOEventDistributor::screenPixelHeight,
                                      TextureFormat::RGBA_UNSIGNED_BYTE, "_sunlightRays");

    // Reflection rays shading

    std::vector<DXGI_FORMAT>* reflectionRaysFormats = new std::vector<DXGI_FORMAT>();
    // Albedo in R8G8B8 and maybe transparency in A8 to indicate water or something...
    reflectionRaysFormats->push_back(DXGI_FORMAT_R8G8B8A8_UNORM);
    reflectionRaysFormats->push_back(DXGI_FORMAT_R16G16_FLOAT);
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

    ResourceManager* resourceManager = EngineManager::getResourceManager();

    resourceManager->allocateUploadBuffer(DXLayer::instance()->getDevice().Get(), nullptr,
                                        sizeof(float) * 4 * numSamples, &_hemisphereSamplesUpload,
                                        L"HemisphereSamples");

    _hemisphereSamplesGPUBuffer = new D3DBuffer();

    _hemisphereSamplesGPUBuffer->resource = _hemisphereSamplesUpload;
    _hemisphereSamplesGPUBuffer->count    = numSamples;

    UINT descriptorVB = resourceManager->createBufferSRV(_hemisphereSamplesGPUBuffer, numSamples,
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
    D3D12_RESOURCE_BARRIER barrierDesc[9];
    ZeroMemory(&barrierDesc, sizeof(barrierDesc));

    barrierDesc[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrierDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    barrierDesc[8] = barrierDesc[7] = barrierDesc[6] = barrierDesc[5] = barrierDesc[4] = barrierDesc[3] =
        barrierDesc[2] = barrierDesc[1] = barrierDesc[0];

    barrierDesc[0].Transition.pResource = _reflectionRays->getResource()->getResource().Get();
    barrierDesc[1].Transition.pResource = _occlusionRays->getResource()->getResource().Get();
    barrierDesc[2].Transition.pResource = _sunLightRays->getResource()->getResource().Get();
    barrierDesc[3].Transition.pResource = _albedoPrimaryRays->getResource()->getResource().Get();
    barrierDesc[4].Transition.pResource = _positionPrimaryRays->getResource()->getResource().Get();
    barrierDesc[5].Transition.pResource = _normalPrimaryRays->getResource()->getResource().Get();
    barrierDesc[6].Transition.pResource = _compositor->getResource()->getResource().Get();
    barrierDesc[7].Transition.pResource = _denoisedOcclusionRays->getResource()->getResource().Get();
    barrierDesc[8].Transition.pResource = _viewZPrimaryRays->getResource()->getResource().Get();

    computeCmdList->ResourceBarrier(9, barrierDesc);

    //_dxrStateObject = new DXRStateObject(_primaryRaysShader->getRootSignature(),
    //                                     _reflectionRaysShader->getRootSignature());

    nri::DeviceCreationD3D12Desc deviceDesc = {};
    deviceDesc.d3d12Device                  = DXLayer::instance()->getDevice().Get();
    deviceDesc.d3d12PhysicalAdapter         = DXLayer::instance()->getAdapter().Get();
    deviceDesc.d3d12GraphicsQueue           = DXLayer::instance()->getGfxCmdQueue().Get();
    deviceDesc.enableNRIValidation          = false;

    // Wrap the device
    nri::Result result = nri::CreateDeviceFromD3D12Device(deviceDesc, _nriDevice);

    // Get needed functionality
    result = nri::GetInterface(*_nriDevice, NRI_INTERFACE(nri::CoreInterface),
                                (nri::CoreInterface*)&_NRI);
    result = nri::GetInterface(*_nriDevice, NRI_INTERFACE(nri::HelperInterface),
                                (nri::HelperInterface*)&_NRI);

    // Get needed "wrapper" extension, D3D12
    result = nri::GetInterface(*_nriDevice, NRI_INTERFACE(nri::WrapperD3D12Interface),
                                (nri::WrapperD3D12Interface*)&_NRI);

    const nrd::MethodDesc methodDescs[] = {
        /*{nrd::Method::SIGMA_SHADOW, static_cast<uint16_t>(IOEventDistributor::screenPixelWidth),
         static_cast<uint16_t>(IOEventDistributor::screenPixelHeight)},*/
        {nrd::Method::REBLUR_DIFFUSE, static_cast<uint16_t>(IOEventDistributor::screenPixelWidth),
         static_cast<uint16_t>(IOEventDistributor::screenPixelHeight)},
        {nrd::Method::REBLUR_SPECULAR,
         static_cast<uint16_t>(IOEventDistributor::screenPixelWidth),
         static_cast<uint16_t>(IOEventDistributor::screenPixelHeight)}
    };

    nrd::DenoiserCreationDesc denoiserCreationDesc = {};
    denoiserCreationDesc.requestedMethods          = methodDescs;
    denoiserCreationDesc.requestedMethodNum        = _countof(methodDescs);

    bool initializeNrdResult = _NRD.Initialize(*_nriDevice, _NRI, _NRI, denoiserCreationDesc);
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
    ResourceManager* resourceManager = EngineManager::getResourceManager();

    DXLayer::instance()->setTimeStamp();

    resourceManager->updateResources();

    HLSLShader* shader = static_cast<HLSLShader*>(_shader);
    auto        cmdList = DXLayer::instance()->getCmdList();

    UINT threadGroupWidth  = 8.0;
    UINT threadGroupHeight = 8.0;

    DXLayer::instance()->setTimeStamp();

    D3D12_RESOURCE_BARRIER barrierDesc[12];
    ZeroMemory(&barrierDesc, sizeof(barrierDesc));

    barrierDesc[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc[0].Transition.pResource   = _indirectLightRays->getResource()->getResource().Get();
    barrierDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrierDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    barrierDesc[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc[1].Transition.pResource = _indirectSpecularLightRays->getResource()->getResource().Get();
    barrierDesc[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    barrierDesc[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    cmdList->ResourceBarrier(2, barrierDesc);

    // Clear occlusion, point light, sun light and reflection UAVs
    float zeroValues[] = {0.0, 0.0, 0.0, 0.0};
    float oneValues[] = {1.0, 0.0};
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

    cmdList->ClearUnorderedAccessViewFloat(
        _indirectSpecularLightRays->getUAVGPUHandle(),
        _indirectSpecularLightRays->getUAVCPUHandle(),
        _indirectSpecularLightRays->getResource()->getResource().Get(), zeroValues, 0, nullptr);

    cmdList->ClearUnorderedAccessViewFloat(
         _indirectLightRays->getUAVGPUHandle(),
         _indirectLightRays->getUAVCPUHandle(),
         _indirectLightRays->getResource()->getResource().Get(), zeroValues, 0, nullptr);

    cmdList->ClearUnorderedAccessViewFloat(
         _compositor->getUAVGPUHandle(),
         _compositor->getUAVCPUHandle(),
         _compositor->getResource()->getResource().Get(), zeroValues, 0, nullptr);

    // Get skybox texture
    TextureBroker* textureManager = TextureBroker::instance();

    auto skyBoxTexture = textureManager->getTexture(SKYBOX_LOCATION);
    int  texturesPerMaterial = TexturesPerMaterial;

    auto cameraView          = viewEventDistributor->getView();
    auto prevCameraView    = viewEventDistributor->getPrevCameraView();
    auto inverseCameraView = cameraView.inverse();
    auto cameraProj        = viewEventDistributor->getProjection();
    auto inverseCameraProj = cameraProj.inverse();

    float screenSize[] = {static_cast<float>(IOEventDistributor::screenPixelWidth),
                          static_cast<float>(IOEventDistributor::screenPixelHeight)};

    auto deltaCameraView      = viewEventDistributor->getPrevCameraView() - cameraView;
    Vector4 deltaCameraVector = deltaCameraView * Vector4(1.0, 1.0, 1.0);
    bool   isCameraMoving     = deltaCameraVector.getMagnitude() >= 0.000001 ? true : false;

    // Process point lights
    PointLightList lightList;
    EngineManager::instance()->processLights(lights, viewEventDistributor, lightList,
                                             RandomInsertAndRemoveEntities);

    auto end = std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
                   .count();
    // auto milliSecondsPassed = end - begin;

    const long long timing = 500000;
    end                    = end % timing;

    std::uniform_int_distribution<UINT> seedDistribution(0, UINT_MAX);
    UINT                                seed                  = seedDistribution(_generatorURNG);
    UINT                                numSamplesPerSet      = 64;
    UINT                                numSampleSets         = 83;
    UINT                                numPixelsPerDimPerSet = 8;

 
    cmdList->BeginEvent(0, L"Reflection Rays", sizeof(L"Reflection Rays"));

    // Reflection rays

    shader = _reflectionRaysShader;

    shader->bind();

    shader->updateData("indirectLightRaysUAV", 0, _indirectLightRays, true, true);
    shader->updateData("indirectSpecularLightRaysUAV", 0, _indirectSpecularLightRays, true, true);
    shader->updateData("diffusePrimarySurfaceModulation", 0, _diffusePrimarySurfaceModulation, true, true);
    shader->updateData("specularPrimarySurfaceModulation", 0, _specularPrimarySurfaceModulation, true, true);

    shader->updateData("albedoUAV", 0, _albedoPrimaryRays, true, true);
    shader->updateData("positionUAV", 0, _positionPrimaryRays, true, true);
    shader->updateData("normalUAV", 0, _normalPrimaryRays, true, true);
    shader->updateData("viewZUAV", 0, _viewZPrimaryRays, true, true);

    shader->updateData("skyboxTexture", 0, skyBoxTexture, true);

    shader->updateRTAS("rtAS", resourceManager->getRTASDescHeap(), resourceManager->getRTASGPUVA(), true);

    shader->updateData("inverseView", inverseCameraView.getFlatBuffer(), true);
    shader->updateData("viewTransform", cameraView.getFlatBuffer(), true);
    int rpp = resourceManager->getRaysPerPixel();
    shader->updateData("maxBounces", &rpp, true);
    int renderMode = resourceManager->getRenderMode();
    shader->updateData("renderMode", &renderMode, true);
    int rayBounceIndex = resourceManager->getRayBounceIndex() - 1;
    shader->updateData("rayBounceIndex", &rayBounceIndex, true);

    int diffuseOrSpecular  = resourceManager->getDiffuseOrSpecular();
    int reflectionOrRefraction = resourceManager->getReflectionOrRefraction();
    int enableEmissives        = resourceManager->getEnableEmissives();
    int enableIBL               = resourceManager->getEnableIBL();

    shader->updateData("diffuseOrSpecular", &diffuseOrSpecular, true);
    shader->updateData("reflectionOrRefraction", &reflectionOrRefraction, true);
    shader->updateData("enableEmissives", &enableEmissives, true);
    shader->updateData("enableIBL", &enableIBL, true);

    resourceManager->updateTextureUnbounded(shader->_resourceIndexes["diffuseTexture"], 0, nullptr, 0, true);
    resourceManager->updateStructuredAttributeBufferUnbounded(shader->_resourceIndexes["vertexBuffer"], nullptr, true);
    resourceManager->updateStructuredIndexBufferUnbounded(shader->_resourceIndexes["indexBuffer"], nullptr, true);

    resourceManager->updateAndBindMaterialBuffer(shader->_resourceIndexes, true);
    resourceManager->updateAndBindAttributeBuffer(shader->_resourceIndexes, true);
    resourceManager->updateAndBindNormalMatrixBuffer(shader->_resourceIndexes, true);
    resourceManager->updateAndBindUniformMaterialBuffer(shader->_resourceIndexes, true);

    shader->updateData("numLights", &lightList.lightCount, true);
    shader->updateData("lightColors", lightList.lightColorsArray, true);
    shader->updateData("lightRanges", lightList.lightRangesArray, true);
    shader->updateData("lightPositions", lightList.lightPosArray, true);
    shader->updateData("isPointLight", lightList.isPointLightArray, true);

    shader->updateData("texturesPerMaterial", &texturesPerMaterial, true);

    shader->updateData("screenSize", screenSize, true);

    shader->updateData("seed", &seed, true);
    shader->updateData("numSamplesPerSet", &numSamplesPerSet, true);
    shader->updateData("numSampleSets", &numSampleSets, true);
    shader->updateData("numPixelsPerDimPerSet", &numPixelsPerDimPerSet, true);

    shader->updateData("frameNumber", &_frameIndex, true);

    int resetHistoryBuffer = isCameraMoving ? 1 : 0;
    shader->updateData("resetHistoryBuffer", &resetHistoryBuffer, true);

    auto resourceBindings  = shader->_resourceIndexes;
    ID3D12DescriptorHeap* descriptorHeaps[] = {resourceManager->getDescHeap().Get()};
    cmdList->SetDescriptorHeaps(1, descriptorHeaps);
    
    cmdList->SetComputeRootDescriptorTable(resourceBindings["sampleSets"],
                                           _hemisphereSamplesGPUBuffer->gpuDescriptorHandle);

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

    barrierDesc[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc[0].Transition.pResource   = _albedoPrimaryRays->getResource()->getResource().Get();
    barrierDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrierDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_GENERIC_READ;

    barrierDesc[3] = barrierDesc[2] = barrierDesc[1] = barrierDesc[0];
    barrierDesc[1].Transition.pResource = _positionPrimaryRays->getResource()->getResource().Get();
    barrierDesc[2].Transition.pResource = _normalPrimaryRays->getResource()->getResource().Get();
    barrierDesc[3].Transition.pResource = _viewZPrimaryRays->getResource()->getResource().Get();

    cmdList->ResourceBarrier(4, barrierDesc);


    if (_denoising)
    {
        auto motionVectors = _svgfDenoiser->getMotionVectors();

        _svgfDenoiser->computeMotionVectors(viewEventDistributor, _positionPrimaryRays);

        D3D12_RESOURCE_BARRIER barrierDesc[1];
        ZeroMemory(&barrierDesc, sizeof(barrierDesc));

        barrierDesc[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierDesc[0].Transition.pResource   = motionVectors->getResource()->getResource().Get();
        barrierDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrierDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        cmdList->ResourceBarrier(1, barrierDesc);
    }


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
        ZeroMemory(&barrierDesc, sizeof(barrierDesc));

        barrierDesc[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierDesc[0].Transition.pResource   = _indirectLightRays->getResource()->getResource().Get();
        barrierDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrierDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        barrierDesc[1].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierDesc[1].Transition.pResource   = _indirectSpecularLightRays->getResource()->getResource().Get();
        barrierDesc[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierDesc[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrierDesc[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        barrierDesc[2].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierDesc[2].Transition.pResource = _indirectLightRaysHistoryBuffer->getResource()->getResource().Get();
        barrierDesc[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierDesc[2].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrierDesc[2].Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        
        barrierDesc[3].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierDesc[3].Transition.pResource = _indirectSpecularLightRaysHistoryBuffer->getResource()->getResource().Get();
        barrierDesc[3].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierDesc[3].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrierDesc[3].Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

        barrierDesc[4].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierDesc[4].Transition.pResource = _diffusePrimarySurfaceModulation->getResource()->getResource().Get();
        barrierDesc[4].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierDesc[4].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrierDesc[4].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        barrierDesc[5].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierDesc[5].Transition.pResource = _specularPrimarySurfaceModulation->getResource()->getResource().Get();
        barrierDesc[5].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierDesc[5].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrierDesc[5].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        cmdList->ResourceBarrier(6, barrierDesc);

        // Wrap the command buffer
        nri::CommandBufferD3D12Desc cmdDesc = {};
        cmdDesc.d3d12CommandList            = (ID3D12GraphicsCommandList*)cmdList.Get();
        cmdDesc.d3d12CommandAllocator       = nullptr; // Not needed for NRD Integration layer

        nri::CommandBuffer* cmdBuffer = nullptr;
        _NRI.CreateCommandBufferD3D12(*_nriDevice, cmdDesc, cmdBuffer);

        // Wrap required textures
        constexpr uint32_t                N              = 7;
        nri::TextureTransitionBarrierDesc entryDescs[N]  = {};
        nri::Format                       entryFormat[N] = {};

        // You need to specify the current state of the resource here, after denoising NRD can
        // modify this state. Application must continue state tracking from this point. Useful
        // information:
        //    SRV = nri::AccessBits::SHADER_RESOURCE, nri::TextureLayout::SHADER_RESOURCE
        //    UAV = nri::AccessBits::SHADER_RESOURCE_STORAGE, nri::TextureLayout::GENERAL

        nri::TextureD3D12Desc textureDesc = {};
        textureDesc.d3d12Resource = _normalPrimaryRays->getResource()->getResource().Get();
        nri::Texture* texture     = (nri::Texture*)entryDescs[0].texture;
        _NRI.CreateTextureD3D12(*_nriDevice, textureDesc, texture);
        
        entryDescs[0].texture = texture;
        entryDescs[0].nextAccess = nri::AccessBits::SHADER_RESOURCE;
        entryDescs[0].nextLayout = nri::TextureLayout::SHADER_RESOURCE;

        textureDesc               = {};
        textureDesc.d3d12Resource = _svgfDenoiser->getMotionVectors()->getResource()->getResource().Get();
        texture                   = (nri::Texture*)entryDescs[1].texture;
        _NRI.CreateTextureD3D12(*_nriDevice, textureDesc, texture);

        entryDescs[1].texture    = texture;
        entryDescs[1].nextAccess = nri::AccessBits::SHADER_RESOURCE;
        entryDescs[1].nextLayout = nri::TextureLayout::SHADER_RESOURCE;

        textureDesc               = {};
        textureDesc.d3d12Resource = _indirectLightRays->getResource()->getResource().Get();
        texture                   = (nri::Texture*)entryDescs[2].texture;
        _NRI.CreateTextureD3D12(*_nriDevice, textureDesc, texture);

        entryDescs[2].texture    = texture;
        entryDescs[2].nextAccess = nri::AccessBits::SHADER_RESOURCE;
        entryDescs[2].nextLayout = nri::TextureLayout::SHADER_RESOURCE;

        textureDesc               = {};
        textureDesc.d3d12Resource = _viewZPrimaryRays->getResource()->getResource().Get();
        texture                   = (nri::Texture*)entryDescs[3].texture;
        _NRI.CreateTextureD3D12(*_nriDevice, textureDesc, texture);

        entryDescs[3].texture    = texture;
        entryDescs[3].nextAccess = nri::AccessBits::SHADER_RESOURCE;
        entryDescs[3].nextLayout = nri::TextureLayout::SHADER_RESOURCE;
        
        textureDesc               = {};
        textureDesc.d3d12Resource = _indirectLightRaysHistoryBuffer->getResource()->getResource().Get();
        texture                   = (nri::Texture*)entryDescs[4].texture;
        _NRI.CreateTextureD3D12(*_nriDevice, textureDesc, texture);

        entryDescs[4].texture    = texture;
        entryDescs[4].nextAccess = nri::AccessBits::SHADER_RESOURCE_STORAGE;
        entryDescs[4].nextLayout = nri::TextureLayout::GENERAL;

        textureDesc               = {};
        textureDesc.d3d12Resource = _indirectSpecularLightRays->getResource()->getResource().Get();
        texture                   = (nri::Texture*)entryDescs[5].texture;
        _NRI.CreateTextureD3D12(*_nriDevice, textureDesc, texture);

        entryDescs[5].texture    = texture;
        entryDescs[5].nextAccess = nri::AccessBits::SHADER_RESOURCE;
        entryDescs[5].nextLayout = nri::TextureLayout::SHADER_RESOURCE;

        textureDesc               = {};
        textureDesc.d3d12Resource = _indirectSpecularLightRaysHistoryBuffer->getResource()->getResource().Get();
        texture                   = (nri::Texture*)entryDescs[6].texture;
        _NRI.CreateTextureD3D12(*_nriDevice, textureDesc, texture);

        entryDescs[6].texture    = texture;
        entryDescs[6].nextAccess = nri::AccessBits::SHADER_RESOURCE_STORAGE;
        entryDescs[6].nextLayout = nri::TextureLayout::GENERAL;

        // Populate common settings
        //  - for the first time use defaults
        //  - currently NRD supports only the following view space: X - right, Y - top, Z - forward
        //  or backward
        nrd::CommonSettings commonSettings = {};
        commonSettings.frameIndex = _frameIndex;
        commonSettings.isMotionVectorInWorldSpace = true;
        //commonSettings.splitScreen                = 1;
        //commonSettings.debug                      = 1;

        uint32_t matrixSize = 16 * sizeof(float);
        auto     mat        = cameraView.transpose();
        memcpy(commonSettings.worldToViewMatrix, &mat, matrixSize);
        auto prevMat = prevCameraView.transpose();
        memcpy(commonSettings.worldToViewMatrixPrev, &prevMat, matrixSize);

        auto projMat = cameraProj.transpose();
        memcpy(commonSettings.viewToClipMatrix, &projMat, matrixSize);
        memcpy(commonSettings.viewToClipMatrixPrev, &projMat, matrixSize);


        ////=============================================================================================================================
        //// INPUTS
        ////=============================================================================================================================

        //// 3D world space motion (RGBA16f+) or 2D screen space motion (RG16f+), MVs must be
        //// non-jittered, MV = previous - current
        //IN_MV,

        //// See "NRD.hlsl/NRD_FrontEnd_UnpackNormalAndRoughness" (RGBA8+ or R10G10B10A2+
        //// depending on encoding)
        //IN_NORMAL_ROUGHNESS,

        //// Linear view depth for primary rays (R16f+)
        //IN_VIEWZ,

        //// Data must be packed using "NRD.hlsl/XXX_PackRadianceAndHitDist" (RGBA16f+)
        //IN_DIFF_RADIANCE_HITDIST, IN_SPEC_RADIANCE_HITDIST,

        //// Ambient (AO) and specular (SO) occlusion (R8+)
        //IN_DIFF_HITDIST, IN_SPEC_HITDIST,

        //// (Optional) Data must be packed using "NRD.hlsl/NRD_PackRayDirectionAndPdf" (RGBA8+)
        //IN_DIFF_DIRECTION_PDF, IN_SPEC_DIRECTION_PDF,

        //// (Optional) User-provided history confidence in range 0-1, i.e. antilag (R8+)
        //IN_DIFF_CONFIDENCE, IN_SPEC_CONFIDENCE,

        //// Data must be packed using "NRD.hlsl/XXX_PackShadow (3 args)" (RG16f+). INF pixels
        //// must be cleared with NRD_INF_SHADOW macro
        //IN_SHADOWDATA,

        //// Data must be packed using "NRD.hlsl/XXX_PackShadow (4 args)" (RGBA8+)
        //IN_SHADOW_TRANSLUCENCY,

        ////=============================================================================================================================
        //// OUTPUTS
        ////=============================================================================================================================

        //// IMPORTANT: These textures can potentially be used as history buffers

        //// SIGMA_SHADOW_TRANSLUCENCY - .x - shadow, .yzw - translucency (RGBA8+)
        //// SIGMA_SHADOW - .x - shadow (R8+)
        //// Data must be unpacked using "NRD.hlsl/XXX_UnpackShadow"
        //OUT_SHADOW_TRANSLUCENCY,

        //// .xyz - radiance, .w - normalized hit distance (in case of REBLUR) or signal variance
        //// (in case of ReLAX) (RGBA16f+)
        //OUT_DIFF_RADIANCE_HITDIST, OUT_SPEC_RADIANCE_HITDIST,

        //// .x - normalized hit distance (R8+)
        //OUT_DIFF_HITDIST, OUT_SPEC_HITDIST,

        ////=============================================================================================================================
        //// POOLS
        ////=============================================================================================================================

        //// Can be reused after denoising
        //TRANSIENT_POOL,

        //// Dedicated to NRD, can't be reused
        //PERMANENT_POOL,

        //MAX_NUM,


        // Fill up the user pool
        NrdUserPool userPool = {{
            // Fill the required inputs and outputs in appropriate slots using entryDescs &
            // entryFormat, applying remapping if necessary. Unused slots can be {nullptr,
            // nri::Format::UNKNOWN}

            // IN_MV
            {&entryDescs[1], nri::Format::RGBA16_SFLOAT},

            // IN_NORMAL_ROUGHNESS
            {&entryDescs[0], nri::Format::RGBA8_UNORM},

            // IN_VIEWZ
            {&entryDescs[3], nri::Format::R32_SFLOAT},

            // IN_DIFF_HIT,
            {&entryDescs[2], nri::Format::RGBA16_SFLOAT},

            // IN_SPEC_HIT,
            {&entryDescs[5], nri::Format::RGBA16_SFLOAT},

            //// Ambient (AO) occlusion (R8+)
            {nullptr, nri::Format::UNKNOWN},
            //
            //// specular (SO) occlusion (R8+)
            {nullptr, nri::Format::UNKNOWN},

            // IN_DIFF_DIRECTION_PDF,
            {nullptr, nri::Format::UNKNOWN},

            // IN_SPEC_DIRECTION_PDF,
            {nullptr, nri::Format::UNKNOWN},

            //// (Optional) User-provided history confidence in range 0-1, i.e. antilag (R8+)
            //// IN_DIFF_CONFIDENCE
            {nullptr, nri::Format::UNKNOWN},
            //
            ////IN_SPEC_CONFIDENCE,
            {nullptr, nri::Format::UNKNOWN},

            // IN_SHADOW
            {nullptr, nri::Format::UNKNOWN},

            // IN_TRANSLUCENCY
            {nullptr, nri::Format::UNKNOWN},

            // OUT_SHADOW | OUT_TRANSMITTANCE
            {nullptr, nri::Format::UNKNOWN},
            // when using Translusent Shadows, OUT_SHADOWS texture is not required. and vice
            // versa

            // OUT_DIFF_HIT
            {&entryDescs[4], nri::Format::RGBA16_SFLOAT},

            // OUT_SPEC_HIT
            {&entryDescs[6], nri::Format::RGBA16_SFLOAT},
        }};

        _NRD.Denoise(0, *cmdBuffer, commonSettings, userPool);

        //for (uint32_t i = 0; i < N; i++)
        //{
        //    nri::Texture* texture = (nri::Texture*)entryDescs[i].texture;
        //    _NRI.DestroyTexture(*texture);
        //}
        
        _NRI.DestroyCommandBuffer(*cmdBuffer);

        //====================================================================================================================
        // STEP 7 - DESTROY
        //====================================================================================================================

        //NRD.Destroy();
    }

    if (resourceManager->getEnableBloom())
    {
        DXLayer::instance()->setTimeStamp();

        EngineManager* engMan = EngineManager::instance();
        Bloom*         bloom  = engMan->getBloomShader();
        SSCompute*     add    = engMan->getAddShader();

        add->uavBarrier();

        cmdList->BeginEvent(0, L"Bloom Pass", sizeof(L"Bloom Pass"));

        // Compute bloom from finalized render target
        bloom->compute(_indirectSpecularLightRaysHistoryBuffer);

        ZeroMemory(&barrierDesc, sizeof(barrierDesc));
        barrierDesc[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierDesc[0].Transition.pResource =
            bloom->getTexture()->getResource()->getResource().Get();
        barrierDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barrierDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        cmdList->ResourceBarrier(1, barrierDesc);

        // Adds bloom data back into the composite render target
        add->compute(bloom->getTexture(), _compositor);

        ZeroMemory(&barrierDesc, sizeof(barrierDesc));
        barrierDesc[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierDesc[0].Transition.pResource =
            bloom->getTexture()->getResource()->getResource().Get();
        barrierDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrierDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        cmdList->ResourceBarrier(1, barrierDesc);

        add->uavBarrier();

        cmdList->EndEvent();
    }

    cmdList->BeginEvent(0, L"Compositor", sizeof(L"Compositor"));

    // Composite all ray passes into a final render

    shader = _compositorShader;

    shader->bind();
    if (renderMode == 3)
    {
        shader->updateData("indirectLightRaysHistoryBufferSRV", 0, _indirectLightRays, true, false);
    }
    else
    {
        shader->updateData("indirectLightRaysHistoryBufferSRV", 0, _indirectLightRaysHistoryBuffer, true, false);
    }

    if (renderMode == 4)
    {
        shader->updateData("indirectSpecularLightRaysHistoryBufferSRV", 0, _indirectSpecularLightRays, true, false);
    }
    else
    {
        shader->updateData("indirectSpecularLightRaysHistoryBufferSRV", 0, _indirectSpecularLightRaysHistoryBuffer, true, false);
    }
    shader->updateData("diffusePrimarySurfaceModulation", 0, _diffusePrimarySurfaceModulation, true, false);
    //shader->updateData("specularPrimarySurfaceModulation", 0, _specularPrimarySurfaceModulation, true, false);

    shader->updateData("pathTracerUAV", 0, _compositor, true, true);

    shader->updateData("reflectionMode", &_reflectionMode, true);
    shader->updateData("shadowMode", &_shadowMode, true);
    shader->updateData("screenSize", screenSize, true);

    shader->dispatch(ceilf(screenSize[0] / static_cast<float>(threadGroupWidth)),
                     ceilf(screenSize[1] / static_cast<float>(threadGroupHeight)), 1);

    shader->unbind();

    ZeroMemory(&barrierDesc, sizeof(barrierDesc));
    barrierDesc[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc[0].Transition.pResource   = _albedoPrimaryRays->getResource()->getResource().Get();
    barrierDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_GENERIC_READ;
    barrierDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    barrierDesc[7] = barrierDesc[6] = barrierDesc[5] = barrierDesc[4] = barrierDesc[3] = barrierDesc[2] = barrierDesc[1] = barrierDesc[0];
    barrierDesc[1].Transition.pResource = _positionPrimaryRays->getResource()->getResource().Get();
    barrierDesc[2].Transition.pResource = _normalPrimaryRays->getResource()->getResource().Get();
    barrierDesc[3].Transition.pResource = _reflectionRays->getResource()->getResource().Get();
    barrierDesc[4].Transition.pResource = _occlusionRays->getResource()->getResource().Get();
    barrierDesc[5].Transition.pResource = _sunLightRays->getResource()->getResource().Get();
    barrierDesc[7].Transition.pResource = _viewZPrimaryRays->getResource()->getResource().Get();
    
    if (_denoising)
    {
        barrierDesc[6].Transition.pResource = _svgfDenoiser->getOcclusionHistoryBuffer()->getResource()->getResource().Get();
        cmdList->ResourceBarrier(8, barrierDesc);

        auto motionVectors = _svgfDenoiser->getMotionVectors();
        ZeroMemory(&barrierDesc, sizeof(barrierDesc));

        barrierDesc[0].Type                   = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrierDesc[0].Transition.pResource   = motionVectors->getResource()->getResource().Get();
        barrierDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        barrierDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

        cmdList->ResourceBarrier(1, barrierDesc);
    }
    else
    {
        cmdList->ResourceBarrier(6, barrierDesc);
    }

    cmdList->EndEvent();

    ZeroMemory(&barrierDesc, sizeof(barrierDesc));

    barrierDesc[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc[0].Transition.pResource = _diffusePrimarySurfaceModulation->getResource()->getResource().Get();
    barrierDesc[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrierDesc[0].Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    barrierDesc[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrierDesc[1].Transition.pResource = _specularPrimarySurfaceModulation->getResource()->getResource().Get();
    barrierDesc[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrierDesc[1].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrierDesc[1].Transition.StateAfter  = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

    cmdList->ResourceBarrier(2, barrierDesc);

    DXLayer::instance()->setTimeStamp();

    _frameIndex++;
}
