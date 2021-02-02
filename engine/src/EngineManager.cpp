#include "EngineManager.h"
#include "AudioManager.h"
#include "Bloom.h"
#include "DXLayer.h"
#include "EngineScene.h"
#include "Entity.h"
#include "HLSLShader.h"
#include "IOEventDistributor.h"
#include "IOEvents.h"
#include "MasterClock.h"
#include "ModelBroker.h"
#include "PathTracerShader.h"
#include "Randomization.h"
#include "SSCompute.h"
#include "SceneBuilder.h"
#include "ShaderBroker.h"
#include "ViewEventDistributor.h"
#include "StaticShader.h"
#include "MRTFrameBuffer.h"
#include "DeferredShader.h"
#include <chrono>

RayTracingPipelineShader* EngineManager::_rayTracingPipeline = nullptr;
GraphicsLayer             EngineManager::_graphicsLayer;
EngineManager*            EngineManager::_engineManager = nullptr;

EngineManager::EngineManager(int* argc, char** argv, HINSTANCE hInstance, int nCmdShow)
{
    // seed the random number generator
    Randomization::seed();

    // initialize engine manager pointer so it can be used a singleton
    _engineManager = this;
    _graphicsLayer = GraphicsLayer::DX12;
    _generatorMode = false;
    _shadowEntity  = nullptr;

    DXLayer::initialize(hInstance, nCmdShow);

    _inputLayer         = new IOEventDistributor(argc, argv, hInstance, nCmdShow, "evil-suzanne");
    _rayTracingPipeline = new RayTracingPipelineShader();

    // Load and compile all shaders for the shader broker
    auto thread = new std::thread(&ShaderBroker::compileShaders, ShaderBroker::instance());
    thread->detach();

    _audioManager = new AudioManager();

    _viewManager = new ViewEventDistributor(argc,
                                            argv,
                                            IOEventDistributor::screenPixelWidth,
                                            IOEventDistributor::screenPixelHeight);

    // Initializes projection matrix and broadcasts upate to all listeners
    _viewManager->setProjection(IOEventDistributor::screenPixelWidth,
                                IOEventDistributor::screenPixelHeight, 0.1f, 10000.0f);

    // Wait for shaders to compile.
    // There is a texture mips computation dependency on loading textures which requires full compilation.
    auto shaders = ShaderBroker::instance();
    while (shaders->isFinishedCompiling() == false)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(0));
    }

    // ModelBroker is now initialized within scene builder
    _scene = SceneBuilder::parse("../config/scene.json", _viewManager, _audioManager);
    
    _rayTracingPipeline->init(DXLayer::instance()->getDevice());

    if (EngineManager::getGraphicsLayer() != GraphicsLayer::DX12)
    {
        _pathTracerShader = static_cast<PathTracerShader*>(ShaderBroker::instance()->getShader("pathTracerShader"));
    }

    _bloom            = new Bloom();
    _add              = new SSCompute("add",
                                       IOEventDistributor::screenPixelWidth,
                                       IOEventDistributor::screenPixelHeight,
                                       TextureFormat::RGBA_UNSIGNED_BYTE);

    _singleDrawRaster = static_cast<StaticShader*>(ShaderBroker::instance()->getShader("staticShader"));

    _renderTexture = new RenderTexture(IOEventDistributor::screenPixelWidth, IOEventDistributor::screenPixelHeight, TextureFormat::RGBA_UNSIGNED_BYTE);
    _depthTexture = new RenderTexture(IOEventDistributor::screenPixelWidth, IOEventDistributor::screenPixelHeight, TextureFormat::DEPTH32_FLOAT);

    _gBuffers = new MRTFrameBuffer();

    // Setup pre and post draw callback events received when a draw call is issued
    IOEvents::setPreDrawCallback(std::bind(&EngineManager::_preDraw, this));
    IOEvents::setPostDrawCallback(std::bind(&EngineManager::_postDraw, this));

    _deferredShader = static_cast<DeferredShader*>(ShaderBroker::instance()->getShader("deferredShader"));

    MasterClock::instance()->run();
    _viewManager->triggerEvents();
    _viewManager->setEntityList(_scene->entityList);

    MasterClock::instance()->start();
    DXLayer::instance()->fenceCommandList();
    _inputLayer->run();
}

