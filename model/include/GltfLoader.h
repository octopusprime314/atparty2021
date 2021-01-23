/*
 * GltfLoader is part of the ReBoot distribution (https://github.com/octopusprime314/ReBoot.git).
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
 *  GltfLoader class. Loads and converts Gltf data into ReBoot engine format
 */

#pragma once
#include "Matrix.h"
#include "Tex2.h"
#include <map>
#include <string>
#include <vector>

class Model;
class AnimatedModel;
class SkinningData;
class Vector4;
class Entity;
class RenderBuffers;

enum class ModelLoadType
{
    SingleModel,
    Collection,
    Scene
};

class GltfLoader
{
    std::string _fileName;

  public:
    GltfLoader(std::string name);
    ~GltfLoader();

    void buildModels(Model* model, ModelLoadType loadType);
};