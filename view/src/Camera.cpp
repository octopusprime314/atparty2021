#define _USE_MATH_DEFINES
#include "Camera.h"
#include "Logger.h"
#include "ShaderBroker.h"

Camera::Camera()
{
    _viewState = Camera::ViewState::DEFERRED_LIGHTING;

    // Used for god mode
    _state.setGravity(false);
}

Camera::~Camera() {}

void Camera::displayViewFrustum(Matrix view)
{
    Vector4 color(1.0, 0.0, 0.0);

    // Model transform to create frustum cube
    MVP mvp;
    mvp.setModel(_mvp.getViewMatrix().inverse() * _mvp.getProjectionMatrix().inverse());
    mvp.setView(view); // set current view matrix to place frustum in correct location
    mvp.setProjection(_mvp.getProjectionMatrix());
}

void Camera::setProjection(Matrix projection) { _mvp.setProjection(projection); }

void Camera::setViewMatrix(Matrix transform)
{
    _prevMvp = _mvp;
    _mvp.setView(transform);
}

StateVector* Camera::getState() { return &_state; }
void         Camera::setState(StateVector* state) { _state = *state; }
void         Camera::setFov(float fov) { _fov = fov; }
float        Camera::getFov() { return _fov; }

void Camera::setView(Matrix translation, Matrix rotation, Matrix scale)
{
    _prevMvp = _mvp;
    Vector4 zero(0.f, 0.f, 0.f);
    _state.setLinearPosition(translation * zero);
    //_state.setAngularPosition(rotation * zero);
    // Debug to set camera to look over procedural geometry.
    _state.setAngularPosition(Vector4(0.0, 0.0, 0.0));
    _mvp.setView(_mvp.getViewMatrix() * scale * rotation * translation);
}

Matrix Camera::getProjection() { return _mvp.getProjectionMatrix(); }

Matrix Camera::getView() { return _mvp.getViewMatrix(); }
Matrix Camera::getPrevView() { return _prevMvp.getViewMatrix(); }

Camera::ViewState Camera::getViewState() { return _viewState; }

void Camera::updateState(int milliseconds) { _state.update(milliseconds); }

void Camera::setViewState(int key)
{
    
}
