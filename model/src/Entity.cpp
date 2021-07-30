#include "Entity.h"
#include "EngineScene.h"
#include "IOEventDistributor.h"
#include "Logger.h"
#include "Model.h"
#include "ModelBroker.h"
#include "ShaderBroker.h"

unsigned int Entity::_idGenerator = 1;

Entity::Entity(Model* model, ViewEvents* eventWrapper, MVP transforms)
    : EventSubscriber(eventWrapper), _clock(MasterClock::instance()), _model(model),
      _id(_idGenerator), _selected(false), _frustumRenderBuffers(new std::vector<RenderBuffers>()),
      _gameState(EngineState::getEngineState()), _layeredTexture(nullptr), _rayTracingTextureId(0)
{
    _worldSpaceTransform = transforms.getModelMatrix();
    _mvp.setProjection(transforms.getProjectionMatrix());
    _mvp.setView(transforms.getViewMatrix());

    if (_model->getClassType() == ModelClass::ModelType)
    {
        // Tile the terrain and other static objects in the scene
        //_generateVAOTiles();
        _idGenerator++;
    }

    // Hook up to kinematic update for proper physics handling
    _clock->subscribeKinematicsRate(this,
        std::bind(&Entity::_updateKinematics, this, std::placeholders::_1));
}

Entity::Entity(const SceneEntity& sceneEntity, ViewEventDistributor* viewManager)
    : EventSubscriber(viewManager->getEventWrapper()), _clock(MasterClock::instance()),
      _id(_idGenerator), _selected(false), _frustumRenderBuffers(new std::vector<RenderBuffers>()),
      _gameState(EngineState::getEngineState()), _layeredTexture(nullptr), _rayTracingTextureId(0)
{
    reset(sceneEntity, viewManager);

    if (_model->getClassType() == ModelClass::ModelType)
    {
        // Tile the terrain and other static objects in the scene
        //_generateVAOTiles();
        _idGenerator++;
    }

    // Hook up to kinematic update for proper physics handling
    _clock->subscribeKinematicsRate(this,
        std::bind(&Entity::_updatePathKinematics, this, std::placeholders::_1));
}

Entity::~Entity()
{
    for (auto vectorPath : _vectorPaths)
    {
        delete vectorPath;
    }
    if (_waypointPath != nullptr)
    {
        delete _waypointPath;
    }

    // Hook up to kinematic update for proper physics handling
    _clock->unsubscribeKinematicsRate(this);
}

void Entity::reset(const SceneEntity& sceneEntity, ViewEventDistributor* viewManager)
{
    _model = ModelBroker::instance()->getModel(sceneEntity.modelname);
    _name  = sceneEntity.name;

    if (sceneEntity.useTransform)
    {
        Matrix xform = sceneEntity.transform;
        setState(xform);
    }
    else
    {
        setState(sceneEntity.position, sceneEntity.rotation, sceneEntity.scale);
    }

    if (sceneEntity.vectorPaths.empty() == false)
    {
        setVectorPaths(sceneEntity.vectorPaths);
    }
    if (sceneEntity.waypointPath.size() > 0)
    {
        if (_waypointPath != nullptr)
        {
            delete _waypointPath;
        }

        _state.setGravity(false);
        _waypointPath = new WaypointPath(_model->getName(), sceneEntity.waypointPath, false, false);
        _waypointPath->resetState(&_state);
        _state.setActive(true);
    }

    if (sceneEntity.waypointVectors.size() > 0)
    {
        _state.setGravity(false);
        _waypointPath = new WaypointPath(_model->getName(), sceneEntity.waypointVectors, false, false);

        // Use the first scale value and assume it never changes
        _scale = sceneEntity.waypointVectors[0].scale;

        _waypointPath->resetState(&_state);
        _state.setActive(true);
    }
}

void Entity::entranceWaypoint(Vector4 initialPos, Vector4 initialRotation, float time)
{

    Vector4 finalPosition = _state.getLinearPosition();
    Vector4 finalRotation = _state.getAngularPosition();

    setState(initialPos, initialRotation, _scale);

    if (_waypointPath != nullptr)
    {
        delete _waypointPath;
    }

    _state.setGravity(false);
    _waypointPath =
        new WaypointPath(_model->getName(), finalPosition, finalRotation, time, false, false);

    _waypointPath->resetState(&_state);
    _state.setActive(true);

    _enteredView = true;
}

