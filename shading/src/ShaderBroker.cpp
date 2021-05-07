#include "ShaderBroker.h"
#include "ComputeShader.h"
#include "Dirent.h"
#include "EngineManager.h"
#include "Logger.h"
#include "PathTracerShader.h"
#include "StaticShader.h"
#include "EffectShader.h"
#include "DeferredShader.h"
#include <algorithm>
#include <cctype>

ShaderBroker* ShaderBroker::_broker = nullptr;

// Only initializes the static pointer once
ShaderBroker* ShaderBroker::instance()
{
    if (_broker == nullptr)
    {
        _broker = new ShaderBroker();
    }
    return _broker;
}
ShaderBroker::ShaderBroker() { _finishedCompiling = false; }
ShaderBroker::~ShaderBroker() {}

bool ShaderBroker::isFinishedCompiling() 
{
    return _finishedCompiling;
}

// helper function to capitalize everything
std::string ShaderBroker::_strToUpper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::toupper(c); });
    return s;
}

void ShaderBroker::compileShaders() { _gatherShaderNames(); }

ShaderBase* ShaderBroker::getShader(std::string shaderName)
{
    std::string upperCaseMapName = _strToUpper(shaderName);

    for (auto tuples : _shaders)
    {
        if (tuples.first.find(upperCaseMapName) != std::string::npos)
        {
            return tuples.second;
        }
    }
    return nullptr;
}

void ShaderBroker::recompileShader(std::string shaderName)
{
    std::string upperCaseMapName = _strToUpper(shaderName);
    if (_shaders.find(upperCaseMapName) != _shaders.end())
    {

        // auto shader = new Shader(upperCaseMapName);
        // Release previous shader
        // glDeleteShader(_shaders[upperCaseMapName]->getShaderContext());
        //_shaders[upperCaseMapName]->updateShader(shader);
    }
    else
    {
        std::cout << "Shader doesn't exist so we can't recompile it!" << std::endl;
    }
}

void ShaderBroker::_gatherShaderNames()
{
    DIR*           dir;
    struct dirent* ent;
    std::string    shadersLocation = SHADERS_LOCATION;
    bool           isFile          = false;

    // VS, PS, CS for now
    std::string shaderTypes[] = {"cs/", "vs/"};
    int shaderTypeCount = sizeof(shaderTypes) / sizeof(std::string);

    for (int i = 0; i < shaderTypeCount; i++)
    {
        shadersLocation = SHADERS_LOCATION + "hlsl/" + shaderTypes[i];

        if ((dir = opendir(shadersLocation.c_str())) != nullptr)
        {
            LOG_INFO("Files to be processed: \n");

            while ((ent = readdir(dir)) != nullptr)
            {
                if (*ent->d_name)
                {
                    std::string fileName = std::string(ent->d_name);

                    if (!fileName.empty() && fileName != "." && fileName != ".." &&
                        fileName.find(".") != std::string::npos &&
                        fileName.find(".ini") == std::string::npos &&
                        fileName.find(".hlsl") != std::string::npos)
                    {

                        LOG_INFO(ent->d_name, "\n");

                        std::string mapName = shadersLocation + fileName.substr(0, fileName.find("."));
                        std::string upperCaseMapName = _strToUpper(mapName);

                        if (mapName.find("add"                     ) != std::string::npos ||
                            mapName.find("blurHorizontalShaderRGB" ) != std::string::npos ||
                            mapName.find("blurShader"              ) != std::string::npos ||
                            mapName.find("blurVerticalShaderRGB"   ) != std::string::npos ||
                            mapName.find("downsample"              ) != std::string::npos ||
                            mapName.find("downsampleRGB"           ) != std::string::npos ||
                            mapName.find("highLuminanceFilter"     ) != std::string::npos ||
                            mapName.find("motionBlur"              ) != std::string::npos ||
                            mapName.find("upsample"                ) != std::string::npos ||
                            mapName.find("upsampleRGB"             ) != std::string::npos ||
                            mapName.find("mipGen"                  ) != std::string::npos)
                        {
                            _shaders[upperCaseMapName] = new ComputeShader(mapName);
                        }
                        else if (mapName.find("staticShader") != std::string::npos)
                        {
                            _shaders[upperCaseMapName] = new StaticShader(mapName);
                        }
                        else if (mapName.find("deferredShader") != std::string::npos)
                        {
                            _shaders[upperCaseMapName] = new DeferredShader(mapName);
                        }
                        else if (mapName.find("fireShader") != std::string::npos)
                        {
                            _shaders[upperCaseMapName] = new EffectShader(mapName);
                        }
                        else if (mapName.find("pathTracerShader") != std::string::npos)
                        {
                            _shaders[upperCaseMapName] = new PathTracerShader(mapName);
                        }
                        else
                        {
                            std::cout << "Shader class " << mapName << " not registered!" << std::endl;
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
    }

    if (EngineManager::getGraphicsLayer() != GraphicsLayer::DX12)
    {
        // Always compile this collection of path tracer shaders
        _shaders["../../SHADING/SHADERS/HLSL/CS/PATHTRACERSHADERCS"] = new PathTracerShader("PATHTRACERSHADER");
    }

    _finishedCompiling = true;
}
