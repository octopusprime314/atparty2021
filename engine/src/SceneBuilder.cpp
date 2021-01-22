#include "SceneBuilder.h"
#include "AudioManager.h"
#include "EngineManager.h"
#include "ModelBroker.h"
#include "ViewEventDistributor.h"
#include "json.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>

namespace
{

SceneEntity build_entity(nlohmann::json& e)
{
    SceneEntity entity;
    entity.name = e["entity_name"];
    std::transform(entity.name.begin(), entity.name.end(), entity.name.begin(), ::toupper);
    entity.modelname = e["model_name"];
    auto td          = e["translation"];
    auto sd          = e["scale"];
    auto rd          = e["rotation"];
    entity.position  = Vector4(td["x"], td["y"], td["z"]);
    entity.scale     = Vector4(sd["x"], sd["y"], sd["z"]);
    entity.rotation  = Vector4(rd["x"], rd["y"], rd["z"]);

    auto it  = e.find("vector_path");
    auto wit = e.find("waypoint_path");
    if (it != e.end())
    {
        for (auto& vp : e["vector_path"])
        {
            entity.vectorPaths.emplace_back(vp);
        }
    }
    if (wit != e.end())
    {
        entity.waypointPath = e["waypoint_path"];
    }

    return entity;
}

SceneLight build_light(nlohmann::json& l)
{
    SceneLight light;
    light.name = l["light_name"];
    std::transform(light.name.begin(), light.name.end(), light.name.begin(), ::toupper);
    auto td        = l["translation"];
    auto sd        = l["scale"];
    auto rd        = l["rotation"];
    auto c         = l["color"];
    light.position = Vector4(td["x"], td["y"], td["z"]);
    light.rotation = Vector4(rd["x"], rd["y"], rd["z"]);
    light.color    = Vector4(c["r"], c["g"], c["b"]);
    light.scale    = Vector4(sd["x"], sd["y"], sd["z"]);

    if (l["light_type"] == "DIRECTIONAL")
    {
        light.lightType = LightType::DIRECTIONAL;
    }
    else if (l["light_type"] == "SHADOWED_DIRECTIONAL")
    {
        light.lightType = LightType::SHADOWED_DIRECTIONAL;
    }
    else if (l["light_type"] == "POINT")
    {
        light.lightType = LightType::POINT;
    }
    else if (l["light_type"] == "SHADOWED_POINT")
    {
        light.lightType = LightType::SHADOWED_POINT;
    }
    else if (l["light_type"] == "SPOTLIGHT")
    {
        light.lightType = LightType::SPOTLIGHT;
    }
    else if (l["light_type"] == "SHADOWED_SPOTLIGHT")
    {
        light.lightType = LightType::SHADOWED_SPOTLIGHT;
    }

    auto it = l.find("vector_path");
    if (it != l.end())
    {
        light.vectorPath = *it;
    }

    auto wit = l.find("waypoint_path");
    if (wit != l.end())
    {
        light.waypointPath = *wit;
    }

    auto cit = l.find("color_path");
    if (cit != l.end())
    {
        light.colorPath = *cit;
    }

    auto lit = l.find("locked");
    if (lit != l.end())
    {
        light.lockedName = *lit;
    }

    return light;
}

SceneString build_string(nlohmann::json& s)
{
    SceneString string;
    auto        td  = s["translation"];
    auto        rd  = s["rotation"];
    auto        sd  = s["scale"];
    auto        cd  = s["color"];
    string.text     = s["text"];
    string.position = Vector4(td["x"], td["y"], td["z"]);
    string.rotation = Vector4(rd["x"], rd["y"], rd["z"]);
    string.scale    = Vector4(sd["x"], sd["y"], sd["z"]);

    auto                     vit = s.find("vector_path");
    auto                     wit = s.find("waypoint_path");
    std::vector<std::string> vectorPaths;
    std::string              waypointPath;
    if (vit != s.end())
    {
        for (auto& vp : s["vector_path"])
        {
            string.vectorPaths.emplace_back(vp);
        }
    }
    if (wit != s.end())
    {
        string.waypointPath = s["waypoint_path"];
    }

    for (auto& o : s["offsets"])
    {
        string.offsets.push_back(o);
    }

    return string;
}

} // namespace
namespace SceneBuilder
{

std::shared_ptr<EngineScene> parse(const std::string& file, ViewEventDistributor* viewManager,
                                   AudioManager* audioManager)
{
    auto scene = std::make_shared<EngineScene>();
    // Set the engine pointer so addEntity won't crash will a null scene object
    EngineManager::instance()->setEngineScene(scene);
    std::ifstream  input(file);
    nlohmann::json jd;
    input >> jd;

    scene->name        = jd["name"];
    scene->soundFile   = jd["sound_file"];
    scene->skyboxDay   = jd["skybox_day"];
    scene->skyboxNight = jd["skybox_night"];
    scene->fbxScene    = jd["fbx_scene"];

    // Load and compile all models for the model broker
    ModelBroker::instance()->buildModels(scene->fbxScene, viewManager);

    audioManager->loadSoundConfig(scene->soundFile);
    for (auto& e : jd["entities"])
    {
        SceneEntity sceneEntity                = build_entity(e);
        scene->sceneEntities[sceneEntity.name] = sceneEntity;

        Entity* engineEntity = new Entity(sceneEntity, viewManager);
        scene->entityList.push_back(engineEntity);
    }

    for (auto& l : jd["lights"])
    {
        SceneLight sceneLight               = build_light(l);
        scene->sceneLights[sceneLight.name] = sceneLight;
        sceneLight.lockedIdx                = -1;
        if (sceneLight.lockedName != "")
        {
            for (int i = 0; i < scene->entityList.size(); i++)
            {
                if (scene->entityList[i]->getName() == sceneLight.lockedName)
                {
                    sceneLight.lockedIdx = i;
                    break;
                }
            }
        }
        scene->lightList.push_back(new Light(sceneLight));
    }

    for (auto& s : jd["strings"])
    {
        SceneString sceneString               = build_string(s);
        scene->sceneStrings[sceneString.text] = sceneString;

        std::string text   = sceneString.text;
        float       offset = 0.0;

        // Loop through each char in the message and display
        for (int i = 0; i < text.size(); i++)
        {
            std::string charName     = text.substr(i, 1);
            Model*      charToDispay = ModelBroker::instance()->getModel(charName);

            if (charToDispay != nullptr)
            {
                auto& strpos   = sceneString.position;
                auto& strrot   = sceneString.rotation;
                auto& strscale = sceneString.scale;

                auto width = strscale.getx();

                auto translation =
                    Matrix::translation(strpos.getx(), strpos.gety(), strpos.getz()) *
                    Matrix::rotationAroundY(strrot.gety()) *
                    Matrix::translation(offset + sceneString.offsets[i], 0, 0) *
                    Vector4(0, 0, 0, 1);

                offset += width + sceneString.offsets[i];
                SceneEntity tempEntity;
                tempEntity.name = sceneString.text + "_" + std::to_string(offset) + "_" + text[i];
                std::transform(tempEntity.name.begin(), tempEntity.name.end(),
                               tempEntity.name.begin(), ::toupper);
                tempEntity.modelname = text[i];

                tempEntity.position     = translation;
                tempEntity.rotation     = sceneString.rotation;
                tempEntity.scale        = sceneString.scale;
                tempEntity.waypointPath = sceneString.waypointPath;
                tempEntity.vectorPaths  = sceneString.vectorPaths;

                Entity* entity = new Entity(tempEntity, viewManager);
                scene->entityList.push_back(entity);
            }
            else
            {
                offset += sceneString.offsets[i];
            }
        }
    }


    if (jd.find("camera") != jd.end())
    {
        ViewEventDistributor::CameraSettings settings;
        auto&                                camera      = jd["camera"];
        std::string                          string_type = camera["type"];
        std::transform(string_type.begin(), string_type.end(), string_type.begin(), ::toupper);
        if (camera.find("path") != camera.end())
            settings.path = camera["path"];

        auto pc           = camera["position"];
        settings.position = Vector4(pc["x"], pc["y"], pc["z"]);
        auto rc           = camera["rotation"];
        settings.rotation = Vector4(rc["x"], rc["y"], rc["z"]);

        if (camera.find("locked") != camera.end())
        {
            std::string locked = camera["locked"];
            std::transform(locked.begin(), locked.end(), locked.begin(), ::toupper);
            for (int i = 0; i < scene->entityList.size(); i++)
            {
                auto& e = scene->entityList[i];
                if (e->getName() == locked)
                {
                    settings.lockedEntity     = i;
                    settings.lockedEntityName = locked;
                }
            }
        }

        if (camera.find("lock_offset") != camera.end())
        {
            auto& lo            = camera["lock_offset"];
            settings.lockOffset = Vector4(lo["x"], lo["y"], lo["z"]);
        }

        if (string_type == "VECTOR")
        {
            settings.type = ViewEventDistributor::CameraType::VECTOR;
            viewManager->setCamera(settings);
        }
        else if (string_type == "WAYPOINT")
        {
            settings.type = ViewEventDistributor::CameraType::WAYPOINT;
            viewManager->setCamera(settings);
        }
        scene->cameraSettings = settings;
    }

    return scene;
}

void saveScene(const std::string& fileName, std::shared_ptr<EngineScene> scene)
{
    std::ofstream               o(fileName);
    std::vector<nlohmann::json> entities;
    for (auto& e : scene->sceneEntities)
    {
        auto&          entity = e.second;
        nlohmann::json j      = {
            {"entity_name", entity.name},
            {"model_name", entity.modelname},
            {"rotation",
             {{"x", entity.rotation.getx()},
              {"y", entity.rotation.gety()},
              {"z", entity.rotation.getz()}}},
            {"translation",
             {{"x", entity.position.getx()},
              {"y", entity.position.gety()},
              {"z", entity.position.getz()}}},
            {"scale",
             {{"x", entity.scale.getx()}, {"y", entity.scale.gety()}, {"z", entity.scale.getz()}}},
            {"waypoint_path", entity.waypointPath},
            {"vector_path", entity.vectorPaths}};
        entities.push_back(j);
    }

    std::vector<nlohmann::json> lights;
    for (auto& l : scene->sceneLights)
    {
        auto&       light = l.second;
        std::string lightType;
        std::string effectType;
        if (light.lightType == LightType::DIRECTIONAL)
        {
            lightType = "DIRECTIONAL";
        }
        else if (light.lightType == LightType::SHADOWED_DIRECTIONAL)
        {
            lightType = "SHADOWED_DIRECTIONAL";
        }
        else if (light.lightType == LightType::POINT)
        {
            lightType = "POINT";
        }
        else if (light.lightType == LightType::SHADOWED_POINT)
        {
            lightType = "SHADOWED_POINT";
        }
        else if (light.lightType == LightType::SPOTLIGHT)
        {
            lightType = "SPOTLIGHT";
        }
        else if (light.lightType == LightType::SHADOWED_SPOTLIGHT)
        {
            lightType = "SHADOWED_SPOTLIGHT";
        }

        nlohmann::json j = {
            {"light_name", light.name},
            {"light_type", lightType},
            {"effect_type", effectType},
            {"rotation",
             {{"x", light.rotation.getx()},
              {"y", light.rotation.gety()},
              {"z", light.rotation.getz()}}},
            {"translation",
             {{"x", light.position.getx()},
              {"y", light.position.gety()},
              {"z", light.position.getz()}}},
            {"scale",
             {{"x", light.scale.getx()}, {"y", light.scale.gety()}, {"z", light.scale.getz()}}},
            {"color",
             {{"r", light.color.getx()}, {"g", light.color.gety()}, {"b", light.color.getz()}}},
            {"vector_path", light.vectorPath},
            {"waypoint_path", light.waypointPath},
            {"color_path", light.colorPath},
            {"locked", light.lockedName}};
        lights.push_back(j);
    }

    std::vector<nlohmann::json> strings;
    for (auto& s : scene->sceneStrings)
    {
        auto&          string = s.second;
        nlohmann::json j      = {
            {"text", string.text},
            {"rotation",
             {{"x", string.rotation.getx()},
              {"y", string.rotation.gety()},
              {"z", string.rotation.getz()}}},
            {"translation",
             {{"x", string.position.getx()},
              {"y", string.position.gety()},
              {"z", string.position.getz()}}},
            {"scale",
             {{"x", string.scale.getx()}, {"y", string.scale.gety()}, {"z", string.scale.getz()}}},
            {"color",
             {{"r", string.color.getx()}, {"g", string.color.gety()}, {"b", string.color.getz()}}},
            {"waypoint_path", string.waypointPath},
            {"vector_path", string.vectorPaths}};
        strings.push_back(j);
    }

    std::string type;
    if (scene->cameraSettings.type == ViewEventDistributor::CameraType::GOD)
    {
        type = "god";
    }
    else if (scene->cameraSettings.type == ViewEventDistributor::CameraType::VECTOR)
    {
        type = "vector";
    }
    else if (scene->cameraSettings.type == ViewEventDistributor::CameraType::WAYPOINT)
    {
        type = "waypoint";
    }

    nlohmann::json j = {
        {"name", scene->name},
        {"camera",
         {{"type", type},
          {"locked", scene->cameraSettings.lockedEntityName},
          {"bobble", scene->cameraSettings.bobble},
          {"lock_offset",
           {{"x", scene->cameraSettings.lockOffset.getx()},
            {"y", scene->cameraSettings.lockOffset.gety()},
            {"z", scene->cameraSettings.lockOffset.getz()}}},
          {"path", scene->cameraSettings.path},
          {"position",
           {{"x", scene->cameraSettings.position.getx()},
            {"y", scene->cameraSettings.position.gety()},
            {"z", scene->cameraSettings.position.getz()}}},
          {"rotation",
           {{"x", scene->cameraSettings.rotation.getx()},
            {"y", scene->cameraSettings.rotation.gety()},
            {"z", scene->cameraSettings.rotation.getz()}}}

         }},
        {"sound_file", scene->soundFile},
        {"skybox_day", scene->skyboxDay},
        {"skybox_night", scene->skyboxNight},
        {"fbx_scene", scene->fbxScene},
        {"entities", entities},
        {"lights", lights},
        {"strings", strings},
    };

    o << std::setw(4) << j << std::endl;
}

} // namespace SceneBuilder
