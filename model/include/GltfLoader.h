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
    using TileTextures   = std::map<std::string, std::vector<std::string>>;
    using ClonedCount    = std::map<std::string, unsigned int>;
    using ClonedMatrices = std::map<std::string, Matrix>;
    using TextureStrides = std::vector<std::pair<int, int>>;

    ClonedMatrices  _clonedWorldTransforms;
    Matrix          _objectSpaceTransform;
    ClonedCount     _clonedInstances;
    bool            _copiedOverFlag;
    TileTextures    _tileTextures;
    int             _strideIndex;
    std::string     _fileName;

    std::vector<float>    _vertices;
    std::vector<float>    _normals;
    std::vector<float>    _textures;
    std::vector<uint32_t> _indices;

  public:
    GltfLoader(std::string name);
    ~GltfLoader();

    void buildTriangles(Model* model);
    void buildCollection(Model* model, ModelLoadType loadType);

    std::string getModelName();
    Matrix      getObjectSpaceTransform();
};