#define _USE_MATH_DEFINES
#include "ViewEventDistributor.h"
#include "Entity.h"
#include "FunctionState.h"
#include "Logger.h"
#include "Matrix.h"
#include "Model.h"
#include "Randomization.h"
#include "ShaderBroker.h"
#include "StateVector.h"
#include "ViewEvents.h"
#include <cmath>
#include <iostream>

/* Printable keys */
#define KEY_SPACE 32
#define KEY_APOSTROPHE 39 /* ' */
#define KEY_COMMA 44      /* , */
#define KEY_MINUS 45      /* - */
#define KEY_PERIOD 46     /* . */
#define KEY_SLASH 47      /* / */
#define KEY_0 48
#define KEY_1 49
#define KEY_2 50
#define KEY_3 51
#define KEY_4 52
#define KEY_5 53
#define KEY_6 54
#define KEY_7 55
#define KEY_8 56
#define KEY_9 57
#define KEY_SEMICOLON 59 /* ; */
#define KEY_EQUAL 61     /* = */
#define KEY_A 65
#define KEY_B 66
#define KEY_C 67
#define KEY_D 68
#define KEY_E 69
#define KEY_F 70
#define KEY_G 71
#define KEY_H 72
#define KEY_I 73
#define KEY_J 74
#define KEY_K 75
#define KEY_L 76
#define KEY_M 77
#define KEY_N 78
#define KEY_O 79
#define KEY_P 80
#define KEY_Q 81
#define KEY_R 82
#define KEY_S 83
#define KEY_T 84
#define KEY_U 85
#define KEY_V 86
#define KEY_W 87
#define KEY_X 88
#define KEY_Y 89
#define KEY_Z 90
#define KEY_LEFT_BRACKET 91  /* [ */
#define KEY_BACKSLASH 92     /* \ */
#define KEY_RIGHT_BRACKET 93 /* ] */
#define KEY_GRAVE_ACCENT 96  /* ` */
#define KEY_WORLD_1 161      /* non-US #1 */
#define KEY_WORLD_2 162      /* non-US #2 */

ViewEventDistributor::ViewEventDistributor() { _viewEvents = new ViewEvents(); }

ViewEventDistributor::ViewEventDistributor(int* argc, char** argv, unsigned int viewportWidth,
                                           unsigned int viewportHeight)
{
    _viewEvents   = new ViewEvents();
    _godState     = true;  // Start in god view mode
    _trackedState = false; // Don't start on the track...yet
    _entityIndex  = 0;     // Start at index 0

    _prevMouseX = IOEventDistributor::screenPixelWidth / 2;
    _prevMouseY = IOEventDistributor::screenPixelHeight / 2;
    _gameState  = EngineState::getEngineState();

    _trackedCamera = &_vectorCamera;

    // Used to enable tracked camera
    //_trackedState = true;
    //_godState     = false;
    //_currCamera   = _trackedCamera;
    _currCamera = &_godCamera;
    _waypointCamera.reset();
    _vectorCamera.reset();
    _currCamera->getState()->setGravity(false);
    _currCamera->getState()->setContact(true);
    _currCamera->getState()->setActive(true);

    // Hook up to kinematic update for proper physics handling
    MasterClock::instance()->subscribeKinematicsRate(this,
        std::bind(&ViewEventDistributor::_updateKinematics, this, std::placeholders::_1));

    // Only used for path tracer so make events work properly
    IOEvents::viewDrawUpdate(std::bind(&ViewEventDistributor::_updateDraw, this));
}

ViewEventDistributor::~ViewEventDistributor() { delete _viewEvents; }

void ViewEventDistributor::displayViewFrustum()
{

    if (_currCamera == &_viewCamera)
    {
        //_godCamera.displayViewFrustum();
    }
    else if (_currCamera == &_godCamera)
    {
        _viewCamera.displayViewFrustum(_godCamera.getView());
    }
    else if (_currCamera == _trackedCamera)
    {
        _viewCamera.displayViewFrustum(_trackedCamera->getView());
    }
}
Vector4 ViewEventDistributor::getCameraPos()
{
    auto cameraView                = _currCamera->getView();

     Vector4 cameraPos = Vector4(cameraView.getFlatBuffer()[3],
                                 cameraView.getFlatBuffer()[7],
                                 cameraView.getFlatBuffer()[11]);

    return cameraPos;
}

