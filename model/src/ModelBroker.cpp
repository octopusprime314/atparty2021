#include "ModelBroker.h"
#include "Dirent.h"
#include "Logger.h"
#include "Model.h"
#include "ViewEventDistributor.h"
#include <algorithm>
#include <cctype>
#include "ShaderBroker.h"
#include "TextureBroker.h"
#include "DXLayer.h"
#include "IOConstants.h"

ModelBroker*          ModelBroker::_broker      = nullptr;
ViewEventDistributor* ModelBroker::_viewManager = nullptr;

ModelBroker* ModelBroker::instance()
{
    if (_broker == nullptr)
    {
        _broker = new ModelBroker();
    }
    return _broker;
}
ModelBroker::ModelBroker() {}
ModelBroker::~ModelBroker() {}

// helper function to capitalize everything
std::string ModelBroker::_strToUpper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    return s;
}

// helper function to capitalize everything
std::string ModelBroker::_strToLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

ViewEventDistributor* ModelBroker::getViewManager() { return _viewManager; }

ModelMap ModelBroker::getModels()
{
    return _models;
}

void ModelBroker::buildModels(const std::string&    fbxScene,
                              ViewEventDistributor* viewEventDistributor)
{
    _viewManager = viewEventDistributor;
    _gatherModelNames(fbxScene);
}

Model* ModelBroker::getModel(std::string modelName)
{
    std::string upperCaseMapName = _strToUpper(modelName);
    if (_models.find(upperCaseMapName + "_LOD1") != _models.end())
    {
        return _models[upperCaseMapName + "_LOD1"];
    }
    else
    {
        return _models[upperCaseMapName];
    }
}

Model* ModelBroker::getModel(std::string modelName, Vector4 pos)
{
    std::string upperCaseMapName = _strToUpper(modelName);
    // If we have an lod marker from the model folder then choose which one
    if (_models.find(upperCaseMapName) != _models.end() &&
        upperCaseMapName.find("LOD") != std::string::npos)
    {

        Vector4 cameraPos = getViewManager()->getCameraPos();
        float   distance  = (pos + cameraPos).getMagnitude();
        // use lod 1 which is the highest poly count for the model
        // if (distance < 1000 || EngineState::getEngineState().worldEditorModeEnabled)
        //{
        return _models[upperCaseMapName];
        /*}
        else
        {
            std::string lod = upperCaseMapName.substr(0, upperCaseMapName.size() - 1);
            if (_models.find(lod + "2") != _models.end())
            {
                return _models[lod + "2"];
            }
            if (_models.find(lod + "3") != _models.end())
            {
                return _models[lod + "3"];
            }
            else
            {
                return _models[upperCaseMapName];
            }
        }*/
    }
    else
    {
        return _models[upperCaseMapName];
    }
}

std::vector<std::string> ModelBroker::getModelNames() { return _modelNames; }


void ModelBroker::addCollectionEntry(Model* model)
{
    auto dxLayer = DXLayer::instance();
    dxLayer->lock();
    auto name = _strToUpper(model->getName());
    _models[name] = model;
    _modelNames.push_back(name);
    dxLayer->unlock();
}

void ModelBroker::updateModel(std::string modelName)
{

    std::string upperCaseMapName = _strToUpper(modelName);

    if (_models.find(upperCaseMapName) != _models.end())
    {

        if (_models[upperCaseMapName]->getClassType() == ModelClass::ModelType)
        {
            auto model = new Model(upperCaseMapName);
            _models[upperCaseMapName]->updateModel(model);
            delete model;
        }
    }
    else
    {
        std::cout << "Model doesn't exist so add it!" << std::endl;
    }
}

