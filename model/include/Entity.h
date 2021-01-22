/*
 * Entity is part of the ReBoot distribution (https://github.com/octopusprime314/ReBoot.git).
 * Copyright (c) 2017 Peter Morley.
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
 *  Entity class. Contains a model class with all of the render information and various
 *  physics/kinematic data about how the model moves in the world
 */

#pragma once
#include "EventSubscriber.h"
#include "MVP.h"
#include "MasterClock.h"
#include "StateVector.h"
#include "VectorPath.h"
#include "WaypointPath.h"
#include "LayeredTexture.h"
#include "ViewEventDistributor.h"
#include <iostream>
#include <vector>

class IOEventDistributor;
class Model;
class FrustumCuller;
struct SceneEntity;

using VAOMap = std::map<int, std::vector<VAO*>>;

class Entity : public EventSubscriber
{

  public:
    // Default model to type to base class
    Entity(Model* model, ViewEvents* eventWrapper, MVP worldSpaceTransform = MVP());
    Entity(const SceneEntity& sceneEntity, ViewEventDistributor* viewManager);
    virtual ~Entity();

    void setRayTracingTextureId(unsigned int rtId);
    void setLayeredTexture(LayeredTexture* layeredTexture);
    void setPosition(Vector4 position);
    void setState(const Vector4& position, const Vector4& rotation, const Vector4& scale);
    void setVelocity(Vector4 velocity);
    void setSelected(bool isSelected);
    void setVectorPaths(const std::vector<std::string>& pathFiles);
    bool isID(unsigned int entityID);
    bool isDynamic();
    bool isAnimated();

    Matrix                      getWorldSpaceTransform();
    unsigned int                getRayTracingTextureId();
    LayeredTexture*             getLayeredTexture();
    FrustumCuller*              getFrustumCuller();
    std::vector<RenderBuffers>* getRenderBuffers();
    StateVector*                getStateVector();
    std::vector<VAO*>*          getFrustumVAO();
    VAOMap                      getVAOMapping();
    void                        setMVP(MVP mvp);
    bool                        getSelected();
    MVP*                        getPrevMVP();
    Model*                      getModel();
    void                        setModel(Model* model);
    MVP*                        getMVP();
    unsigned int                getID();
    WaypointPath*               getWaypointPath();
    void        reset(const SceneEntity& sceneEntity, ViewEventDistributor* viewManager);
    void        entranceWaypoint(Vector4 initialPos, Vector4 initialRotation, float time);
    std::string getName();
    void        setName(const std::string& name);
    bool        getHasEntered() { return _enteredView; }

  protected:
    std::string                 _name;
    std::vector<RenderBuffers>* _frustumRenderBuffers;
    unsigned int                _rayTracingTextureId;
    Matrix                      _worldSpaceTransform;
    std::vector<VectorPath*>    _vectorPaths;
    WaypointPath*               _waypointPath = nullptr;

    VAOMap            _frustumVAOMapping;
    LayeredTexture*   _layeredTexture;
    std::vector<VAO*> _frustumVAOs;
    // id generator that is incremented every time a new Entity is added
    static unsigned int _idGenerator;
    EngineStateFlags    _gameState;
    bool                _selected;
    // Previous Model view matrix container for motion blur
    MVP          _prevMVP;
    StateVector  _state;
    MasterClock* _clock;
    Model*       _model;
    Vector4      _scale; // Used with pathing
    MVP          _mvp;
    unsigned int _id;
    bool         _enteredView = false;

    void _updateReleaseKeyboard(int key, int x, int y){};
    void _updateGameState(EngineStateFlags state);
    void _updateKeyboard(int key, int x, int y){};
    void _updateMouse(double x, double y){};
    void _updateProjection(Matrix projection);
    void _updateKinematics(int milliSeconds);
    void _updatePathKinematics(int milliSeconds);
    void _updateView(Matrix view);
    void _updateDraw();
};