Vector4 ViewEventDistributor::getPrevCameraPos()
{
    auto cameraView                = _prevCameraView;

    Vector4 cameraPos = Vector4(cameraView.getFlatBuffer()[3], cameraView.getFlatBuffer()[7],
                                cameraView.getFlatBuffer()[11]);
    return cameraPos;
}

Matrix ViewEventDistributor::getPrevCameraView()
{
    return _prevCameraView;
}

Vector4 ViewEventDistributor::getCameraRot()
{
    Vector4 rotation = _currCamera->getState()->getAngularPosition();
    return Vector4(rotation.getx(), rotation.gety(), rotation.getz());
}

void ViewEventDistributor::setProjection(unsigned int viewportWidth, unsigned int viewportHeight,
                                         float nearPlaneDistance, float farPlaneDistance)
{
    // 45 degree angle up/down/left/right,
    // width by height aspect ratio
    // near plane from camera location
    // far plane from camera location
    _currCamera->setProjection(Matrix::projection(
        45.0f, static_cast<float>(viewportWidth) / static_cast<float>(viewportHeight),
        nearPlaneDistance, farPlaneDistance));
    _godCamera.setProjection(_currCamera->getProjection());
    _viewCamera.setProjection(_currCamera->getProjection());
    _trackedCamera->setProjection(_currCamera->getProjection());
}

void ViewEventDistributor::setView(Matrix translation, Matrix rotation, Matrix scale)
{
    Vector4 zero(0.f, 0.f, 0.f);
    _scale           = scale;
    _rotation        = rotation;
    _inverseRotation = rotation.inverse();
    _translation     = translation;

    _currCamera->setView(_translation, _rotation, _scale);

    _godCamera.setViewMatrix(_currCamera->getView());
    _viewCamera.setViewMatrix(_currCamera->getView());
    _trackedCamera->setViewMatrix(_currCamera->getView());
}

void ViewEventDistributor::setCamera(const CameraSettings&            settings,
                                     const std::vector<PathWaypoint>* waypoints)
{
    _lockedEntity = settings.lockedEntity;
    _lockOffset   = settings.lockOffset;
    _bobble       = settings.bobble;
    if (settings.type == CameraType::VECTOR)
    {
        std::string vec_file = settings.path;
        _vectorCamera.setVectorsFromFile(vec_file);
        _trackedState = true;
        _godState     = false;
        _waypointCamera.reset();
        _vectorCamera.reset();
        _trackedCamera = &_vectorCamera;
        _currCamera    = _trackedCamera;

        setProjection(IOEventDistributor::screenPixelWidth, IOEventDistributor::screenPixelHeight,
                      0.1f, 5000.0f);
        setView(Matrix::translation(584.0f, -5.0f, 20.0f), Matrix::rotationAroundY(-180.0f),
                Matrix());

        StateVector* state = _currCamera->getState();
        state->setGravity(false);
        state->setActive(true);
        state->setLinearPosition(settings.position);
        state->setAngularPosition(settings.rotation);
    }
    else if (settings.type == CameraType::WAYPOINT)
    {
        StateVector* state = _waypointCamera.getState();
        state->setContact(false);
        state->setGravity(false);
        state->setActive(true);
        state->setLinearPosition(settings.position);
        state->setAngularPosition(settings.rotation);

        if (waypoints == nullptr)
        {
            std::string wp_file = settings.path;
            _waypointCamera.setWaypointsFromFile(wp_file);
        }
        else
        {
            _waypointCamera.setWaypoints(*waypoints);
        }
        _trackedState = true;
        _godState     = false;
        _vectorCamera.reset();
        _trackedCamera = &_waypointCamera;
        _currCamera    = _trackedCamera;

        setProjection(IOEventDistributor::screenPixelWidth, IOEventDistributor::screenPixelHeight,
                      0.1f, 5000.0f);

        _waypointCamera.reset();
    }
    else
    {
        _trackedState      = true;
        _godState          = true;
        _currCamera        = &_godCamera;
        StateVector* state = _currCamera->getState();
        state->setContact(true);
        state->setGravity(false);
        state->setActive(false);
        state->setLinearPosition(settings.position);
        state->setAngularPosition(settings.rotation);
    }
}

