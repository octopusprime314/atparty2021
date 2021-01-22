#include "GltfLoader.h"
#include "EngineManager.h"
#include "Entity.h"
#include "Model.h"
#include "ModelBroker.h"
#include "RenderBuffers.h"
#include "Texture.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include "ModelBroker.h"
#include "ViewEventDistributor.h"

#undef max

#include <GLTFSDK/GLTF.h>
#include <GLTFSDK/GLTFResourceReader.h>
#include <GLTFSDK/GLBResourceReader.h>
#include <GLTFSDK/Deserialize.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>

#include <cassert>
#include <cstdlib>

using namespace Microsoft::glTF;

struct TransformationMatrices
{
    Matrix rotation;
    Matrix translation;
    Matrix scale;
};

// The glTF SDK is decoupled from all file I/O by the IStreamReader (and IStreamWriter)
// interface(s) and the C++ stream-based I/O library. This allows the glTF SDK to be used in
// sandboxed environments, such as WebAssembly modules and UWP apps, where any file I/O code
// must be platform or use-case specific.
class StreamReader : public IStreamReader
{
public:
    StreamReader(std::filesystem::path pathBase) : m_pathBase(std::move(pathBase))
    {
        assert(m_pathBase.has_root_path());
    }

    // Resolves the relative URIs of any external resources declared in the glTF manifest
    std::shared_ptr<std::istream> GetInputStream(const std::string& filename) const override
    {
        // In order to construct a valid stream:
        // 1. The filename argument will be encoded as UTF-8 so use filesystem::u8path to
        //    correctly construct a path instance.
        // 2. Generate an absolute path by concatenating m_pathBase with the specified filename
        //    path. The filesystem::operator/ uses the platform's preferred directory separator
        //    if appropriate.
        // 3. Always open the file stream in binary mode. The glTF SDK will handle any text
        //    encoding issues for us.
        auto streamPath = m_pathBase / std::filesystem::u8path(filename);
        auto stream = std::make_shared<std::ifstream>(streamPath, std::ios_base::binary);

        // Check if the stream has no errors and is ready for I/O operations
        if (!stream || !(*stream))
        {
            throw std::runtime_error("Unable to create a valid input stream for uri: " + filename);
        }

        return stream;
    }

private:
    std::filesystem::path m_pathBase;
};

// Uses the Document class to print some basic information about various top-level glTF entities
void PrintDocumentInfo(const Document* document)
{
    // Asset Info
    std::cout << "Asset Version:    " << document->asset.version << "\n";
    std::cout << "Asset MinVersion: " << document->asset.minVersion << "\n";
    std::cout << "Asset Generator:  " << document->asset.generator << "\n";
    std::cout << "Asset Copyright:  " << document->asset.copyright << "\n\n";

    // Scene Info
    std::cout << "Scene Count: " << document->scenes.Size() << "\n";

    if (document->scenes.Size() > 0U)
    {
        std::cout << "Default Scene Index: " << document->GetDefaultScene().id << "\n\n";
    }
    else
    {
        std::cout << "\n";
    }

    // Entity Info
    std::cout << "Node Count:     " << document->nodes.Size() << "\n";
    std::cout << "Camera Count:   " << document->cameras.Size() << "\n";
    std::cout << "Material Count: " << document->materials.Size() << "\n\n";

    // Mesh Info
    std::cout << "Mesh Count: " << document->meshes.Size() << "\n";
    std::cout << "Skin Count: " << document->skins.Size() << "\n\n";

    // Texture Info
    std::cout << "Image Count:   " << document->images.Size() << "\n";
    std::cout << "Texture Count: " << document->textures.Size() << "\n";
    std::cout << "Sampler Count: " << document->samplers.Size() << "\n\n";

    // Buffer Info
    std::cout << "Buffer Count:     " << document->buffers.Size() << "\n";
    std::cout << "BufferView Count: " << document->bufferViews.Size() << "\n";
    std::cout << "Accessor Count:   " << document->accessors.Size() << "\n\n";

    // Animation Info
    std::cout << "Animation Count: " << document->animations.Size() << "\n\n";

    for (const auto& extension : document->extensionsUsed)
    {
        std::cout << "Extension Used: " << extension << "\n";
    }

    if (!document->extensionsUsed.empty())
    {
        std::cout << "\n";
    }

    for (const auto& extension : document->extensionsRequired)
    {
        std::cout << "Extension Required: " << extension << "\n";
    }

    if (!document->extensionsRequired.empty())
    {
        std::cout << "\n";
    }
}

