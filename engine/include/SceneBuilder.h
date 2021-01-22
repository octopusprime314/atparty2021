#pragma once

#include "EngineScene.h"
#include <memory>
#include <string>

class ViewEventDistributor;
class AudioManager;

namespace SceneBuilder
{
std::shared_ptr<EngineScene> parse(const std::string& file, ViewEventDistributor* viewWrapper,
                                   AudioManager* audioManager);

void saveScene(const std::string& fileName, std::shared_ptr<EngineScene> scene);
} // namespace SceneBuilder