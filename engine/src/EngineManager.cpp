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
#include "Randomization.h"
#include "SSCompute.h"
#include "SceneBuilder.h"
#include "ShaderBroker.h"
#include "ViewEventDistributor.h"
#include "StaticShader.h"
#include "MRTFrameBuffer.h"
#include "DeferredShader.h"
#include "MergeShader.h"
#include "SSAO.h"
#include <chrono>
#include "PathTracerShader.h"

ResourceManager* EngineManager::_rayTracingPipeline = nullptr;
GraphicsLayer             EngineManager::_graphicsLayer;
EngineManager*            EngineManager::_engineManager = nullptr;

EngineManager::EngineManager(int* argc, char** argv, HINSTANCE hInstance, int nCmdShow)
{
    // seed the random number generator
    Randomization::seed();

    // initialize engine manager pointer so it can be used a singleton
    _engineManager = this;
    _graphicsLayer = GraphicsLayer::DXR_1_1_PATHTRACER;
    _generatorMode = false;
    _shadowEntity  = nullptr;

    DXLayer::initialize(hInstance, nCmdShow);

    _inputLayer         = new IOEventDistributor(argc, argv, hInstance, nCmdShow, "evil-suzanne");
    _rayTracingPipeline = new ResourceManager();

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

    _ssaoPass         = new SSAO();
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

    std::string::size_type sz; // alias of size_t
    auto meshModel0 = ModelBroker::instance()->getModel("particle");
    auto meshModel1 = ModelBroker::instance()->getModel("particle1");
    auto meshModel2 = ModelBroker::instance()->getModel("particle2");
    auto meshModel3 = ModelBroker::instance()->getModel("particle3");

    SceneEntity sceneEntity;

    const int numParticlesPerOrigin = 750;
    // random floats between -1.0 - 1.0
    std::random_device               rd;
    std::mt19937                     generator(rd());
    std::uniform_real_distribution<> randomFloats(-1.0, 1.0);


    std::vector<Vector4> particleOrigins = {
        Vector4(-13.05, -0.41, -0.63)/*, Vector4(-13.45, -0.41, -0.63),  Vector4(-13.85, -0.41, -0.63),*/
        //Vector4(0.05, -0.41, -0.63), Vector4(0.5, -0.41, -0.63),  Vector4(0.25, -0.41, -0.63),
        /*Vector4(0.05, -0.41, -1.63), Vector4(0.5, -0.41, -1.63),  Vector4(0.25, -0.41, -1.63),
        Vector4(0.05, -0.41, -2.63), Vector4(0.5, -0.41, -2.63),  Vector4(0.25, -0.41, -2.63),
        Vector4(0.05, -0.41, -3.63), Vector4(0.5, -0.41, -3.63), Vector4(0.25, -0.41, -3.63),
        Vector4(0.5, -0.41, -4.63), Vector4(0.75, -0.41, -4.63), Vector4(1.0, -0.41, -4.63),
        Vector4(1.25, -0.41, -5.63),  Vector4(1.5, -0.41, -5.63), Vector4(1.75, -0.41, -5.63),
        Vector4(2.0, -0.41, -6.63),  Vector4(2.25, -0.41, -6.63), Vector4(2.50, -0.41, -6.63),
        Vector4(2.5, -0.41, -7.63),  Vector4(2.75, -0.41, -7.63), Vector4(3.0, -0.41, -7.63),*/
    };

    //// End of cave location
    //std::vector<Vector4> particleOrigins = {
    //    Vector4(10.45, 0.01, -10.93),
    //};

    int particleOriginIndex = 0;
    for (int i = 0; i < numParticlesPerOrigin * particleOrigins.size(); i++)
    {
        // Random colorful particles
        float modelChoice = randomFloats(generator);
        //if (modelChoice <= -0.5)
        //{
        //    sceneEntity.modelname =
        //        meshModel0->getName().substr(0, meshModel0->getName().find_last_of("."));
        //}
        //else if (modelChoice > -0.5 && modelChoice <= 0.0)
        //{
        //    sceneEntity.modelname =
        //        meshModel1->getName().substr(0, meshModel1->getName().find_last_of("."));
        //}
        //else if (modelChoice > 0.0 && modelChoice <= 0.5)
        //{
        //    sceneEntity.modelname =
        //        meshModel2->getName().substr(0, meshModel2->getName().find_last_of("."));
        //}
        //else if (modelChoice > 0.5)
        //{
            sceneEntity.modelname =
                meshModel3->getName().substr(0, meshModel3->getName().find_last_of("."));
        //}

        //sceneEntity.modelname = meshModel0->getName().substr(0, meshModel0->getName().find_last_of("."));

        int particleGroupId = i / numParticlesPerOrigin;

        sceneEntity.name      = /*sceneEntity.modelname*/ "particle_lod1" + std::to_string(particleGroupId);
        sceneEntity.position  = particleOrigins[particleOriginIndex];
        sceneEntity.rotation  = Vector4(0.0, 0.0, 0.0);
        auto scale            = (randomFloats(generator) + 1.0) / 2.0;
        sceneEntity.scale     = Vector4(scale * 0.001, scale * 0.001,  scale * 0.001);
        auto transform = Matrix::translation(sceneEntity.position.getx(), sceneEntity.position.gety(), sceneEntity.position.getz()) *
                         Matrix::rotationAroundY(sceneEntity.rotation.gety()) *
                         Matrix::rotationAroundZ(sceneEntity.rotation.getz()) *
                         Matrix::rotationAroundX(sceneEntity.rotation.getx()) *
                         Matrix::scale(sceneEntity.scale.getx(), sceneEntity.scale.gety(), sceneEntity.scale.getz());

        sceneEntity.useTransform = true;
        sceneEntity.transform    = transform;

        EngineManager::instance()->addEntity(sceneEntity);

        particleOriginIndex = i / numParticlesPerOrigin;
    }

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
    _scene->sceneEntities[sceneEntity.name].entityPtr = entityList.back();
    _entityListLock.unlock();

    return entityList.back();
}