void buildModel(std::vector<float>& vertices, std::vector<float>& normals,
                std::vector<float>& textures, std::vector<uint32_t>& indices, Model* model)
{

    size_t totalVerts = vertices.size();

    RenderBuffers* renderBuffers = model->getRenderBuffers();
    renderBuffers->reserveBuffers(totalVerts);

    int vertexAndNormalIndex = 0;
    int textureIndex         = 0;
    // Read each triangle vertex indices and store them in triangle array
    for (int i = 0; i < totalVerts; i += 3)
    {
        int indexA    = vertexAndNormalIndex;
        int indexTexA = textureIndex;

        Vector4 A(vertices[indexA], vertices[indexA + 1], vertices[indexA + 2], 1.0);
        // Scale then rotate vertex
        renderBuffers->addVertex(A);

        Vector4 AN(normals[indexA], normals[indexA + 1], normals[indexA + 2], 1.0);

        // Scale then rotate normal
        renderBuffers->addNormal(AN);
        Tex2 TexA(textures[indexTexA], textures[indexTexA + 1]);
        renderBuffers->addTexture(TexA);

        vertexAndNormalIndex += 3;
        textureIndex += 2;
    }

    renderBuffers->addVertexIndices(indices);

    (*model->getVAO())[0]->createVAO(renderBuffers, ModelClass::ModelType);
}

float ConvertToDegrees(float radian) 
{
    return (180.0f / PI) * radian;
}

Vector4 QuaternionToEuler(Vector4 q)
{
    Vector4 v = Vector4(0, 0, 0);

    v.getFlatBuffer()[1] = ConvertToDegrees(atan2(2 * q.gety() * q.getw() - 2 * q.getx() * q.getz(),
                                   1 - 2 * pow(q.gety(), 2.0f) - 2 * pow(q.getz(), 2.0f)));

    v.getFlatBuffer()[2] =
        ConvertToDegrees(asin(2 * q.getx() * q.gety() + 2 * q.getz() * q.getw()));

    v.getFlatBuffer()[0] =
        ConvertToDegrees(atan2(2 * q.getx() * q.getw() - 2 * q.gety() * q.getz(),
                                 1 - 2 * pow(q.getx(), 2.0f) - 2 * pow(q.getz(), 2.0f)));

    if (q.getx() * q.gety() + q.getz() * q.getw() == 0.5)
    {
        v.getFlatBuffer()[0] = ConvertToDegrees((float)(2 * atan2(q.getx(), q.getw())));
        v.getFlatBuffer()[1] = 0;
    }

    else if (q.getx() * q.gety() + q.getz() * q.getw() == -0.5)
    {
        v.getFlatBuffer()[0] = ConvertToDegrees((float)(-2 * atan2(q.getx(), q.getw())));
        v.getFlatBuffer()[1] = 0;
    }

    return v;
}

float GetRoll(Vector4 q)
{
    float x = ConvertToDegrees(atan2(2 * q.getx() * q.getw() - 2 * q.gety() * q.getz(),
                                     1 - 2 * pow(q.getx(), 2.0f) - 2 * pow(q.getz(), 2.0f)));

    if (q.getx() * q.gety() + q.getz() * q.getw() == 0.5)
    {
        x = ConvertToDegrees((float)(2 * atan2(q.getx(), q.getw())));
    }

    else if (q.getx() * q.gety() + q.getz() * q.getw() == -0.5)
    {
        x = ConvertToDegrees((float)(-2 * atan2(q.getx(), q.getw())));
    }
    return x;
}