void ViewEventDistributor::triggerEvents()
{
    _viewEvents->updateProjection(_currCamera->getProjection());
    _viewEvents->updateView(_currCamera->getView());
}

Matrix ViewEventDistributor::getProjection() { return _currCamera->getProjection(); }

Matrix ViewEventDistributor::getView()
{
    auto cameraView                = _currCamera->getView();

    cameraView.getFlatBuffer()[8]  = -cameraView.getFlatBuffer()[8];
    cameraView.getFlatBuffer()[9]  = -cameraView.getFlatBuffer()[9];
    cameraView.getFlatBuffer()[10] = -cameraView.getFlatBuffer()[10];
    cameraView.getFlatBuffer()[11] = -cameraView.getFlatBuffer()[11];

    //cameraView.getFlatBuffer()[4] = -cameraView.getFlatBuffer()[4];
    //cameraView.getFlatBuffer()[5] = -cameraView.getFlatBuffer()[];
    //cameraView.getFlatBuffer()[6] = -cameraView.getFlatBuffer()[2];
    //cameraView.getFlatBuffer()[7] = -cameraView.getFlatBuffer()[3];
    return cameraView;
}

Matrix ViewEventDistributor::getFrustumProjection() { return _viewCamera.getProjection(); }

Matrix ViewEventDistributor::getFrustumView() { return _viewCamera.getView(); }

Camera::ViewState ViewEventDistributor::getViewState() { return _currCamera->getViewState(); }

void ViewEventDistributor::setEntityList(std::vector<Entity*> entityList)
{
    _entityList = entityList;
}

ViewEvents* ViewEventDistributor::getEventWrapper() { return _viewEvents; }

void ViewEventDistributor::_updateReleaseKeyboard(int key, int x, int y)
{
    // If function state exists
    if (_keyboardState.find(key) != _keyboardState.end())
    {
        _keyboardState[key]->kill();
        delete _keyboardState[key];
        _keyboardState.erase(key); // erase by key
    }
}

void ViewEventDistributor::_updateKinematics(int milliSeconds)
{
    _waypointCamera.setInversion(_inverseRotation);
    _vectorCamera.setInversion(_inverseRotation);
    _waypointCamera.setInversion(_inverseRotation);
    Vector4 position = _currCamera->getState()->getLinearPosition();

    // Do kinematic calculations for god camera
    _currCamera->updateState(milliSeconds);

    if (_trackedState && _lockedEntity >= 0 && _lockedEntity < _entityList.size())
    {
        _currCamera->getState()->setLinearPosition(
            _entityList[_lockedEntity]->getStateVector()->getLinearPosition() + _lockOffset);
    }

    // Pass position information to model matrix
    _translation = Matrix::translation(-position.getx(), -position.gety(), -position.getz());
}

WaypointCamera* ViewEventDistributor::getWaypointCamera() { return &_waypointCamera; }

Vector4 ViewEventDistributor::getEyeDirection()
{
    auto    viewMatrix = _currCamera->getView().getFlatBuffer();
    Vector4 eyeDirection(-viewMatrix[1], -viewMatrix[5], -viewMatrix[9]);
    return eyeDirection;
}

