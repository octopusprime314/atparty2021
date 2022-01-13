#include "Model.h"
#include "IOEventDistributor.h"
#include "ModelBroker.h"
#include "ShaderBroker.h"

TextureBroker* Model::_textureManager = TextureBroker::instance();
unsigned int   Model::_modelIdTagger  = 0;

Model::Model(std::string name, ModelClass classId)
    : _isInstanced(false), _classId(classId), _name(name.substr(name.find_last_of("/") + 1)),
      _modelId(0), _gltfLoader(nullptr)
{
    _modelCountToLoad = 0;
    _isLoaded = false;

    // trim extension off
    //_name = _name.substr(0, _name.find_last_of("."));
    _vao.push_back(new VAO());

    std::string postScript = name.substr(name.find("_"));
    if (postScript.find("collection") != std::string::npos)
    {
        return;
    }

    _gltfLoader = new GltfLoader(name);

    // Scenes and collections contain instances of geoemetry
    if (name.find("scene") != std::string::npos ||
        name.find("collection") != std::string::npos)
    {
        ModelLoadType loadType = (name.find("scene") != std::string::npos) ? ModelLoadType::Scene :
                                                                             ModelLoadType::Collection;

        auto thread = new std::thread(&GltfLoader::buildModels, _gltfLoader, this, loadType);
        thread->detach();
        _isLoaded = true;
    }
    else
    {
        auto thread = new std::thread(&GltfLoader::buildModels, _gltfLoader, this, ModelLoadType::SingleModel);
        thread->detach();
    }

    _vao.back()->setPrimitiveOffsetId(0);

    _modelId = _modelIdTagger++;
}

Model::~Model() {}

bool Model::isModelLoaded()
{
    return _isLoaded;
}

void Model::runShader(Entity* entity)
{
    std::lock_guard<std::mutex> lockGuard(_updateLock);
    //_shaderProgram->runShader(entity);
}

void Model::addVAO(ModelClass classType)
{
    _vao[_vao.size() - 1]->createVAO(&_renderBuffers, classType);
    _vao.back()->setPrimitiveOffsetId(0);
}

void Model::updateModel(Model* model)
{
    std::lock_guard<std::mutex> lockGuard(_updateLock);
    this->_vao      = model->_vao;
}

std::vector<VAO*>* Model::getVAO() { return &_vao; }

unsigned int Model::getId() { return _modelId; }

ModelClass Model::getClassType() { return _classId; }

std::string Model::getName() { return _name; }

size_t Model::getArrayCount()
{
    if (_classId == ModelClass::AnimatedModelType)
    {
        return _renderBuffers.getIndices()->size();
    }
    else if (_classId == ModelClass::ModelType)
    {
        return _renderBuffers.getVertices()->size();
    }
    else
    {
        std::cout << "What class is this????" << std::endl;
        return 0;
    }
}

std::vector<std::string> Model::getTextureNames() { return _textureRecorder; }
std::vector<Material> Model::getMaterialNames() { return _materialRecorder; }
Material Model::getMaterial(int index) { return _materialRecorder[index]; }

void Model::addTexture(std::string textureName, int textureStride, int vertexStride, int indexStride)
{
    _vao[_vao.size() - 1]->addTextureStride(std::pair<std::string, int>(textureName, textureStride),
                                            vertexStride, indexStride);

    auto thread = new std::thread(&TextureBroker::addTexture, _textureManager, textureName, nullptr);
    thread->detach();

    _textureRecorder.push_back(textureName);
}

void Model::addMaterial(std::vector<std::string> materialTextures, int textureStride,
                        int vertexStride, int indexStride, UniformMaterial uniformMaterial)
{
    struct Material material;
    std::string amalgamatedTextureName = "";
    int i = 0;
    // texture strings contain std::string albedo, std::string normal, std::string roughnessMetallic
    for (auto materialTextureName : materialTextures)
    {
        auto thread =
            new std::thread(&TextureBroker::addTexture, _textureManager, materialTextureName, nullptr);
        thread->detach();

        if (i == 0)
        {
            material.albedo = materialTextureName;
        }
        else if (i == 1)
        {
            material.normal = materialTextureName;
        }
        else if (i == 2)
        {
            material.roughnessMetallic = materialTextureName;
        }
        else if (i == 3)
        {
            material.emissive = materialTextureName;
        }

        amalgamatedTextureName += materialTextureName;
        i++;
    }

    _vao[_vao.size() - 1]->addTextureStride(std::pair<std::string, int>(amalgamatedTextureName, textureStride),
                                            vertexStride, indexStride);

    material.uniformMaterial = uniformMaterial;
    _materialRecorder.push_back(material);
}

void Model::addLayeredTexture(std::vector<std::string> textureNames, int stride)
{

    // Encode layered into string to notify shader what type of texture is used
    std::string sumString = "Layered";
    for (auto& str : textureNames)
    {
        sumString += str;
        _textureRecorder.push_back(str);
    }
    // Create sum string for later identification
    _vao[_vao.size() - 1]->addTextureStride(std::pair<std::string, int>(sumString, stride), 0, 0);
    _textureManager->addLayeredTexture(textureNames);
}

AssetTexture* Model::getTexture(std::string textureName)
{
    return _textureManager->getTexture(textureName);
}

LayeredTexture* Model::getLayeredTexture(std::string textureName)
{
    return _textureManager->getLayeredTexture(textureName);
}

std::string Model::_getModelName(std::string name)
{
    std::string modelName = name;
    modelName             = modelName.substr(0, modelName.find_first_of("/"));
    return modelName;
}

RenderBuffers* Model::getRenderBuffers() { return &_renderBuffers; }

void Model::setInstances(std::vector<Vector4> offsets)
{
    _isInstanced = true;
    int i        = 0;
    for (auto& offset : offsets)
    {
        _offsets[i++] = offset.getx();
        _offsets[i++] = offset.gety();
        _offsets[i++] = offset.getz();
    }
    _instances = static_cast<int>(offsets.size());
}

bool Model::getIsInstancedModel() { return _isInstanced; }
float* Model::getInstanceOffsets() { return _offsets; }
GltfLoader* Model::getGltfLoader() { return _gltfLoader; }
