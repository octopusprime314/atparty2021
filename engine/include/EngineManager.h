/*
 * EngineManager is part of the ReBoot distribution (https://github.com/octopusprime314/ReBoot.git).
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
 *  EngineManager class. Contains the view manager and models stored in the view space
 */

#pragma once
#include "EngineScene.h"
#include <memory>
#include <optional>
#include <vector>

class ViewEventDistributor;
class AudioManager;
class SSAO;
class FontRenderer;
class Bloom;
class SSCompute;
class ShaderBroker;
class Entity;
class IOEventDistributor;
class DXLayer;
class RayTracingPipelineShader;
class PathTracerShader;

enum class GraphicsLayer
{
    OPENGL,
    DX12,
    DXR,
    DXR_TRACERAYINLINE,
    DXR_PATHTRACER
};

class EngineManager
{

    static RayTracingPipelineShader* _rayTracingPipeline;
    static GraphicsLayer             _graphicsLayer;
    bool                             _useRaytracing;
    bool                             _generatorMode;
    std::mutex                       _entityListLock;
    Entity*                          _shadowEntity;
    // Manages audio playback
    AudioManager*                _audioManager;
    // manages the view/camera matrix from the user's perspective
    ViewEventDistributor*        _viewManager;
    PathTracerShader*            _pathTracerShader;
    unsigned int                 _pathCounter;
    // Contains models active in scene
    std::shared_ptr<EngineScene> _scene;
    FontRenderer*                _fontRenderer;
    IOEventDistributor*          _inputLayer;
    SSAO*                        _ssaoPass;
    Bloom*                       _bloom;
    SSCompute*                   _add;

    // Post of drawing objects call this function
    void _postDraw();
    // Prior to drawing objects call this function
    void _preDraw();

    static EngineManager* _engineManager;

    EngineManager(int* argc, char** argv, HINSTANCE hInstance, int nCmdShow);

  public:
    static EngineManager* instance(int* argc = nullptr, char** argv = nullptr,
                                   HINSTANCE hInstance = 0, int nCmdShow = 0);

    ~EngineManager();

    // Entity Management
    Entity*                    addEntity(SceneEntity sceneEntity);
    std::optional<SceneEntity> getSceneEntity(const std::string& entityName);
    std::vector<Entity*>*      getEntityList();
    ViewEventDistributor*      getViewManager() { return _viewManager; }
    Bloom*                     getBloomShader() { return _bloom; }
    SSCompute*                 getAddShader()   { return _add; }

    // Light Management
    Light* addLight(const SceneLight& sceneLight);
    void updateLightState(const std::string& name, const Vector4& position, const Vector4& rotation,
                          const Vector4& scale, const Vector4& color);
    std::optional<SceneLight> getSceneLight(const std::string& lightName);

    void setEngineScene(std::shared_ptr<EngineScene> scene) { _scene = scene; }

    static GraphicsLayer             getGraphicsLayer();
    static RayTracingPipelineShader* getRTPipeline();
};