void ViewEventDistributor::_updateKeyboard(int key, int x, int y)
{
    if (_gameState.gameModeEnabled)
    {
        _currCamera->setViewState(key);

        if (!_trackedState && (key == KEY_W || key == KEY_S || key == KEY_A ||
                               key == KEY_D || key == KEY_E || key == KEY_C))
        {

            Vector4     trans;
            const float velMagnitude = 80.0f;

            if (key == KEY_W)
            { // forward w
                trans = Vector4(_inverseRotation * Vector4(0.0, 0.0, -velMagnitude));
            }
            else if (key == KEY_S)
            { // backward s
                trans = Vector4(_inverseRotation * Vector4(0.0, 0.0, velMagnitude));
            }
            else if (key == KEY_D)
            { // right d
                trans = Vector4(_inverseRotation * Vector4(velMagnitude, 0.0, 0.0));
            }
            else if (key == KEY_A)
            { // left a
                trans = Vector4(_inverseRotation * Vector4(-velMagnitude, 0.0, 0.0));
            }
            else if (key == KEY_E)
            { // up e
                trans = Vector4(_inverseRotation * Vector4(0.0, velMagnitude, 0.0));
            }
            else if (key == KEY_C)
            { // down c
                trans = Vector4(_inverseRotation * Vector4(0.0, -velMagnitude, 0.0));
            }

            StateVector* state = nullptr;
            // If not in god camera view mode then push view changes to the model
            // for full control of a model's movements
            if (!_godState && _entityIndex < _entityList.size())
            {
                state = _entityList[_entityIndex]->getStateVector();
            }
            else if (_godState)
            {
                state = _currCamera->getState();
                state->setActive(true);
            }

            // Define lambda equation
            auto lamdaEq = [=](float t) -> Vector4 {
                if (t > 1.0f)
                {
                    return trans;
                }
                else
                {
                    return static_cast<Vector4>(trans) * t;
                }
            };
            // lambda function container that manages force model
            // Last forever in intervals of 5 milliseconds
            FunctionState* func = new FunctionState(
                std::bind(&StateVector::setForce, state, std::placeholders::_1), lamdaEq, 5);

            // Keep track to kill function when key is released
            if (_keyboardState.find(key) != _keyboardState.end())
            {
                _keyboardState[key]->kill();
                delete _keyboardState[key];
                _keyboardState.erase(key); // erase by key
            }
            _keyboardState[key] = func;
        }
        else if (key == KEY_Y)
        {
            static const double mouseSensitivity = 15.5f;
            Vector4 newRot = Vector4(0.0, static_cast<float>(mouseSensitivity * 200.0f), 0.0);
            _currCamera->getState()->setTorque(newRot);

            // If not in god camera view mode then push view changes to the model for full control
            // of a model's movements
            if (!_godState)
            {
                _currCamera->setViewMatrix(_currCamera->getView());
            }
            _currCamera->getState()->setActive(true);
        }
        else if (key == KEY_G)
        { // God's eye view change g
            _godState     = true;
            _trackedState = false;

            if (_currCamera == _trackedCamera)
            {
                StateVector* state = _trackedCamera->getState();
                state->setActive(false);
                state->setContact(true);
                state->setTorque(Vector4(0, 0, 0));
                state->setForce(Vector4(0, 0, 0));
                state->setLinearAcceleration(Vector4(0, 0, 0));
                state->setLinearVelocity(Vector4(0, 0, 0));
                state->setAngularAcceleration(Vector4(0, 0, 0));
                state->setAngularVelocity(Vector4(0, 0, 0));
                _godCamera.setState(state);
                _currCamera = &_godCamera;
            }
        }
        else if (key == KEY_T)
        {
            std::string vec_file = "../assets/paths/path.txt";
            _vectorCamera.setVectorsFromFile(vec_file);
            _trackedState = true;
            _godState     = false;
            _waypointCamera.reset();
            _vectorCamera.reset();
            _currCamera = _trackedCamera;
            _currCamera->getState()->setGravity(false);
            _currCamera->getState()->setActive(true);

            setProjection(IOEventDistributor::screenPixelWidth,
                          IOEventDistributor::screenPixelHeight, 0.1f, 5000.0f);
            setView(Matrix::translation(584.0f, -5.0f, 20.0f), Matrix::rotationAroundY(-180.0f),
                    Matrix());

            StateVector* state = _currCamera->getState();
            _updateView(_currCamera, state->getLinearPosition(), state->getAngularPosition());
        }
        else if (key == KEY_Q)
        { // Cycle through model's view point q

            if (_entityList.size() > 0)
            {
                _entityIndex++; // increment to the next model when q is pressed again
                if (_entityIndex >= _entityList.size())
                {
                    _entityIndex = 0;
                }

                _trackedState = false;
                _godState     = false;
                _currCamera   = &_viewCamera;

                StateVector* state = _entityList[_entityIndex]->getStateVector();
                _updateView(_currCamera, state->getLinearPosition(), state->getAngularPosition());
            }
        }
    }
}

void ViewEventDistributor::_updateGameState(EngineStateFlags state) { _gameState = state; }