EngineManager::~EngineManager()
{
    delete _viewManager;
}

Entity* EngineManager::addEntity(SceneEntity sceneEntity)
{
    _entityListLock.lock();
    auto& entityList                        = _scene->entityList;
    _scene->sceneEntities[sceneEntity.name] = sceneEntity;
    entityList.push_back(new Entity(sceneEntity, _viewManager));
    _entityListLock.unlock();

    return entityList.back();
}

std::optional<SceneEntity> EngineManager::getSceneEntity(const std::string& entityName)
{
    if (_scene->sceneEntities.find(entityName) != _scene->sceneEntities.end())
    {
        return _scene->sceneEntities[entityName];
    }

    return std::optional<SceneEntity>();
}

Light* EngineManager::addLight(const SceneLight& sceneLight)
{
    auto& lightList                      = _scene->lightList;
    _scene->sceneLights[sceneLight.name] = sceneLight;
    lightList.push_back(new Light(sceneLight));

    return lightList.back();
}


std::optional<SceneLight> EngineManager::getSceneLight(const std::string& lightName)
{
    if (_scene->sceneLights.find(lightName) != _scene->sceneLights.end())
    {
        return _scene->sceneLights[lightName];
    }
    return std::optional<SceneLight>();
}

void EngineManager::processLights(std::vector<Light*>&  lights,
                                  ViewEventDistributor* viewEventDistributor,
                                  PointLightList&       pointLightList,
                                  bool                  addLights)
{
    // Use map to sort the lights based on distance from the viewer
    std::map<float, int> lightsSorted;
    // Get point light positions
    unsigned int pointLights = 0;

    Vector4 cameraPos  = viewEventDistributor->getCameraPos();
    cameraPos.getFlatBuffer()[2] = -cameraPos.getFlatBuffer()[2];

    static unsigned int previousTime = 0;
    static constexpr unsigned int lightGenInternalMs = 250;

    auto milliSeconds = MasterClock::instance()->getGameTime();
    if (((milliSeconds - previousTime) > lightGenInternalMs) && addLights)
    {
        // random floats between -1.0 - 1.0
        // Will be used to obtain a seed for the random number engine
        std::random_device rd;
        // Standard mersenne_twister_engine seeded with rd()
        std::mt19937                     generator(rd());
        std::uniform_real_distribution<> randomFloats(-1.0, 1.0);

        float lightIntensityRange = 10.0f;
        //float randomLightIntensity = lightIntensityRange;
        float randomLightIntensity = (((randomFloats(generator) + 1.0) / 2.0) * lightIntensityRange) + 300000.0;

        Vector4 randomColor(static_cast<int>(((randomFloats(generator) + 1.0) / 2.0) * 2.0),
                            static_cast<int>(((randomFloats(generator) + 1.0) / 2.0) * 2.0),
                            static_cast<int>(((randomFloats(generator) + 1.0) / 2.0) * 2.0));

        //Vector4 randomColor(1.0, 1.0, 1.0);
        //Vector4 randomColor(64.0 / 255.0, 156.0 / 255.0, 255.0 / 255.0);

        SceneLight light;
        light.name      = "light trail" + std::to_string(lights.size());
        light.lightType = LightType::POINT;
        light.rotation  = Vector4(0.0, 0.0, 0.0, 1.0);
        light.scale     = Vector4(randomLightIntensity, randomLightIntensity, randomLightIntensity);
        light.color     = randomColor;
        light.position  = -cameraPos;
        light.lockedIdx = -1;
        EngineManager::instance()->addLight(light);

        previousTime = MasterClock::instance()->getGameTime();
    }

    int pointLightOffset = 0;
    for (auto& light : lights)
    {
        if (light->getType() == LightType::POINT || light->getType() == LightType::SHADOWED_POINT)
        {
            Vector4 pointLightPos     = light->getPosition();
            Vector4 pointLightVector  = cameraPos + pointLightPos;
            float   distanceFromLight = pointLightVector.getMagnitude();

            lightsSorted.insert(std::pair<float, int>(distanceFromLight, pointLightOffset));
            pointLights++;
        }
        pointLightOffset++;
    }

    int       lightPosIndex    = 0;
    int       lightColorIndex  = 0;
    int       lightRangeIndex  = 0;
    int       totalLights      = 0;
    for (auto& lightIndex : lightsSorted)
    {
        auto light = lights[lightIndex.second];
        // If point light then add to uniforms
        if (light->getType() == LightType::POINT || light->getType() == LightType::SHADOWED_POINT)
        {
            // Point lights need to remain stationary so move lights with camera space changes
            auto   pos       = light->getPosition();
            float* posBuff   = pos.getFlatBuffer();
            float* colorBuff = light->getColor().getFlatBuffer();
            for (int i = 0; i < 4; i++)
            {
                pointLightList.lightPosArray[lightPosIndex++] = posBuff[i];
                pointLightList.lightColorsArray[lightColorIndex++] = colorBuff[i];
            }
            pointLightList.lightRangesArray[lightRangeIndex++] = light->getScale().getFlatBuffer()[0];
            totalLights++;
        }
    }
    pointLightList.lightCount = pointLights;
}