void EngineManager::setEntityWayPointpath(std::string               name,
                                             std::vector<PathWaypoint> waypointVectors)
{
    _entityListLock.lock();
    _scene->sceneEntities[name].waypointVectors = waypointVectors;
    _scene->sceneEntities[name].entityPtr->reset(_scene->sceneEntities[name], _viewManager);
    _entityListLock.unlock();

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
                                  PointLightList&       lightList,
                                  bool                  addLights)
{
    // Use map to sort the lights based on distance from the viewer
    std::map<float, int> lightsSorted;
    // Get light positions
    unsigned int lightCount = 0;

    Vector4 cameraPos  = viewEventDistributor->getCameraPos();
    //cameraPos.getFlatBuffer()[2] = -cameraPos.getFlatBuffer()[2];

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

    int lightOffset = 0;
    for (auto& light : lights)
    {
        Vector4 lightPos     = light->getPosition();
        Vector4 lightVector  = cameraPos + lightPos;
        float   distanceFromLight = lightVector.getMagnitude();

        lightsSorted.insert(std::pair<float, int>(distanceFromLight, lightOffset));
        lightCount++;

        lightOffset++;
    }

    int       lightPosIndex    = 0;
    int       lightColorIndex  = 0;
    int       lightRangeIndex  = 0;
    int       totalLights      = 0;
    for (auto& lightIndex : lightsSorted)
    {
        auto light = lights[lightIndex.second];
        // Point lights need to remain stationary so move lights with camera space changes
        auto   pos       = light->getPosition();
        float* posBuff   = pos.getFlatBuffer();
        float* colorBuff = light->getColor().getFlatBuffer();
        for (int i = 0; i < 4; i++)
        {
            lightList.lightPosArray[lightPosIndex++]      = posBuff[i];
            lightList.lightColorsArray[lightColorIndex++] = colorBuff[i];
        }

        lightList.isPointLightArray[lightRangeIndex] = light->getType() == LightType::POINT ? 1 : 0;
        lightList.lightRangesArray[lightRangeIndex++] = light->getScale().getFlatBuffer()[0];

        totalLights++;
    }
    lightList.lightCount = lightCount;
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

GraphicsLayer         EngineManager::getGraphicsLayer()   { return _graphicsLayer;      }
std::vector<Entity*>* EngineManager::getEntityList()      { return &_scene->entityList; }
ResourceManager*      EngineManager::getResourceManager() { return _rayTracingPipeline; }

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

        ResourceManager* resourceManager = EngineManager::getResourceManager();
        resourceManager->updateResources();

        _singleDrawRaster->startEntity();

        _singleDrawRaster->runShader(_scene->entityList);

        HLSLShader::releaseOM(_gBuffers->getTextures());

        // Only compute ssao for opaque objects
        //_ssaoPass->computeSSAO(_gBuffers, _viewManager);

        std::vector<RenderTexture> rts;
        rts.push_back(*_renderTexture);
        rts.push_back(*_depthTexture);
        HLSLShader::setOM(rts, IOEventDistributor::screenPixelWidth, IOEventDistributor::screenPixelHeight);

        // Process point lights
        PointLightList pointLightList;
        processLights(lightList, _viewManager, pointLightList, RandomInsertAndRemoveEntities);

        _deferredShader->runShader(&pointLightList, _viewManager, *_gBuffers, static_cast<RenderTexture*>(_ssaoPass->getBlur()->getTexture()));

        // Render billboard effects associated with lights
        for (Light* light : lightList)
        {
            light->render();
        }

        HLSLShader::releaseOM(rts);

        //Bloom*     bloom = getBloomShader();
        //SSCompute* add   = getAddShader();
        //
        //add->uavBarrier();
        //
        //// Compute bloom from finalized render target
        //bloom->compute(_renderTexture);
        //
        //// Adds bloom data back into the composite render target
        //add->compute(bloom->getTexture(), _renderTexture);
        //
        //add->uavBarrier();

        RenderTexture& velocityTexture = _gBuffers->getTextures()[3];
        auto mergeShader = static_cast<MergeShader*>(ShaderBroker::instance()->getShader("mergeShader"));
        mergeShader->runShader(_renderTexture, &velocityTexture);

        finalRender = _renderTexture;
    }

    DXLayer::instance()->addCmdListIndex();
    auto thread = new std::thread(&DXLayer::flushCommandList, DXLayer::instance(), finalRender);
    thread->detach();

    // Pass waypoint idx to the audio manager if we're using a waypoint camera
    if (_viewManager->getCameraType() == ViewEventDistributor::CameraType::WAYPOINT) {
        auto waypointCamera = _viewManager->getWaypointCamera();
        //_audioManager->update(waypointCamera->getCurrentWaypointIdx());
    }
    else {
        //_audioManager->update();
    }
}