void Entity::_updateDraw()
{
    Matrix inverseViewProjection = ModelBroker::getViewManager()->getView().inverse() *
                                   ModelBroker::getViewManager()->getProjection().inverse();

    if (1) // FrustumCuller::getVisibleAABB(this, frustumPlanes))
    {
        // Run model shader by allowing the shader to operate on the model
        _model->runShader(this);
    }
}

Model* Entity::getModel()
{
    // Used to query the correct lod
    //auto pos =
    //    Vector4(_worldSpaceTransform.getFlatBuffer()[3], _worldSpaceTransform.getFlatBuffer()[7],
    //            _worldSpaceTransform.getFlatBuffer()[11]);
    //return ModelBroker::instance()->getModel(_model->getName(), pos);
    return _model;
}

void Entity::setModel(Model* model) { _model = model; }


void Entity::_updateView(Matrix view)
{
    if (_waypointPath != nullptr && _waypointPath->isMoving() == false)
    {
        return;
    }
    _prevMVP.setView(_mvp.getViewMatrix());

    // Receive updates when the view matrix has changed
    _mvp.setView(view);

    // If view changes then change our normal matrix
    _mvp.setNormal(view.inverse().transpose());

    if (_waypointPath != nullptr)
        _waypointPath->updateView(view);
}

void Entity::_updateProjection(Matrix projection)
{
    // Receive updates when the projection matrix has changed
    _mvp.setProjection(projection);

    if (_waypointPath != nullptr)
        _waypointPath->updateProjection(projection);
}

bool Entity::getSelected() { return _selected; }

void Entity::setSelected(bool isSelected) { _selected = isSelected; }

void Entity::setVectorPaths(const std::vector<std::string>& pathFiles)
{
    // clear first
    for (int i = 0; i < _vectorPaths.size(); i++)
    {
        delete _vectorPaths[i];
    }
    _vectorPaths.clear();

    _state.setContact(false);
    _state.setGravity(false);
    int i = 0;
    for (std::string file : pathFiles)
    {
        _vectorPaths.push_back(new VectorPath(file));
        _vectorPaths[i]->resetState(&_state);
        i++;
    }
    _state.setActive(true);
}

void Entity::_updateKinematics(int milliSeconds)
{
    // Do kinematic calculations
    _state.update(milliSeconds);
    _prevMVP.setModel(_mvp.getModelMatrix());
    Vector4 position = _state.getLinearPosition();
    Matrix  kinematicTransform =
        Matrix::translation(position.getx(), position.gety(), position.getz());
    auto totalTransform = kinematicTransform * _worldSpaceTransform;
    _mvp.setModel(totalTransform);
}

void Entity::_updatePathKinematics(int milliSeconds)
{
    if (_waypointPath != nullptr && _waypointPath->isMoving() == false)
    {
        return;
    }

    for (VectorPath* vectorPath : _vectorPaths)
    {
        vectorPath->updateState(milliSeconds, &_state, _waypointPath != nullptr ? true : false);
    }

    if (_waypointPath != nullptr)
    {
        _waypointPath->updateState(milliSeconds, &_state);
    }

    _prevMVP.setModel(_mvp.getModelMatrix());
    Vector4 linearPos  = _state.getLinearPosition();
    Vector4 angularPos = _state.getAngularPosition();
    // Compute x, y and z rotation vectors by multiplying through
    Matrix rotation = Matrix::rotationAroundY(static_cast<float>(angularPos.gety())) *
                      Matrix::rotationAroundZ(static_cast<float>(angularPos.getz())) *
                      Matrix::rotationAroundX(static_cast<float>(angularPos.getx()));

    Matrix kinematicTransform =
        Matrix::translation(linearPos.getx(), linearPos.gety(), linearPos.getz()) * rotation
         * Matrix::scale(_scale.getx(), _scale.gety(), _scale.getz());

    if (_waypointPath != nullptr)
    {
        _worldSpaceTransform = kinematicTransform;
    }
    else
    {
        _worldSpaceTransform = _initialWorldSpaceTransform;
    }
    
    _mvp.setModel(_worldSpaceTransform);
}

unsigned int Entity::getRayTracingTextureId() { return _rayTracingTextureId; }

void Entity::setRayTracingTextureId(unsigned int rtId) { _rayTracingTextureId = rtId; }

void Entity::_updateGameState(EngineStateFlags state) { _gameState = state; }

VAOMap Entity::getVAOMapping() { return _frustumVAOMapping; }

MVP* Entity::getMVP() { return &_mvp; }
void Entity::setMVP(MVP transforms)
{
    _worldSpaceTransform = transforms.getModelMatrix();
    _mvp.setProjection(transforms.getProjectionMatrix());
    _mvp.setView(transforms.getViewMatrix());
}
MVP* Entity::getPrevMVP() { return &_prevMVP; }