float GetPitch(Vector4 q)
{
    float y = ConvertToDegrees(atan2(2 * q.gety() * q.getw() - 2 * q.getx() * q.getz(),
                                     1 - 2 * pow(q.gety(), 2.0f) - 2 * pow(q.getz(), 2.0f)));
    if (q.getx() * q.gety() + q.getz() * q.getw() == 0.5)
    {
        y = 0;
    }
    else if (q.getx() * q.gety() + q.getz() * q.getw() == -0.5)
    {
        y = 0;
    }
    return y;
}

float GetYaw(Vector4 q)
{
    float z = ConvertToDegrees(asin(2 * q.getx() * q.gety() + 2 * q.getz() * q.getw()));
    return z;
}

// Uses the Document and GLTFResourceReader classes to print information about various glTF binary resources
void PrintResourceInfo(const Document*           document,
                       const GLTFResourceReader* resourceReader,
                       std::vector<float>&       vertices,
                       std::vector<float>&       normals,
                       std::vector<float>&       textures,
                       std::vector<uint32_t>&    indices,
                       Model*                    model,
                       ModelLoadType             loadType,
                       std::string               pathFile)
{

    std::vector<Model*> modelsPending;
    if (loadType == ModelLoadType::Collection || loadType == ModelLoadType::Scene)
    {
        Model* saveMaster = model;
        for (int meshIndex = 0; meshIndex < document->meshes.Elements().size(); meshIndex++)
        {
            std::string strippedLod = pathFile.substr(0, pathFile.find("_"));
            std::string strippedExtension = pathFile.substr(0, pathFile.find("."));
            std::string name = COLLECTIONS_MESH_LOCATION + strippedLod + "/" + strippedExtension +
                               std::to_string(meshIndex) + "collection";
            model = new Model(name);
            ModelBroker::instance()->addCollectionEntry(model);

            modelsPending.push_back(model);
        }
        saveMaster->setLoadModelCount(modelsPending.size());
    }

    int modelIndex = 0;
    while (modelIndex < document->meshes.Elements().size())
    {
        // Offsets are in vertices
        std::vector<int> vertexStrides;
        std::vector<int> indexStrides;

        if (loadType == ModelLoadType::Collection || loadType == ModelLoadType::Scene)
        {
            vertices.clear();
            normals.clear();
            textures.clear();
            indices.clear();
        }

        bool             is32BitIndices = false;
        std::vector<int> materialIndices;
        // Use the resource reader to get each mesh primitive's position data
        for (int meshIndex = modelIndex; meshIndex < document->meshes.Elements().size(); meshIndex++)
        {
            const auto& mesh = document->meshes.Elements()[meshIndex];

            std::cout << "Mesh: " << mesh.id << "\n";

            for (int meshPrimIndex = 0; meshPrimIndex < mesh.primitives.size(); meshPrimIndex++)
            {
                const auto& meshPrimitive = mesh.primitives[meshPrimIndex];

                std::string accessorId;

                if (meshPrimitive.TryGetAttributeAccessorId(ACCESSOR_POSITION, accessorId))
                {
                    vertexStrides.push_back(vertices.size() / 3);

                    const Accessor& accessor = document->accessors.Get(accessorId);

                    auto tempVerts = resourceReader->ReadBinaryData<float>(*document, accessor);
                    vertices.insert(vertices.end(), tempVerts.begin(), tempVerts.end());
                    const auto dataByteLength = vertices.size() * sizeof(float);

                    std::cout << "MeshPrimitive: " << dataByteLength << " bytes of position data\n";
                }
                if (meshPrimitive.TryGetAttributeAccessorId(ACCESSOR_NORMAL, accessorId))
                {
                    const Accessor& accessor = document->accessors.Get(accessorId);

                    auto tempNormals = resourceReader->ReadBinaryData<float>(*document, accessor);
                    normals.insert(normals.end(), tempNormals.begin(), tempNormals.end());
                    const auto dataByteLength = normals.size() * sizeof(float);

                    std::cout << "MeshNormals: " << dataByteLength << " bytes of normal data\n";
                }
                if (meshPrimitive.TryGetAttributeAccessorId(ACCESSOR_TEXCOORD_0, accessorId))
                {
                    const Accessor& accessor = document->accessors.Get(accessorId);

                    auto tempTextures = resourceReader->ReadBinaryData<float>(*document, accessor);
                    textures.insert(textures.end(), tempTextures.begin(), tempTextures.end());
                    const auto dataByteLength = textures.size() * sizeof(float);

                    std::cout << "MeshTex0Coords: " << dataByteLength << " bytes of texture0 data\n";
                }

                const Accessor& accessor = document->accessors.Get(meshPrimitive.indicesAccessorId);

                if ((accessor.type == TYPE_SCALAR) &&
                    (accessor.componentType == COMPONENT_UNSIGNED_SHORT))
                {
                    indexStrides.push_back(indices.size());
                    std::vector<uint16_t> uint16Indices = resourceReader->ReadBinaryData<uint16_t>(*document, accessor);

                    for (uint16_t index : uint16Indices)
                    {
                        indices.push_back(index);
                    }
                }
                else if ((accessor.type == TYPE_SCALAR) &&
                         (accessor.componentType == COMPONENT_UNSIGNED_INT))
                {
                    indexStrides.push_back(indices.size());
                    auto newIndices = resourceReader->ReadBinaryData<uint32_t>(*document, accessor);
                    indices.insert(indices.end(), newIndices.begin(), newIndices.end());

                    is32BitIndices = true;
                }
                std::string::size_type sz; // alias of size_t
                materialIndices.push_back(std::stoi(meshPrimitive.materialId, &sz));

            }

            if (loadType == ModelLoadType::Collection || loadType == ModelLoadType::Scene)
            {
                break;
            }
        }

        std::vector<int> textureIndexing;
        std::vector<int> texturesPerMaterial;
        std::vector<float> materialTransmission;

        // Use the resource reader to get each image's data
        for (int materialIndex : materialIndices)
        {
            const auto& material = document->materials.Elements()[materialIndex];

            auto textureInMaterial = material.GetTextures();

            auto texturesPrevSize = textureIndexing.size();

            bool materialsFound[3] = {false, false, false};

            std::string::size_type sz; // alias of size_t
            for (auto texture : textureInMaterial)
            {
                if (texture.first.empty() == false &&
                    texture.second == Microsoft::glTF::TextureType::BaseColor)
                {
                    int index = std::stoi(texture.first, &sz);
                    textureIndexing.push_back(index);
                    materialsFound[0] = true;
                }
            }

            for (auto texture : textureInMaterial)
            {
                if (texture.first.empty() == false &&
                    texture.second == Microsoft::glTF::TextureType::Normal)
                {
                    int index = std::stoi(texture.first, &sz);
                    textureIndexing.push_back(index);
                    materialsFound[1] = true;
                }
            }

            for (auto texture : textureInMaterial)
            {
                if (texture.first.empty() == false &&
                    texture.second == Microsoft::glTF::TextureType::MetallicRoughness)
                {
                    int index = std::stoi(texture.first, &sz);
                    textureIndexing.push_back(index);
                    materialsFound[2] = true;
                }
            }

            // If material roughness is supplied then just use albedo
            if (materialsFound[1] == false ||
                materialsFound[2] == false)
            {
                for (auto texture : textureInMaterial)
                {
                    if (texture.first.empty() == false &&
                        texture.second == Microsoft::glTF::TextureType::BaseColor)
                    {
                        int index = std::stoi(texture.first, &sz);

                        if (materialsFound[1] == false)
                        {
                            textureIndexing.push_back(index);
                        }
                        if (materialsFound[2] == false)
                        {
                            textureIndexing.push_back(index);
                        }
                    }
                }
            }

            materialTransmission.push_back(material.metallicRoughness.baseColorFactor.a);
            
            texturesPerMaterial.push_back(textureIndexing.size() - texturesPrevSize);
        }

        if (loadType == ModelLoadType::Collection || loadType == ModelLoadType::Scene)
        {
            std::string strippedExtension = pathFile.substr(0, pathFile.find("."));
            std::string name = strippedExtension + std::to_string(modelIndex) + "collection";
            model = ModelBroker::instance()->getModel(name);
        }

        model->getRenderBuffers()->set32BitIndices(is32BitIndices);

        int i = 0;
        int materialIndex = 0;
        std::vector<std::string> materialTextureNames;
        for (const auto& texture : textureIndexing)
        {
            auto modelName            = model->getName();
            auto lodStrippedModelName = modelName.substr(0, modelName.find_last_of("_"));

            const auto& image = document->images.Elements()[texture];

            std::string filename;

            if (image.uri.empty())
            {
                assert(!image.bufferViewId.empty());

                auto& bufferView = document->bufferViews.Get(image.bufferViewId);
                auto& buffer     = document->buffers.Get(bufferView.bufferId);

                filename += buffer.uri; //NOTE: buffer uri is empty if image is stored in GLB binary chunk
            }
            else if (IsUriBase64(image.uri))
            {
                filename = "Data URI";
            }
            else
            {
                filename = image.uri;
            }

            if (loadType == ModelLoadType::Collection || loadType == ModelLoadType::Scene)
            {
                materialTextureNames.push_back(TEXTURE_LOCATION + "collections/" + filename);
            }
            else
            {
                materialTextureNames.push_back(TEXTURE_LOCATION + lodStrippedModelName + "/" + filename);
            }

            materialIndex++;
            if (materialIndex == texturesPerMaterial[i])
            {
                model->addMaterial(materialTextureNames, vertexStrides[i], vertexStrides[i], indexStrides[i], materialTransmission[i]);

                i++;
                materialIndex = 0;
                materialTextureNames.clear();
            }
        }

        if (loadType == ModelLoadType::Collection || loadType == ModelLoadType::Scene)
        {
            buildModel(vertices, normals, textures, indices, model);
            modelIndex++;
        }
        else
        {
            break;
        }
    }

    if (loadType == ModelLoadType::Scene)
    {
        ViewEventDistributor::CameraSettings camSettings;

        // Use the resource reader to get each mesh primitive's position data
        for (int nodeIndex = 0; nodeIndex < document->nodes.Elements().size(); nodeIndex++)
        {
            const auto& node = document->nodes.Elements()[nodeIndex];

            // Load instances of geometry using node hierarchy
            if (node.meshId.empty() == false)
            {

                std::string::size_type sz; // alias of size_t
                int                    meshIndex = std::stoi(node.meshId, &sz);
                auto                   meshModel = modelsPending[meshIndex];

                SceneEntity sceneEntity;

                Vector4 quaternion(node.rotation.x, node.rotation.y, node.rotation.z,
                                   node.rotation.w);
                sceneEntity.modelname = meshModel->getName();
                sceneEntity.name      = meshModel->getName() + std::to_string(nodeIndex);
                sceneEntity.position =
                    Vector4(-node.translation.x, node.translation.y, node.translation.z);
                sceneEntity.rotation =
                    Vector4(GetRoll(quaternion), GetPitch(quaternion), -GetYaw(quaternion));
                sceneEntity.scale = Vector4(node.scale.x, node.scale.y, node.scale.z);

                EngineManager::instance()->addEntity(sceneEntity);
            }
            // Load camera from child nodes
            else if (node.children.empty() == false)
            {
                Vector4 cameraPosition(-node.translation.x, node.translation.y, node.translation.z,
                                       1);
                Vector4 quaternion(node.rotation.x, node.rotation.y, node.rotation.z,
                                   node.rotation.w);

                camSettings.bobble           = false;
                camSettings.lockedEntity     = -1;
                camSettings.lockedEntityName = "";
                camSettings.lockOffset       = Vector4(0.0, 0.0, 0.0, 0.0);
                camSettings.path             = "";
                camSettings.position         = cameraPosition;
                camSettings.rotation =
                    Vector4(GetRoll(quaternion), 180.0 + GetPitch(quaternion), -GetYaw(quaternion));
                camSettings.type = ViewEventDistributor::CameraType::WAYPOINT;
            }
        }

        // Use the resource reader to get each mesh primitive's position data
        for (const auto& animation : document->animations.Elements())
        {
            std::vector<PathWaypoint> waypoints;

            std::string::size_type sz; // alias of size_t
            int                    cameraIndex         = std::stoi(animation.id, &sz);
            auto                   samplerIds          = document->animations[cameraIndex].samplers.Elements();

            const Accessor& translationTimeAccessor = document->accessors.Get(samplerIds[0].inputAccessorId);
            const Accessor& rotationTimeAccessor    = document->accessors.Get(samplerIds[1].inputAccessorId);
            const Accessor& translationAccessor     = document->accessors.Get(samplerIds[0].outputAccessorId);
            const Accessor& rotationAccessor        = document->accessors.Get(samplerIds[1].outputAccessorId);

            auto translationTimeFloats = resourceReader->ReadBinaryData<float>(*document, translationTimeAccessor);
            auto rotationTimeFloats    = resourceReader->ReadBinaryData<float>(*document, rotationTimeAccessor);
            auto translationVectors    = resourceReader->ReadBinaryData<float>(*document, translationAccessor);
            auto rotationVectors       = resourceReader->ReadBinaryData<float>(*document, rotationAccessor);

            int rotationIndex    = 0;
            int translationIndex = 0;
            int timeIndex        = 0;
            float previousTime   = 0.0;
            while (timeIndex < translationTimeFloats.size())
            {
                Vector4 quaternion(rotationVectors[rotationIndex], rotationVectors[rotationIndex + 1],
                        rotationVectors[rotationIndex + 2], rotationVectors[rotationIndex + 3]);

                Vector4 rotation =
                    Vector4(GetRoll(quaternion), 180.0 + GetPitch(quaternion), -GetYaw(quaternion));

                // Way points are in milliseconds
                PathWaypoint waypoint(Vector4(-translationVectors[translationIndex],
                                              translationVectors[translationIndex + 1],
                                              translationVectors[translationIndex + 2]), 
                                      rotation,
                                      (translationTimeFloats[timeIndex] - previousTime) * 1000.0);

                previousTime = translationTimeFloats[timeIndex];

                waypoints.push_back(waypoint);

                rotationIndex += 4;
                translationIndex += 3;
                timeIndex++;
            }

            auto viewMan = ModelBroker::getViewManager();
            viewMan->setCamera(camSettings, &waypoints);
        }
    }

    for (auto modelPending : modelsPending)
    {
        modelPending->_isLoaded = true;
    }
}