// Only initializes the static pointer once
// All function parameters are optional so if instance() is called then assume engine manager is
// instantiated
EngineManager* EngineManager::instance(int* argc, char** argv, HINSTANCE hInstance, int nCmdShow)
{
    if (_engineManager == nullptr && argc != nullptr && argv != nullptr)
    {
        _engineManager = new EngineManager(argc, argv, hInstance, nCmdShow);
    }
    return _engineManager;
}

GraphicsLayer             EngineManager::getGraphicsLayer() { return _graphicsLayer;      }
std::vector<Entity*>*     EngineManager::getEntityList()    { return &_scene->entityList; }
RayTracingPipelineShader* EngineManager::getRTPipeline()    { return _rayTracingPipeline; }

void EngineManager::_preDraw()
{
    // Init command lists
    if (_graphicsLayer >= GraphicsLayer::DX12)
    {
        DXLayer::instance()->initCmdLists();
    }

}
void EngineManager::_postDraw()
{
    auto& lightList  = _scene->lightList;

    RenderTexture* finalRender = nullptr;

    if (_graphicsLayer == GraphicsLayer::DXR_1_1_PATHTRACER ||
        _graphicsLayer == GraphicsLayer::DXR_1_0_PATHTRACER)
    {
        _pathTracerShader->runShader(lightList, _viewManager);

        finalRender = _pathTracerShader->getCompositedFrame();
    }
    else
    {
        HLSLShader::setOM(_gBuffers->getTextures(), IOEventDistributor::screenPixelWidth, IOEventDistributor::screenPixelHeight);

        RayTracingPipelineShader* rtPipeline = EngineManager::getRTPipeline();
        rtPipeline->buildAccelerationStructures();

        _singleDrawRaster->startEntity();

        for (auto entity : _scene->entityList)
        {
            _singleDrawRaster->runShader(entity);
        }

        HLSLShader::releaseOM(_gBuffers->getTextures());

        std::vector<RenderTexture> rts;
        rts.push_back(*_renderTexture);
        rts.push_back(*_depthTexture);
        HLSLShader::setOM(rts, IOEventDistributor::screenPixelWidth, IOEventDistributor::screenPixelHeight);

        // Process point lights
        PointLightList pointLightList;
        processLights(lightList, _viewManager, pointLightList, RandomInsertAndRemoveEntities);

        _deferredShader->runShader(&pointLightList, _viewManager, *_gBuffers);

        HLSLShader::releaseOM(rts);

        finalRender = _renderTexture;
    }

    DXLayer::instance()->addCmdListIndex();
    auto thread = new std::thread(&DXLayer::flushCommandList, DXLayer::instance(), finalRender);
    thread->detach();

    _audioManager->update();
}