StateVector* Entity::getStateVector() { return &_state; }

unsigned int Entity::getID() { return _id; }

bool Entity::isID(unsigned int entityID)
{

    if (entityID == _id)
    {
        return true;
    }
    else
    {
        return false;
    }
}


std::vector<RenderBuffers>* Entity::getRenderBuffers() { return _frustumRenderBuffers; }

Matrix Entity::getWorldSpaceTransform() { return _worldSpaceTransform; }

bool Entity::isDynamic()
{
    return false;
}

bool Entity::isAnimated()
{
    return (_model->getClassType() == ModelClass::AnimatedModelType);
}

LayeredTexture* Entity::getLayeredTexture() { return _layeredTexture; }

void Entity::setLayeredTexture(LayeredTexture* layeredTexture) { _layeredTexture = layeredTexture; }

std::vector<VAO*>* Entity::getFrustumVAO()
{
    auto pos =
        Vector4(_worldSpaceTransform.getFlatBuffer()[3], _worldSpaceTransform.getFlatBuffer()[7],
                _worldSpaceTransform.getFlatBuffer()[11]);
    auto model = ModelBroker::instance()->getModel(_model->getName(), pos);

    
    _frustumVAOs.clear();
    auto addedVAOs = model->getVAO();
    // Do not add the original non frustum culled vao that needs to be used for shadows only
    int i = 0;
    for (auto vaoIndex : *addedVAOs)
    {
        _frustumVAOs.push_back(vaoIndex);
        i++;
    }
    return &_frustumVAOs;
}

void Entity::setPosition(Vector4 position)
{

    Matrix kinematicTransform =
        Matrix::translation(position.getx(), position.gety(), position.getz());
    auto totalTransform = kinematicTransform * _worldSpaceTransform;

    Vector4 pos = Vector4(totalTransform.getFlatBuffer()[3], totalTransform.getFlatBuffer()[7],
                          totalTransform.getFlatBuffer()[11]);

    _state.setLinearPosition(pos);

    _prevMVP.setModel(_mvp.getModelMatrix());

    _mvp.setModel(totalTransform);
}

void Entity::setState(const Vector4& position, const Vector4& rotation, const Vector4& scale)
{
    _scale = scale;

    Matrix objectSpaceTransform = Matrix();

    auto transform = Matrix::translation(position.getx(), position.gety(), position.getz()) *
                     Matrix::rotationAroundY(rotation.gety()) * 
                     Matrix::rotationAroundZ(rotation.getz()) *
                     Matrix::rotationAroundX(rotation.getx()) *
                     Matrix::scale(_scale.getx(), _scale.gety(), _scale.getz()) *
                     objectSpaceTransform;

    MVP worldSpaceTransform;
    worldSpaceTransform.setProjection(ModelBroker::getViewManager()->getProjection());
    worldSpaceTransform.setView(ModelBroker::getViewManager()->getView());
    worldSpaceTransform.setModel(transform);

    _worldSpaceTransform = worldSpaceTransform.getModelMatrix();
    _mvp.setProjection(worldSpaceTransform.getProjectionMatrix());
    _mvp.setView(worldSpaceTransform.getViewMatrix());

    _state.setLinearPosition(position);
    _state.setAngularPosition(rotation);
}

void Entity::setState(Matrix& transform)
{
    _scale = Vector4(1.0, 1.0, 1.0, 1.0); //scale;

    MVP worldSpaceTransform;
    worldSpaceTransform.setProjection(ModelBroker::getViewManager()->getProjection());
    worldSpaceTransform.setView(ModelBroker::getViewManager()->getView());
    worldSpaceTransform.setModel(transform);

    _worldSpaceTransform = worldSpaceTransform.getModelMatrix();
    _mvp.setProjection(worldSpaceTransform.getProjectionMatrix());
    _mvp.setView(worldSpaceTransform.getViewMatrix());

    _initialWorldSpaceTransform = _worldSpaceTransform;

    //_state.setLinearPosition(transform * Vector4(0.0, 0.0, 0.0, 1.0));

    //_state.setAngularPosition(rotation);
}

void Entity::setVelocity(Vector4 velocity) { _state.setLinearVelocity(velocity); }

WaypointPath* Entity::getWaypointPath() { return _waypointPath; }

std::string Entity::getName() { return _name; }

void Entity::setName(const std::string& name) { _name = name; }