void PrintInfo(const std::filesystem::path& path,
               std::vector<float>& vertices,
               std::vector<float>& normals,
               std::vector<float>& textures,
               std::vector<uint32_t>& indices,
               Model* model,
               ModelLoadType loadType)
{
    // Pass the absolute path, without the filename, to the stream reader
    auto streamReader = std::make_unique<StreamReader>(path.parent_path());

    std::filesystem::path pathFile = path.filename();
    std::filesystem::path pathFileExt = pathFile.extension();

    std::string manifest;

    auto MakePathExt = [](const std::string& ext)
    {
        return "." + ext;
    };

    std::unique_ptr<GLTFResourceReader> resourceReader;

    // If the file has a '.gltf' extension then create a GLTFResourceReader
    if (pathFileExt == MakePathExt(GLTF_EXTENSION))
    {
        auto gltfStream = streamReader->GetInputStream(pathFile.u8string()); // Pass a UTF-8 encoded filename to GetInputString
        auto gltfResourceReader = std::make_unique<GLTFResourceReader>(std::move(streamReader));

        std::stringstream manifestStream;

        // Read the contents of the glTF file into a string using a std::stringstream
        manifestStream << gltfStream->rdbuf();
        manifest = manifestStream.str();

        resourceReader = std::move(gltfResourceReader);
    }

    // If the file has a '.glb' extension then create a GLBResourceReader. This class derives
    // from GLTFResourceReader and adds support for reading manifests from a GLB container's
    // JSON chunk and resource data from the binary chunk.
    if (pathFileExt == MakePathExt(GLB_EXTENSION))
    {
        auto glbStream = streamReader->GetInputStream(pathFile.u8string()); // Pass a UTF-8 encoded filename to GetInputString
        auto glbResourceReader = std::make_unique<GLBResourceReader>(std::move(streamReader), std::move(glbStream));

        manifest = glbResourceReader->GetJson(); // Get the manifest from the JSON chunk

        resourceReader = std::move(glbResourceReader);
    }

    if (!resourceReader)
    {
        throw std::runtime_error("Command line argument path filename extension must be .gltf or .glb");
    }

    Document document;

    try
    {
        document = Deserialize(manifest);
    }
    catch (const GLTFException& ex)
    {
        std::stringstream ss;

        ss << "Microsoft::glTF::Deserialize failed: ";
        ss << ex.what();

        throw std::runtime_error(ss.str());
    }

    std::cout << "### glTF Info - " << pathFile << " ###\n\n";

    PrintResourceInfo(&document, resourceReader.get(), vertices, normals, textures, indices, model,
                      loadType, pathFile.string());
}

