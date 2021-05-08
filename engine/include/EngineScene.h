#pragma once

#include "Entity.h"
#include "Light.h"
#include "Effect.h"
#include <vector>

struct SceneEntity
{
    std::string              name;
    std::string              modelname;
    Vector4                  position;
    Vector4                  rotation;
    Vector4                  scale;
    std::string              waypointPath;
    std::vector<std::string> vectorPaths;
};

struct SceneLight
{
    std::string name;
    std::string lockedName;
    int         lockedIdx;
    LightType   lightType;
    EffectType  effectType;
    Vector4     position;
    Vector4     rotation;
    Vector4     scale;
    Vector4     color;
    std::string vectorPath;
    std::string waypointPath;
    std::string colorPath;
};

struct SceneString
{
    std::string              text;
    Vector4                  position;
    Vector4                  rotation;
    Vector4                  scale;
    Vector4                  color;
    std::string              waypointPath;
    std::vector<std::string> vectorPaths;
    std::vector<int>         offsets;
};

struct EngineScene
{
    std::string                          name;
    std::string                          fbxScene;
    std::string                          soundFile;
    std::string                          skyboxDay;
    std::string                          skyboxNight;
    ViewEventDistributor::CameraSettings cameraSettings;
    std::vector<Entity*>                 entityList;
    std::vector<Light*>                  lightList;

    // Initial parsed data from the json. Used for keeping an original state
    std::unordered_map<std::string, SceneEntity> sceneEntities;
    std::unordered_map<std::string, SceneLight>  sceneLights;
    std::unordered_map<std::string, SceneString> sceneStrings;

    ~EngineScene()
    {
        for (auto& e : entityList)
        {
            delete e;
        }
        for (auto& l : lightList)
        {
            delete l;
        }
    }
};