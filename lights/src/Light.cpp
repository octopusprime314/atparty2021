#include "Light.h"
#include "EngineManager.h"
#include "EngineScene.h"
#include "Logger.h"
#include "MasterClock.h"
#include "ModelBroker.h"
#include "ShaderBroker.h"
#include <random>

Light::Light(const SceneLight& sceneLight)
    : Effect(ModelBroker::instance()->getViewManager()->getEventWrapper(), sceneLight.effectType,
             sceneLight.position, sceneLight.rotation, sceneLight.scale),
      _type(sceneLight.lightType), _name(sceneLight.name), _color(sceneLight.color),
      _milliSecondTime(0)
{
    if (sceneLight.colorPath != "")
    {
        _colorPath = new ColorPath(sceneLight.colorPath);
    }

    if (sceneLight.lockedIdx > 0)
    {
        _lockedIdx = sceneLight.lockedIdx;
    }

    if (sceneLight.vectorPath != "")
    {
        setVectorPath(sceneLight.vectorPath);
    }

    if (sceneLight.waypointPath != "")
    {
        if (_waypointPath != nullptr)
        {
            delete _waypointPath;
        }

        _state.setGravity(false);
        _waypointPath = new WaypointPath(sceneLight.name, sceneLight.waypointPath, false, false);
        _waypointPath->resetState(&_state);
        _state.setActive(true);
    }

    if (sceneLight.waypointVectors.size() > 0)
    {
        if (_waypointPath != nullptr)
        {
            delete _waypointPath;
        }

        _state.setGravity(false);
        _waypointPath = new WaypointPath(sceneLight.name, sceneLight.waypointVectors, false, false);

        _waypointPath->resetState(&_state);
        _state.setActive(true);
    }

    setLightState(sceneLight.position, sceneLight.rotation, sceneLight.scale, sceneLight.color);

    // Hook up to kinematic update for proper physics handling
    MasterClock::instance()->subscribeKinematicsRate(this,
        std::bind(&Light::_updateKinematics, this, std::placeholders::_1));
}

Light::Light(ViewEvents* eventWrapper, MVP mvp, LightType type, EffectType effect, Vector4 color,
             Vector4 position, Vector4 scale)
    : Effect(eventWrapper, effect, position, Vector4(0.0, 0.0, 0.0, 1.0), scale), _type(type),
      _lightMVP(mvp), _color(color), _milliSecondTime(0)
{

    // Extract light position from view matrix
    float* inverseView = _lightMVP.getViewMatrix().inverse().getFlatBuffer();
    _position          = Vector4(inverseView[3], inverseView[7], inverseView[11], 1.0);

    // Hook up to kinematic update for proper physics handling
    MasterClock::instance()->subscribeKinematicsRate(this,
        std::bind(&Light::_updateKinematics, this, std::placeholders::_1));
}

Light::~Light()
{
    MasterClock::instance()->unsubscribeKinematicsRate(this);
    delete _vectorPath;
}

MVP Light::getLightMVP()
{

    // Move the positions of the lights based on the camera view except
    // the large map directional light that is used for low resolution
    // shadow map generation
    if (_type == LightType::SHADOWED_DIRECTIONAL || _type == LightType::DIRECTIONAL)
    {
        return _lightMVP;
    }
    else
    {
        return _lightMVP;
    }
}

MVP Light::getCameraMVP() { return _cameraMVP; }

Vector4 Light::getPosition()
{
    // Extract light position from model matrix
    float* model = _lightMVP.getModelMatrix().getFlatBuffer();
    auto position = Vector4(model[3], model[7], model[11], 1.0);
    return position;
}

Vector4 Light::getScale() { return _scale; }

Vector4 Light::getLightDirection()
{
    auto    viewMatrix = _lightMVP.getViewMatrix().getFlatBuffer();
    Vector4 lightDirection(-viewMatrix[1], -viewMatrix[5], -viewMatrix[9]);
    return lightDirection;
}

LightType Light::getType() { return _type; }

std::string Light::getName() { return _name; }

Vector4& Light::getColor() { return _color; }

bool Light::isShadowCaster()
{

    if (_type == LightType::SHADOWED_DIRECTIONAL || _type == LightType::SHADOWED_POINT ||
        _type == LightType::SHADOWED_SPOTLIGHT)
    {
        return true;
    }
    return false;
}

float Light::getRange()
{
    float* projMatrix = _lightMVP.getProjectionBuffer();
    float  farVal     = projMatrix[17];
    return farVal;
}

void  Light::setMVP(MVP mvp) { _lightMVP = mvp; }
float Light::getWidth()
{

    float* projMatrix = _lightMVP.getProjectionBuffer();
    float  width      = 2.0f * (1.0f - projMatrix[3]) / projMatrix[0];
    return width;
}
float Light::getHeight()
{

    float* projMatrix = _lightMVP.getProjectionBuffer();
    float  height     = 2.0f * (1.0f + projMatrix[7]) / projMatrix[5];
    return height;
}

