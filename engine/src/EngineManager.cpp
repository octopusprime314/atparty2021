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
    _graphicsLayer = GraphicsLayer::DXR_PATHTRACER;
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

    _pathTracerShader = static_cast<PathTracerShader*>(ShaderBroker::instance()->getShader("pathTracerShader"));
    _bloom            = new Bloom();
    _add              = new SSCompute("add",
                                       IOEventDistributor::screenPixelWidth,
                                       IOEventDistributor::screenPixelHeight,
                                       TextureFormat::RGBA_UNSIGNED_BYTE);

    // Setup pre and post draw callback events received when a draw call is issued
    IOEvents::setPreDrawCallback(std::bind(&EngineManager::_preDraw, this));
    IOEvents::setPostDrawCallback(std::bind(&EngineManager::_postDraw, this));

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

    if (_graphicsLayer == GraphicsLayer::DXR_PATHTRACER)
    {
        _pathTracerShader->runShader(lightList, _viewManager);

        DXLayer::instance()->addCmdListIndex();
        auto albedoUAV = _pathTracerShader->getCompositedFrame();
        auto thread    = new std::thread(&DXLayer::flushCommandList, DXLayer::instance(), albedoUAV);
        thread->detach();
    }

    _audioManager->update();
}