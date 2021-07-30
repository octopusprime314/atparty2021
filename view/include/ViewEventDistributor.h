/*
 * ViewEventDistributor is part of the ReBoot distribution
 * (https://github.com/octopusprime314/ReBoot.git). Copyright (c) 2017 Peter Morley.
 *
 * ReBoot is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * ReBoot is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 *  ViewEventDistributor class.  Manages the transformation of the view state.
 *  Geometry that live in model-world space need to be updated in opposite
 *  fashion in order to properly move geometry around view.
 *  Classes that derive from EventSubscriber will receive view manager
 *  change events and will transform their model-world space to the updated
 *  view transform.
 */

#pragma once
#include "Camera.h"
#include "EventSubscriber.h"
#include "IOEventDistributor.h"
#include "VectorCamera.h"
#include "ViewEvents.h"
#include "WaypointCamera.h"
#include <memory>

class Model;
class Entity;
class FunctionState;
using FuncMap = std::map<unsigned char, FunctionState*>;

class ViewEventDistributor : public EventSubscriber
{
  public:
    enum class ViewState
    {
        DEFERRED_LIGHTING = 0,
        DIFFUSE,
        NORMAL,
        POSITION,
        VELOCITY,
        SCREEN_SPACE_AMBIENT_OCCLUSION,
        CAMERA_SHADOW,
        MAP_SHADOW,
        POINT_SHADOW,
        PHYSICS
    };

    enum class CameraType
    {
        GOD,
        VECTOR,
        WAYPOINT
    };

    struct CameraSettings
    {
        ViewEventDistributor::CameraType type;
        Vector4                          position;
        Vector4                          rotation;
        std::string                      path             = "";
        int                              lockedEntity     = -1;
        std::string                      lockedEntityName = "";
        Vector4                          lockOffset       = Vector4();
        bool                             bobble           = false;
    };

  private:
    Matrix               _inverseRotation; // Manages how to translate based on the inverse of the actual rotation
    WaypointCamera       _waypointCamera;
    FuncMap              _keyboardState;
    Camera*              _trackedCamera;
    bool                 _trackedState; // indicates whether the camera is on a track
    VectorCamera         _vectorCamera;
    Matrix               _translation; // Keep track of translation state
    int                  _entityIndex; // used to keep track of which model the view is set to
    ViewEvents*          _viewEvents;
    std::vector<Entity*> _entityList; // used to translate view to a model's transformation
    double               _prevMouseX;
    double               _prevMouseY;
    double               _currMouseX;
    double               _currMouseY;
    Camera               _viewCamera;
    Camera*              _currCamera;
    EngineStateFlags     _gameState;
    Camera               _godCamera;
    bool                 _godState; // indicates whether the view is in god or model view point mode
    Matrix               _rotation; // Keep track of rotation state
    Matrix               _scale;    // Keep track of scale state
    int                  _lockedEntity = -1;
    Vector4              _lockOffset;
    bool                 _bobble;
    Vector4              _prevCameraPos;
    Matrix               _prevCameraView;
    CameraType           _cameraType;

    void _updateKinematics(int milliSeconds);
    void _updateView(Camera* camera, Vector4 posV, Vector4 rotV);

  public:
    ViewEventDistributor();
    ViewEventDistributor(int* argc, char** argv, unsigned int viewportWidth,
                         unsigned int viewportHeight);
    ~ViewEventDistributor();
    void              setProjection(unsigned int viewportWidth, unsigned int viewportHeight,
                                    float nearPlaneDistance, float farPlaneDistance);
    void              setView(Matrix translation, Matrix rotation, Matrix scale);
    void              setEntityList(std::vector<Entity*> entityList);
    Matrix            getFrustumProjection();
    void              displayViewFrustum();
    ViewEvents*       getEventWrapper();
    Matrix            getFrustumView();
    Matrix            getProjection();
    void              triggerEvents();
    Vector4           getCameraPos();
    Vector4           getCameraRot();
    CameraType        getCameraType();
    Camera::ViewState getViewState();
    Matrix            getView();
    void              setCamera(const CameraSettings& settings, const std::vector<PathWaypoint>* waypoints = nullptr);
    WaypointCamera*   getWaypointCamera();
    Vector4           getEyeDirection();
    Vector4           getPrevCameraPos();
    Matrix            getPrevCameraView();

  protected:
    void _updateKeyboard(int key, int x, int y);        // Do stuff based on keyboard upate
    void _updateReleaseKeyboard(int key, int x, int y); // Do stuff based on keyboard release upate
    void _updateMouse(double x, double y);              // Do stuff based on mouse update
    void _updateGameState(EngineStateFlags state);
    void _updateDraw(); // Do draw stuff
};