void Light::_updateTime(int time)
{

    // Every update comes in real time so in order to speed up
    // we need to multiply that value by some constant
    // A full day takes one minute should do it lol
    // divide total time by 60 seconds times 1000 to convert to milliseconds
    uint64_t updateTimeAmplified = dayLengthMilliseconds / (60 * 1000);
    _milliSecondTime += (updateTimeAmplified * time);
    _milliSecondTime %= dayLengthMilliseconds;

    // if (_type == LightType::SHADOWED_DIRECTIONAL || _type == LightType::DIRECTIONAL) {

    //    //fraction of the rotation
    //    float posInRotation = static_cast<float>(_milliSecondTime) /
    //                          static_cast<float>(dayLengthMilliseconds);

    //    float* view         = _lightMVP.getViewMatrix().getFlatBuffer();
    //    float radiusOfLight = Vector4(view[3],
    //                                  view[7],
    //                                  view[11],
    //                                  1.0f).getMagnitude();

    //    _lightMVP.setView(Matrix::translation(0.0,
    //                                          0.0,
    //                                          radiusOfLight) *
    //                      Matrix::rotationAroundX(posInRotation * 360.0f));
    //}
}

void Light::render()
{
    if (_effectType != EffectType::None && _color.getw() > 0.0)
    {
        // Bring the time back to real time for the effects shader
        // The amount of milliseconds in 24 hours
        uint64_t updateTimeAmplified = dayLengthMilliseconds / (60 * 1000);
        float    realTimeMilliSeconds =
            static_cast<float>(_milliSecondTime) / static_cast<float>(updateTimeAmplified);
        //_effectShader->runShader(this, realTimeMilliSeconds / 1000.f);
    }
}

void Light::renderShadow(std::vector<Entity*> entityList){};

void Light::setVectorPath(const std::string& pathFile)
{
    _state.setContact(false);
    _state.setGravity(false);
    _vectorPath = new VectorPath(pathFile);
    _vectorPath->resetState(&_state);

    _state.setActive(true);
}

void Light::setColorPath(const std::string& pathFile) { _colorPath = new ColorPath(pathFile); }

void Light::setLightState(const Vector4& position, const Vector4& rotation, const Vector4& scale,
                          const Vector4& color)
{
    _position = position;
    _rotation = rotation;
    _scale    = scale;
    _color    = color;

    auto transform = Matrix::translation(position.getx(), position.gety(), position.getz()) *
                     Matrix::rotationAroundY(static_cast<float>(rotation.gety())) *
                     Matrix::rotationAroundZ(static_cast<float>(rotation.getz())) *
                     Matrix::rotationAroundX(static_cast<float>(rotation.getx()));

    _lightMVP.setModel(transform);
    _lightMVP.setView(ModelBroker::instance()->getViewManager()->getView());

    // Use the magnitude of the light scaled with a light range multiplier to give the range of the
    // light source
    const float rangeScale = 10;
    _lightMVP.setProjection(Matrix::projection(0.0f, 0.0, 0.0, _scale.getMagnitude() * rangeScale));
}

void Light::_updateKinematics(int milliSeconds)
{
    if (_lockedIdx >= 0)
    {

        Vector4 linearPos = EngineManager::instance()
                                ->getEntityList()
                                ->at(_lockedIdx)
                                ->getStateVector()
                                ->getLinearPosition();

        // Only scale or translate light sources...rotation blows up stuff because we need to
        // billboard lights
        Matrix kinematicTransform =
            Matrix::translation(linearPos.getx(), linearPos.gety(), linearPos.getz());

        _lightMVP.setModel(kinematicTransform);
    }
    else
    {
        // Do kinematic calculations
        if (_vectorPath != nullptr)
        {
            _vectorPath->updateState(milliSeconds, &_state,
                                     _waypointPath != nullptr ? true : false);
            //_vectorPath->updateState(milliSeconds, &_state);
            Vector4 position = _state.getLinearPosition();

            Matrix kinematicTransform =
                Matrix::translation(position.getx(), position.gety(), position.getz());

            auto transform =
                Matrix::translation(_position.getx(), _position.gety(), _position.getz());

            _lightMVP.setModel(kinematicTransform * transform);
        }

        if (_waypointPath != nullptr)
        {
            _waypointPath->updateState(milliSeconds, &_state);

            Matrix kinematicTransform = _state.getTransform();

            _lightMVP.setModel(kinematicTransform);
        }
    }
    if (_colorPath != nullptr)
    {
        _colorPath->updateColor(milliSeconds, _color);
        // LOG_INFO(_color);
    }

    _updateTime(milliSeconds);
}