GltfLoader::GltfLoader(std::string name) : _fileName(name), _strideIndex(0), _copiedOverFlag(false)
{
}

GltfLoader::~GltfLoader()
{
}

std::string GltfLoader::getModelName()
{
    std::string modelName = _fileName.substr(_fileName.find_last_of("/") + 1);
    modelName             = modelName.substr(0, modelName.find_last_of("."));
    modelName             = modelName.substr(0, modelName.find_last_of("_"));
    return modelName;
}

Matrix GltfLoader::getObjectSpaceTransform() { return _objectSpaceTransform; }

void GltfLoader::buildTriangles(Model* model)
{

    std::filesystem::path path = _fileName;

    if (path.is_relative())
    {
        auto pathCurrent = std::filesystem::current_path();

        // Convert the relative path into an absolute path by appending the command line argument to
        // the current path
        pathCurrent /= path;
        pathCurrent.swap(path);
    }

    if (!path.has_filename())
    {
        throw std::runtime_error("Command line argument path has no filename");
    }

    if (!path.has_extension())
    {
        throw std::runtime_error("Command line argument path has no filename extension");
    }

    PrintInfo(path, _vertices, _normals, _textures, _indices, model, ModelLoadType::SingleModel);

    buildModel(_vertices, _normals, _textures, _indices, model);

    model->_isLoaded = true;
}


void GltfLoader::buildCollection(Model* masterModel, ModelLoadType loadType)
{
    std::filesystem::path path = _fileName;

    if (path.is_relative())
    {
        auto pathCurrent = std::filesystem::current_path();

        // Convert the relative path into an absolute path by appending the command line argument to
        // the current path
        pathCurrent /= path;
        pathCurrent.swap(path);
    }

    if (!path.has_filename())
    {
        throw std::runtime_error("Command line argument path has no filename");
    }

    if (!path.has_extension())
    {
        throw std::runtime_error("Command line argument path has no filename extension");
    }

    PrintInfo(path, _vertices, _normals, _textures, _indices, masterModel, loadType);

}
