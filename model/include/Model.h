/*
 * Model is part of the ReBoot distribution (https://github.com/octopusprime314/ReBoot.git).
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
 *  Model class. Contains vertex and normal data which will get populated in opengl
 *  buffers to be rendered to the screen
 */

#pragma once
#include "GltfLoader.h"
#include "MVP.h"
#include "MasterClock.h"
#include "Matrix.h"
#include "RenderBuffers.h"
#include "StateVector.h"
#include "Tex2.h"
#include "TextureBroker.h"
#include "VAO.h"
#include <iostream>
#include <mutex>
#include <vector>
class FrustumCuller;
class IOEventDistributor;

enum class ModelClass
{
    ModelType = 0,
    AnimatedModelType
};

const uint32_t ColorValidBit     = 1;
const uint32_t NormalValidBit    = 2;
const uint32_t RoughnessValidBit = 4;
const uint32_t MetallicValidBit  = 8;
const uint32_t EmissiveValidBit  = 16;

struct UniformMaterial
{
    float baseColor[3];
    float metallic;
    float roughness;
    float transmittance;
    float emissiveColor[3];
    uint32_t validBits;
};

static constexpr int TexturesPerMaterial = 4;
struct Material
{
    std::string albedo;
    std::string normal;
    std::string roughnessMetallic;
    std::string emissive;

    UniformMaterial uniformMaterial;
};

class Model
{

  public:
    // Default model to type to base class
    Model(std::string name, ModelClass classId = ModelClass::ModelType);

    virtual ~Model();
    void                     addLayeredTexture(std::vector<std::string> textureNames, int stride);
    void                     addTexture(std::string textureName, int textureStride, int vertexStride, int indexStride);
    void                     addMaterial(std::vector<std::string> materialTextures, int textureStride,
                                         int vertexStride, int indexStride, UniformMaterial uniformMaterial);
    void                     setInstances(std::vector<Vector4> offsets);
    LayeredTexture*          getLayeredTexture(std::string textureName);
    AssetTexture*            getTexture(std::string textureName);
    void                     addVAO(ModelClass classType);
    void                     runShader(Entity* entity);
    virtual void             updateModel(Model* model);
    bool                     getIsInstancedModel();
    float*                   getInstanceOffsets();
    RenderBuffers*           getRenderBuffers();
    std::vector<std::string> getTextureNames();
    std::vector<Material>    getMaterialNames();
    Material                 getMaterial(int index);
    bool                     isModelLoaded();
    size_t                   getArrayCount();
    ModelClass               getClassType();
    std::string              getName();
    std::vector<VAO*>*       getVAO();
    unsigned int             getId();
    GltfLoader*              getGltfLoader();
    void setLoadModelCount(int modelCountToLoad) { _modelCountToLoad = modelCountToLoad; }
    int  getLoadModelCount() { return _modelCountToLoad; }

    bool _isLoaded;

  protected:
    std::string _getModelName(std::string name);

    std::vector<Material>    _materialRecorder;
    std::vector<std::string> _textureRecorder;
    // Static texture manager for texture reuse purposes, all models have access
    static TextureBroker* _textureManager;
    static unsigned int   _modelIdTagger;
    // Manages vertex, normal and texture data
    RenderBuffers _renderBuffers;
    // Indicates whether the collision geometry is sphere or triangle based
    // 300 x, y and z offsets
    float      _offsets[900];
    bool       _isInstanced;
    std::mutex _updateLock;
    int        _modelCountToLoad;

    int         _instances;
    GltfLoader* _gltfLoader;
    ModelClass  _classId;
    // used to identify model, used for ray tracing
    unsigned int _modelId;
    std::string  _name;
    // Vao container
    std::vector<VAO*> _vao;
};