void ViewEventDistributor::_updateMouse(double x, double y)
{ // Do stuff based on mouse update

    static const double mouseSensitivity = 15.5f;

    if (!_trackedState && _gameState.gameModeEnabled)
    {

        Vector4 newRot = Vector4(0.0, 0.0, 0.0);

        // Filter out large changes because that causes view twitching
        if (x != _prevMouseX && x != IOEventDistributor::screenPixelWidth / 2)
        {

            double diffX = _prevMouseX - x;

            if (diffX > 0)
            { // rotate left around y axis
                newRot = newRot + Vector4(0.0, static_cast<float>(mouseSensitivity * diffX), 0.0);
            }
            else if (diffX < 0)
            { // rotate right around y axis
                newRot = newRot + Vector4(0.0, static_cast<float>(mouseSensitivity * diffX), 0.0);
            }
            _currCamera->getState()->setTorque(newRot);

            // If not in god camera view mode then push view changes to the model for full control
            // of a model's movements
            if (!_godState)
            {
                _currCamera->setViewMatrix(_currCamera->getView());
            }
            _currCamera->getState()->setActive(true);

            /*char str[256];
            sprintf(str, "Mouse x: %f y: %f, Delta x: %f, Delta y: %f\n", x, y, _prevMouseX - x,
            _prevMouseY - y); OutputDebugString(str);*/
        }

        if (y != _prevMouseY && y != IOEventDistributor::screenPixelHeight / 2)
        {

            double diffY = _prevMouseY - y;

            if (diffY > 0)
            { // rotate left around y axis
                newRot = newRot + Vector4(static_cast<float>(mouseSensitivity * diffY), 0.0, 0.0);
            }
            else if (diffY < 0)
            { // rotate right around y axis
                newRot = newRot + Vector4(static_cast<float>(mouseSensitivity * diffY), 0.0, 0.0);
            }
            _currCamera->getState()->setTorque(newRot);

            // If not in god camera view mode then push view changes to the model for full control
            // of a model's movements
            if (!_godState)
            {
                _currCamera->setViewMatrix(_currCamera->getView());
            }
            _currCamera->getState()->setActive(true);

            /*char str[256];
            sprintf(str, "Mouse x: %f y: %f, Delta x: %f, Delta y: %f\n", x, y, _prevMouseX - x,
            _prevMouseY - y); OutputDebugString(str);*/
        }
    }
    _prevMouseX = x;
    _prevMouseY = y;
}

void ViewEventDistributor::_updateView(Camera* camera, Vector4 posV, Vector4 rotV)
{
    _prevCameraPos  = getCameraPos();
    _prevCameraView = getView();

    float* pos = posV.getFlatBuffer();
    float* rot = rotV.getFlatBuffer();
    // Update the translation state matrix
    _translation = Matrix::translation(-pos[0], -pos[1], -pos[2]);
    // Update the rotation state matrix

    _rotation = Matrix::rotationAroundX(rotV.getx()) *
                Matrix::rotationAroundY(rotV.gety()) *
                Matrix::rotationAroundZ(rotV.getz());

    _inverseRotation = Matrix::rotationAroundX(-rotV.getx()) *
                        Matrix::rotationAroundY(-rotV.gety()) *
                       Matrix::rotationAroundZ(-rotV.getz());

    // translate then rotate around point
    camera->setViewMatrix(_rotation * _translation);
    // Send out event to all listeners to offset locations essentially
    _viewEvents->updateView(camera->getView());
}

void ViewEventDistributor::_updateDraw()
{
    // Do draw stuff
    // If not in god camera view mode then push view changes to the model for full control of a
    // model's movements
    if (!_trackedState && !_godState && _entityIndex < _entityList.size())
    {
        StateVector* state = _entityList[_entityIndex]->getStateVector();
        _updateView(_currCamera, state->getLinearPosition(),
                    _currCamera->getState()->getAngularPosition());
    }
    else if (_godState || _trackedState)
    {
        StateVector* state    = _currCamera->getState();
        Vector4      position = state->getLinearPosition();
        Vector4      rotation = state->getAngularPosition();
        // bobble it!
        if (_bobble)
        { // Ignore this for now. WIP
            auto& bobbleVector = Randomization::pointInSphere();
            rotation += bobbleVector / 100;
        }
        _updateView(_currCamera, position, rotation);
    }

    // Turn off camera rotation effects if there hasn't been a change in mouse
    if (_currMouseX == _prevMouseX && _currMouseY == _prevMouseY)
    {

        _currCamera->getState()->setTorque(Vector4(0.0, 0.0, 0.0));
    }
    _currMouseX = _prevMouseX;
    _currMouseY = _prevMouseY;
}