void ModelBroker::_gatherModelNames(const std::string& fbxScene)
{
    bool           isFile = false;
    DIR*           dir;
    struct dirent* ent;

    int validModels = 0;

    // Alphanumeric 3d models
    if ((dir = opendir(ALPHANUMERIC_MESH_LOCATION.c_str())) != nullptr)
    {
        LOG_INFO("Files to be processed: \n");

        while ((ent = readdir(dir)) != nullptr)
        {
            if (*ent->d_name)
            {
                std::string fileName = std::string(ent->d_name);
                fileName             = fileName.substr(0, fileName.find_first_of("."));

                if (fileName.empty() == false && fileName != "." && fileName != ".." &&
                    fileName.find(".ini") == std::string::npos)
                {
                    _models[_strToUpper(fileName)] =
                        new Model(ALPHANUMERIC_MESH_LOCATION + std::string(ent->d_name));
                    _modelNames.push_back(_strToUpper(fileName));
                    validModels++;
                }
            }
        }
        closedir(dir);
    }
    else
    {
        std::cout << "Problem reading from directory!" << std::endl;
    }

    // Static models
    if ((dir = opendir(STATIC_MESH_LOCATION.c_str())) != nullptr)
    {
        LOG_INFO("Files to be processed: \n");

        while ((ent = readdir(dir)) != nullptr)
        {
            if (*ent->d_name)
            {
                std::string fileName = std::string(ent->d_name);

                if (fileName.empty() == false && fileName != "." && fileName != ".." &&
                    fileName.find(".ini") == std::string::npos)
                {

                    DIR*           subDir;
                    struct dirent* subEnt;
                    if ((subDir = opendir((STATIC_MESH_LOCATION + fileName).c_str())) != nullptr)
                    {
                        while ((subEnt = readdir(subDir)) != nullptr)
                        {

                            if (*subEnt->d_name)
                            {
                                LOG_INFO(subEnt->d_name, "\n");

                                std::string lodFile = std::string(subEnt->d_name);
                                if (lodFile.find("lod") != std::string::npos &&
                                    lodFile.find(".bin") == std::string::npos)
                                {
                                    std::string mapName = fileName + "/" + lodFile;
                                    lodFile = lodFile.substr(0, lodFile.find_last_of("."));
                                    _models[_strToUpper(lodFile)] =
                                        new Model(STATIC_MESH_LOCATION + mapName);
                                    _modelNames.push_back(_strToUpper(lodFile));
                                    validModels++;
                                }
                            }
                        }
                    }
                }
            }
        }
        closedir(dir);
    }
    else
    {
        std::cout << "Problem reading from directory!" << std::endl;
    }

    std::vector<Model*> collectionMasterModels;
    // Collection models
    if ((dir = opendir(COLLECTIONS_MESH_LOCATION.c_str())) != nullptr)
    {
        LOG_INFO("Files to be processed: \n");

        while ((ent = readdir(dir)) != nullptr)
        {
            if (*ent->d_name)
            {
                std::string fileName = std::string(ent->d_name);

                if (fileName.empty() == false && fileName != "." && fileName != ".." &&
                    fileName.find(".ini") == std::string::npos)
                {

                    DIR*           subDir;
                    struct dirent* subEnt;
                    if ((subDir = opendir((COLLECTIONS_MESH_LOCATION + fileName).c_str())) != nullptr)
                    {
                        while ((subEnt = readdir(subDir)) != nullptr)
                        {

                            if (*subEnt->d_name)
                            {
                                LOG_INFO(subEnt->d_name, "\n");

                                std::string lodFile = std::string(subEnt->d_name);
                                if (lodFile.find("lod") != std::string::npos &&
                                    lodFile.find(".bin") == std::string::npos)
                                {
                                    std::string mapName = fileName + "/" + lodFile;
                                    lodFile = lodFile.substr(0, lodFile.find_last_of("."));

                                    Model* model = new Model(COLLECTIONS_MESH_LOCATION + mapName);
                                    collectionMasterModels.push_back(model);
                                }
                            }
                        }
                    }
                }
            }
        }
        closedir(dir);
    }
    else
    {
        std::cout << "Problem reading from directory!" << std::endl;
    }

    // Single scene model that creates entities (instances) using all the previously loaded models
    if ((dir = opendir(SCENE_MESH_LOCATION.c_str())) != nullptr)
    {
        LOG_INFO("Files to be processed: \n");

        while ((ent = readdir(dir)) != nullptr)
        {
            if (*ent->d_name)
            {
                std::string fileName = std::string(ent->d_name);

                if (fileName.empty() == false && fileName != "." && fileName != ".." &&
                    fileName.find(".ini") == std::string::npos)
                {
                    DIR*           subDir;
                    struct dirent* subEnt;
                    if ((subDir = opendir((SCENE_MESH_LOCATION + fileName).c_str())) != nullptr)
                    {
                        while ((subEnt = readdir(subDir)) != nullptr)
                        {

                            if (*subEnt->d_name)
                            {
                                LOG_INFO(subEnt->d_name, "\n");

                                std::string lodFile = std::string(subEnt->d_name);
                                if (lodFile == fbxScene)
                                {
                                    std::string mapName = fileName + "/" + lodFile;
                                    lodFile = lodFile.substr(0, lodFile.find_last_of("."));

                                    Model* model = new Model(SCENE_MESH_LOCATION + mapName);
                                    collectionMasterModels.push_back(model);
                                }
                            }
                        }
                    }
                }
            }
        }
        closedir(dir);
    }
    else
    {
        std::cout << "Problem reading from directory!" << std::endl;
    }



    for (auto model : collectionMasterModels)
    {
        if (model != nullptr)
        {
            while (model->getLoadModelCount() == 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(0));
            }
            validModels += model->getLoadModelCount();
        }
    }

    // Wait for all models to signal finish loading
    while (true)
    {
        int modelsLoaded = 0;

        for (auto model : _models)
        {
            if (model.second != nullptr && model.second->isModelLoaded())
            {
                modelsLoaded++;
            }
        }

        if (validModels == modelsLoaded)
        {
            break;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(0));
    }

    // Make sure all the textures have finished loading as well
    auto texBroker = TextureBroker::instance();
    while (texBroker->areTexturesUploaded() == false)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(0));
    }
}