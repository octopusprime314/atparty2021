#include "GltfLoader.h"
#include "EngineManager.h"
#include "Entity.h"
#include "Model.h"
#include "AnimatedModel.h"
#include "ModelBroker.h"
#include "RenderBuffers.h"
#include "Texture.h"
#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include "ModelBroker.h"
#include "ViewEventDistributor.h"
#include "json.hpp"
#include "SkinningData.h"

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
        if (textures.size() != 0)
        {
            Tex2 TexA(textures[indexTexA], textures[indexTexA + 1]);
            renderBuffers->addTexture(TexA);
        }
        else
        {
            Tex2 TexA(0.0, 0.0);
            renderBuffers->addTexture(TexA);
        }

        vertexAndNormalIndex += 3;
        textureIndex += 2;
    }

    renderBuffers->addVertexIndices(indices);

    auto animatedModel = dynamic_cast<AnimatedModel*>(model);
    (*model->getVAO())[0]->createVAO(renderBuffers,
        animatedModel == nullptr ? ModelClass::ModelType : ModelClass::AnimatedModelType,
                                     animatedModel);
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
    float z = ConvertToDegrees(asin(std::clamp(2 * q.getx() * q.gety() + 2 * q.getz() * q.getw(), -1.0f, 1.0f)));
    return z;
}

std::vector<PathWaypoint> AnimatingNodes(const Document* document, const GLTFResourceReader* resourceReader,
                    std::vector<PathWaypoint> waypoints, int nodeIndex, std::map<int, int> nodeIndexToAnimationIndex,
                    std::map<int, std::vector<PathWaypoint>>& nodeWayPoints, std::vector<PathWaypoint> currWayPoints,
                    std::map<int, std::map<TargetPath, int>>& nodeToSamplerIndex,
               Matrix transform)
{
    std::string::size_type sz;

    const auto& node = document->nodes.Elements()[nodeIndex];

    auto samplerIds = document->animations[nodeIndexToAnimationIndex[nodeIndex]].samplers.Elements();


    auto targetPaths = nodeToSamplerIndex[nodeIndex];

    std::map<TargetPath, Accessor> inputAccessorForAnimation;
    std::map<TargetPath, Accessor> outputAccessorForAnimation;

    for (auto sampleKind : targetPaths)
    {
        inputAccessorForAnimation[sampleKind.first] = document->accessors.Get(samplerIds[sampleKind.second].inputAccessorId);
        outputAccessorForAnimation[sampleKind.first] = document->accessors.Get(samplerIds[sampleKind.second].outputAccessorId);
    }
        
    // Load instances of geometry using node hierarchy

    std::vector<float> translationTimeFloats;
    std::vector<float> translationVectors;
    std::vector<float> rotationTimeFloats;
    std::vector<float> rotationVectors;
    std::vector<float> scaleTimeFloats;
    std::vector<float> scaleVectors;

    if (inputAccessorForAnimation.find(TargetPath::TARGET_TRANSLATION) != inputAccessorForAnimation.end())
    {
        translationTimeFloats =
            resourceReader->ReadBinaryData<float>(*document, inputAccessorForAnimation[TARGET_TRANSLATION]);
        translationVectors =
            resourceReader->ReadBinaryData<float>(*document, outputAccessorForAnimation[TARGET_TRANSLATION]);
    }

    if (inputAccessorForAnimation.find(TargetPath::TARGET_ROTATION) != inputAccessorForAnimation.end())
    {
        rotationTimeFloats =
            resourceReader->ReadBinaryData<float>(*document, inputAccessorForAnimation[TARGET_ROTATION]);
        rotationVectors =
            resourceReader->ReadBinaryData<float>(*document, outputAccessorForAnimation[TARGET_ROTATION]);
    }

    if (inputAccessorForAnimation.find(TargetPath::TARGET_SCALE) != inputAccessorForAnimation.end())
    {
        scaleTimeFloats =
            resourceReader->ReadBinaryData<float>(*document, inputAccessorForAnimation[TARGET_SCALE]);
        scaleVectors =
            resourceReader->ReadBinaryData<float>(*document, outputAccessorForAnimation[TARGET_SCALE]);
    }

  

    int rotationIndex    = 0;
    int scaleIndex       = 0;
    int translationIndex = 0;
    int timeIndex        = 0;
    float previousTime   = 0.0;

    int iterations = std::max(scaleTimeFloats.size(), std::max(translationTimeFloats.size(), rotationTimeFloats.size()));

    if (iterations < 2)
    {
        return currWayPoints;
    }
 
    // Offset animation by timeOffset in seconds
    float timeOffset  = 0;
    float currentTime = timeOffset;

    currWayPoints.resize(iterations);

    // Don't process step interpolation by making sure iterations are greater than 1
    while (timeIndex < iterations)
    {
        Vector4 translation = Vector4(0.0, 0.0, 0.0);
        Vector4 rotation    = Vector4(0.0, 0.0, 0.0);
        Vector4 scale       = Vector4(1.0, 1.0, 1.0);

        bool grabbedTime = false;
        if (timeIndex < translationTimeFloats.size() &&
            inputAccessorForAnimation.find(TargetPath::TARGET_TRANSLATION) != inputAccessorForAnimation.end())
        {
            translation = Vector4(translationVectors[translationIndex],
                                  translationVectors[translationIndex + 1],
                                  translationVectors[translationIndex + 2]);
            translationIndex += 3;

            currentTime = (translationTimeFloats[timeIndex]) + timeOffset;
        }

        if ((rotationIndex/4) < rotationTimeFloats.size() && currentTime >= rotationTimeFloats[rotationIndex/4] &&
            inputAccessorForAnimation.find(TargetPath::TARGET_ROTATION) != inputAccessorForAnimation.end())
        {
            Vector4 quaternion(rotationVectors[rotationIndex],
                                rotationVectors[rotationIndex + 1],
                                rotationVectors[rotationIndex + 2],
                                rotationVectors[rotationIndex + 3]);

            // Default camera orients in the negative Y direction
            if (/*(node.children.size() > 0 && document->nodes.Elements()[std::stoi(node.children[0], &sz)].cameraId.empty() == false)*/
                (node.name == "Camera"))
            {

                rotation = Vector4(-(90.0f - GetRoll(quaternion)), GetPitch(quaternion), GetYaw(quaternion));
            }
            else
            {
                rotation =
                    Vector4(-GetRoll(quaternion), -GetPitch(quaternion), -GetYaw(quaternion));
            }

            currentTime = (rotationTimeFloats[rotationIndex / 4]) + timeOffset;
            rotationIndex += 4;
        }

        if ((scaleIndex/3) < scaleTimeFloats.size() && currentTime >= scaleTimeFloats[scaleIndex/3] &&
            inputAccessorForAnimation.find(TargetPath::TARGET_SCALE) != inputAccessorForAnimation.end())
        {
            scale = Vector4(scaleVectors[scaleIndex], scaleVectors[scaleIndex + 1],
                            scaleVectors[scaleIndex + 2]);

            currentTime = (scaleTimeFloats[scaleIndex / 3]) + timeOffset;

            scaleIndex += 3;
        }

        if (timeIndex == 0)
        {
            previousTime = currentTime;
        }

        // Way points are in milliseconds

        auto currentTransform = Matrix::translation(translation.getx(), translation.gety(), translation.getz()) *
                           Matrix::rotationAroundY(rotation.gety()) *
                           Matrix::rotationAroundZ(rotation.getz()) *
                           Matrix::rotationAroundX(rotation.getx()) *
                           Matrix::scale(scale.getx(), scale.gety(), scale.getz());

        currentTransform = currWayPoints[waypoints.size()].transform * currentTransform;

        PathWaypoint addedWaypoint(translation,
                                   rotation,
                                   scale,
                                   currentTime * 1000.0, currentTransform);

        currWayPoints[waypoints.size()] = addedWaypoint;

        waypoints.push_back(addedWaypoint);

        timeIndex++;
    }

    nodeWayPoints[nodeIndex] = waypoints;

    return currWayPoints;
}



void ProcessWayPoints(Node node, int nodeIndex, std::vector<Model*> modelsPending,
                      const Document*                           document,
                      std::map<int, std::vector<PathWaypoint>>& nodeWayPoints,
                      std::map<int, int>&                 meshIdsAssigned,
                      std::vector<PathWaypoint> currNodeWayPoints)
{
    auto nodeCount = node.children.size();
    for (int i = 0; i < nodeCount; i++)
    {
        std::string::size_type sz; // alias of size_t
        int                    childNodeIndex = std::stoi(node.children[i], &sz);

        auto tempNode = document->nodes.Elements()[childNodeIndex];

        if (tempNode.meshId.empty() == false)
        {
            // std::string::size_type sz; // alias of size_t
            int  meshIndex = std::stoi(tempNode.meshId, &sz);
            auto meshModel = modelsPending[meshIndex];

            
            if (nodeWayPoints.find(nodeIndex) != nodeWayPoints.end())
            {
                EngineManager::instance()->setEntityWayPointpath(meshModel->getName() +
                                                                     std::to_string(childNodeIndex),
                                                                 nodeWayPoints[nodeIndex]);

                currNodeWayPoints = nodeWayPoints[nodeIndex];

                meshIdsAssigned[meshIndex] = nodeIndex;

            }

            ProcessWayPoints(tempNode, childNodeIndex, modelsPending, document, nodeWayPoints,
                             meshIdsAssigned, currNodeWayPoints);
        }
    }
}

void ProcessChild(const Document* document, const GLTFResourceReader* resourceReader,
                  int childNodeIndex, std::map<int, Matrix>& meshNodeTransforms,
                  Matrix                                    currentTransform,
                  std::map<int, std::map<TargetPath, int>>  nodeToSamplerIndex,
                  std::map<int, std::vector<PathWaypoint>>& nodeWayPoints,
                  std::vector<PathWaypoint> currWayPoints, int recursionIndex,
                  std::map<int, int> nodeIndexToAnimationIndex)
{
    recursionIndex++;
    std::vector<PathWaypoint> waypoints;
    const auto& node = document->nodes.Elements()[childNodeIndex];

    Vector4 position = Vector4(node.translation.x, node.translation.y, node.translation.z);
    Vector4 quaternion(node.rotation.x, node.rotation.y, node.rotation.z, node.rotation.w);
    Vector4 rotation = Vector4(-GetRoll(quaternion), -GetPitch(quaternion), -GetYaw(quaternion));
    Vector4 scale = Vector4(node.scale.x, node.scale.y, node.scale.z);

    auto transform = Matrix::translation(position.getx(), position.gety(), position.getz()) *
                        Matrix::rotationAroundY(rotation.gety()) *
                        Matrix::rotationAroundZ(rotation.getz()) *
                        Matrix::rotationAroundX(rotation.getx()) *
                        Matrix::scale(scale.getx(), scale.gety(), scale.getz());

    auto newTransform = currentTransform * transform;

    meshNodeTransforms[childNodeIndex] = newTransform;

    if (nodeToSamplerIndex.find(childNodeIndex) != nodeToSamplerIndex.end())
    {
        currWayPoints = AnimatingNodes(document, resourceReader, waypoints, childNodeIndex, nodeIndexToAnimationIndex,
                                       nodeWayPoints, currWayPoints, nodeToSamplerIndex, meshNodeTransforms[childNodeIndex]);
    }

    std::string::size_type sz; // alias of size_t
    //int meshId = std::stoi(node.meshId, &sz);

    for (int childIndex = 0; childIndex < node.children.size(); childIndex++)
    {
        std::string::size_type sz; // alias of size_t
        int                    newChildNodeIndex = std::stoi(node.children[childIndex], &sz);
        ProcessChild(document, resourceReader, newChildNodeIndex, meshNodeTransforms, newTransform,
                     nodeToSamplerIndex, nodeWayPoints, currWayPoints, recursionIndex,
                     nodeIndexToAnimationIndex);
    }

}

// Uses the Document and GLTFResourceReader classes to print information about various glTF binary resources
void BuildGltfMeshes(const Document*           document,
                     const GLTFResourceReader* resourceReader,
                     Model*                    model,
                     ModelLoadType             loadType,
                     std::string               pathFile)
{
    std::vector<float>        vertices;
    std::vector<float>        normals;
    std::vector<float>        textures;
    std::vector<uint32_t>     indices;
    std::vector<Model*>       modelsPending;

    if (loadType == ModelLoadType::Collection || loadType == ModelLoadType::Scene)
    {
        
        // Find skin nodes

        Model* saveMaster             = model;
        int    modelWorkerThreadIndex = 0;

        for (int nodeIndex = 0; nodeIndex < document->nodes.Elements().size(); nodeIndex++)
        {
            Node node = document->nodes.Elements()[nodeIndex];
            if (node.meshId.empty() == false)
            {
                std::string::size_type sz; // alias of size_t
                int                    meshIndex = std::stoi(node.meshId, &sz);
                const auto& mesh     = document->meshes.Elements()[meshIndex];
                std::string meshName = mesh.name;

                std::string strippedLod       = pathFile.substr(0, pathFile.find("_"));
                std::string strippedExtension = pathFile.substr(0, pathFile.find_last_of("."));
                std::string name              = COLLECTIONS_MESH_LOCATION + strippedLod + "/" +
                                   strippedExtension + std::to_string(meshIndex) + meshName +
                                   "collection";
                std::string shortName =
                    strippedExtension + std::to_string(meshIndex) + meshName + "collection";

                if (ModelBroker::instance()->getModel(shortName) == nullptr)
                {
                    if (node.skinId.empty() == false)
                    {
                        model = new AnimatedModel(name);
                    }
                    else
                    {
                        model = new Model(name);
                    }
                    ModelBroker::instance()->addCollectionEntry(model);

                    modelsPending.push_back(model);
                }
            }
        }
        saveMaster->setLoadModelCount(modelsPending.size());
    }

    std::map<int, Matrix> meshNodeTransforms;
    

    std::vector<int> rootNodes;
    for (int sceneIndex = 0; sceneIndex < document->scenes.Size(); sceneIndex++)
    {
        const auto& scene = document->scenes.Elements()[sceneIndex];

        std::string::size_type sz; // alias of size_t

        for (int rootNodeIndex = 0; rootNodeIndex < scene.nodes.size(); rootNodeIndex++)
        {
            int rootNode = std::stoi(scene.nodes[rootNodeIndex], &sz);
            rootNodes.push_back(rootNode);
        }
    }

    std::string::size_type                   sz;
    std::map<int, std::vector<PathWaypoint>> nodeWayPoints;

    std::map<int, std::map<TargetPath, int>> nodeToSamplerIndex;

    std::map<int, int> nodeIndexToAnimationIndex;
    int                animationIndex = 0;
    for (const auto& animation : document->animations.Elements())
    {
        for (const auto& channel : animation.channels.Elements())
        {

            int        nodeIndex   = std::stoi(channel.target.nodeId, &sz);
            TargetPath sampleType  = channel.target.path;
            int        sampleIndex = std::stoi(channel.samplerId, &sz);

            nodeToSamplerIndex[nodeIndex][sampleType] = sampleIndex;
            nodeIndexToAnimationIndex[nodeIndex]      = animationIndex;
        }
        animationIndex++;
    }

    Vector4 cameraQuaternion = Vector4(0.0, 0.0, 0.0);
    Vector4 cameraPosition = Vector4(0.0, 0.0, 0.0);
    std::vector<PathWaypoint> waypoints;
    // Use the resource reader to get each mesh primitive's position data
    for (int nodeIndex : rootNodes)
    {
        const auto& node = document->nodes.Elements()[nodeIndex];

        Matrix currentTransform = Matrix();

        Vector4 position = Vector4(node.translation.x, node.translation.y, node.translation.z);
        Vector4 quaternion(node.rotation.x, node.rotation.y, node.rotation.z, node.rotation.w);
        Vector4 rotation =
            Vector4(-GetRoll(quaternion),
                    -GetPitch(quaternion),
                    -GetYaw(quaternion));
        Vector4 scale = Vector4(node.scale.x, node.scale.y, node.scale.z);

        currentTransform =
            Matrix::translation(position.getx(), position.gety(), position.getz()) *
            Matrix::rotationAroundY(rotation.gety()) *
            Matrix::rotationAroundZ(rotation.getz()) *
            Matrix::rotationAroundX(rotation.getx()) *
            Matrix::scale(scale.getx(), scale.gety(), scale.getz());

        meshNodeTransforms[nodeIndex] = meshNodeTransforms[nodeIndex] * currentTransform;

        std::vector<PathWaypoint> wayPointPathsCurrent;
        if (nodeToSamplerIndex.find(nodeIndex) != nodeToSamplerIndex.end())
        {
            wayPointPathsCurrent = AnimatingNodes(document, resourceReader, waypoints, nodeIndex, nodeIndexToAnimationIndex,
                                        nodeWayPoints, wayPointPathsCurrent, nodeToSamplerIndex, meshNodeTransforms[nodeIndex]);
        }

        for (int childIndex = 0; childIndex < node.children.size(); childIndex++)
        {

            int recursionIndex = 0;
            std::string::size_type sz; // alias of size_t
            int childNodeIndex = std::stoi(node.children[childIndex], &sz);
            ProcessChild(document, resourceReader, childNodeIndex, meshNodeTransforms,
                         currentTransform, nodeToSamplerIndex, nodeWayPoints, wayPointPathsCurrent,
                         recursionIndex, nodeIndexToAnimationIndex);
        }
    }

    std::map<int, int> skinnedNodesIndices;
    std::vector<Node>  skinnedNodes;
    // Use the resource reader to get each mesh primitive's position data
    for (int nodeIndex = 0; nodeIndex < document->nodes.Elements().size(); nodeIndex++)
    {
        Node node = document->nodes.Elements()[nodeIndex];

        if (node.skinId.empty() == false)
        {
            skinnedNodes.push_back(node);
            int skinNodeIndex              = std::stoi(node.skinId, &sz);
            skinnedNodesIndices[nodeIndex] = skinNodeIndex;
        }
    }

    std::map<int, std::vector<int>>    jointIds;
    std::map<int, std::vector<Matrix>> inverseBindMatrices;

    int skinIndex = document->skins.Elements().size() - 1;

    while (skinIndex >= 0)
    {
        auto skin = document->skins.Elements()[skinIndex];

        int inverseBindMatricesIndex = std::stoi(skin.inverseBindMatricesAccessorId, &sz);

        for (int i = 0; i < skin.jointIds.size(); i++)
        {
            int jointId = std::stoi(skin.jointIds[i], &sz);
            jointIds[skinIndex].push_back(jointId);
        }

        auto inverseBindMatricesArray = document->accessors.Get(skin.inverseBindMatricesAccessorId);
        auto matrices = resourceReader->ReadBinaryData<float>(*document, inverseBindMatricesArray);

        auto jointId = jointIds[skinIndex];
        for (int i = 0; i < jointId.size(); i++)
        {
            Matrix constructedMatrix;
            constructedMatrix.getFlatBuffer()[0]  = matrices[(i * 16) + 0];
            constructedMatrix.getFlatBuffer()[1]  = matrices[(i * 16) + 1];
            constructedMatrix.getFlatBuffer()[2]  = matrices[(i * 16) + 2];
            constructedMatrix.getFlatBuffer()[3]  = matrices[(i * 16) + 3];
            constructedMatrix.getFlatBuffer()[4]  = matrices[(i * 16) + 4];
            constructedMatrix.getFlatBuffer()[5]  = matrices[(i * 16) + 5];
            constructedMatrix.getFlatBuffer()[6]  = matrices[(i * 16) + 6];
            constructedMatrix.getFlatBuffer()[7]  = matrices[(i * 16) + 7];
            constructedMatrix.getFlatBuffer()[8]  = matrices[(i * 16) + 8];
            constructedMatrix.getFlatBuffer()[9]  = matrices[(i * 16) + 9];
            constructedMatrix.getFlatBuffer()[10] = matrices[(i * 16) + 10];
            constructedMatrix.getFlatBuffer()[11] = matrices[(i * 16) + 11];
            constructedMatrix.getFlatBuffer()[12] = matrices[(i * 16) + 12];
            constructedMatrix.getFlatBuffer()[13] = matrices[(i * 16) + 13];
            constructedMatrix.getFlatBuffer()[14] = matrices[(i * 16) + 14];
            constructedMatrix.getFlatBuffer()[15] = matrices[(i * 16) + 15];

            inverseBindMatrices[skinIndex].push_back(constructedMatrix.transpose());
        }

        for (auto node : skinnedNodes)
        {
            int skinNodeIndex = 0;
            // auto node = skinnedNodes[skinIndex];
            // Skinning
            std::vector<float> joints;
            std::vector<float> weights;
            bool               loadedJoints  = false;
            bool               loadedWeights = false;
            std::string        accessorId;

            if (node.meshId.empty() == false)
            {

                int  meshIndex = std::stoi(node.meshId, &sz);
                auto mesh      = document->meshes[meshIndex];

                for (int meshPrimIndex = 0; meshPrimIndex < mesh.primitives.size(); meshPrimIndex++)
                {
                    const auto& meshPrimitive = mesh.primitives[meshPrimIndex];

                    if (meshPrimitive.TryGetAttributeAccessorId(ACCESSOR_JOINTS_0, accessorId))
                    {
                        const Accessor& accessor = document->accessors.Get(accessorId);

                        auto tempJoints =
                            resourceReader->ReadBinaryData<uint8_t>(*document, accessor);
                        joints.insert(joints.end(), tempJoints.begin(), tempJoints.end());
                        const auto dataByteLength = joints.size() * sizeof(float);

                        loadedJoints = true;
                    }
                    if (meshPrimitive.TryGetAttributeAccessorId(ACCESSOR_WEIGHTS_0, accessorId))
                    {
                        const Accessor& accessor = document->accessors.Get(accessorId);

                        auto tempWeights =
                            resourceReader->ReadBinaryData<float>(*document, accessor);
                        weights.insert(weights.end(), tempWeights.begin(), tempWeights.end());
                        const auto dataByteLength = weights.size() * sizeof(float);

                        loadedWeights = true;
                    }
                }

                auto animatedModel = dynamic_cast<AnimatedModel*>(modelsPending[meshIndex]);
                int  skinNodeIndex = std::stoi(node.skinId, &sz);

                int totalTransforms    = 0;
                int maxFrames          = 0;
                int nodeWayPointsCount = nodeWayPoints.size();
                for (auto nodeWayPoint : nodeWayPoints)
                {
                    totalTransforms += nodeWayPoint.second.size();
                    if (nodeWayPoint.second.size() > maxFrames)
                    {
                        for (int i = 0; i < jointIds[skinNodeIndex].size(); i++)
                        {
                            if (nodeWayPoint.first == jointIds[skinNodeIndex][i])
                            {
                                maxFrames = nodeWayPoint.second.size();
                            }
                        }
                    }
                }

                std::vector<Matrix> finalTransforms;
                animatedModel->setWeights(weights);
                animatedModel->setJoints(joints);

                // Use the resource reader to get each mesh primitive's position data
                for (int nodeIndex = 0; nodeIndex < document->nodes.Elements().size(); nodeIndex++)
                {
                    Node node = document->nodes.Elements()[nodeIndex];

                    if (skinnedNodesIndices.find(nodeIndex) != skinnedNodesIndices.end() &&
                        skinNodeIndex == skinnedNodesIndices[nodeIndex])
                    {
                        for (int i = 0; i < maxFrames; i++)
                        {
                            auto jointId = jointIds[skinNodeIndex];
                            for (int j = 0; j < jointId.size(); j++)
                            {
                                auto   worldToRoot = meshNodeTransforms[jointId[j]].inverse();
                                Matrix jointMatrix;

                                jointMatrix =
                                    /*worldToRoot **/
                                    nodeWayPoints[jointId[j]][i].transform *
                                    inverseBindMatrices[skinNodeIndex][j];

                                finalTransforms.push_back(jointMatrix);
                            }
                        }
                    }
                }
                animatedModel->setKeyFrames(maxFrames);
                animatedModel->setJointMatrices(finalTransforms);
            }
        }
        skinIndex--;
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
                    const Accessor& accessor = document->accessors.Get(accessorId);

                    auto tempVerts = resourceReader->ReadBinaryData<float>(*document, accessor);
                    vertices.insert(vertices.end(), tempVerts.begin(), tempVerts.end());

                    vertexStrides.push_back(vertices.size() / 3);

                }
                if (meshPrimitive.TryGetAttributeAccessorId(ACCESSOR_NORMAL, accessorId))
                {
                    const Accessor& accessor = document->accessors.Get(accessorId);

                    auto tempNormals = resourceReader->ReadBinaryData<float>(*document, accessor);
                    normals.insert(normals.end(), tempNormals.begin(), tempNormals.end());
                }
                if (meshPrimitive.TryGetAttributeAccessorId(ACCESSOR_TEXCOORD_0, accessorId))
                {
                    const Accessor& accessor = document->accessors.Get(accessorId);

                    auto tempTextures = resourceReader->ReadBinaryData<float>(*document, accessor);
                    textures.insert(textures.end(), tempTextures.begin(), tempTextures.end());
                    const auto dataByteLength = textures.size() * sizeof(float);
                }

                const Accessor& accessor = document->accessors.Get(meshPrimitive.indicesAccessorId);

                if ((accessor.type == TYPE_SCALAR) &&
                    (accessor.componentType == COMPONENT_UNSIGNED_SHORT))
                {
                    std::vector<uint16_t> uint16Indices = resourceReader->ReadBinaryData<uint16_t>(*document, accessor);

                    for (uint16_t index : uint16Indices)
                    {
                        indices.push_back(index);
                    }
                    indexStrides.push_back(indices.size());

                }
                else if ((accessor.type == TYPE_SCALAR) &&
                            (accessor.componentType == COMPONENT_UNSIGNED_INT))
                {
                    auto newIndices = resourceReader->ReadBinaryData<uint32_t>(*document, accessor);
                    indices.insert(indices.end(), newIndices.begin(), newIndices.end());

                    indexStrides.push_back(indices.size());
                    is32BitIndices = true;
                }

                if (meshPrimitive.materialId.empty() == false)
                {
                    std::string::size_type sz; // alias of size_t
                    materialIndices.push_back(std::stoi(meshPrimitive.materialId, &sz));
                }
            }

            if (loadType == ModelLoadType::Collection || loadType == ModelLoadType::Scene)
            {
                break;
            }
        }

        std::vector<int> textureIndexing;
        std::vector<int> texturesPerMaterial;
        std::vector<UniformMaterial> uniformMaterials;

        // Use the resource reader to get each image's data
        for (int materialIndex : materialIndices)
        {
            const auto& material = document->materials.Elements()[materialIndex];

            auto textureInMaterial = material.GetTextures();

            auto texturesPrevSize = textureIndexing.size();

            bool materialsFound[4] = {false, false, false, false};

            uniformMaterials.push_back(UniformMaterial());

            std::string::size_type sz; // alias of size_t
            for (auto texture : textureInMaterial)
            {
                if (texture.first.empty() == false &&
                    texture.second == Microsoft::glTF::TextureType::BaseColor)
                {
                    int index = std::stoi(document->textures[texture.first].imageId, &sz);
                    textureIndexing.push_back(index);
                    materialsFound[0] = true;
                }
            }

            if (materialsFound[0] == false)
            {
                textureIndexing.push_back(-1);
            }

            for (auto texture : textureInMaterial)
            {
                if (texture.first.empty() == false &&
                    texture.second == Microsoft::glTF::TextureType::Normal)
                {
                    int index = std::stoi(document->textures[texture.first].imageId, &sz);
                    textureIndexing.push_back(index);
                    materialsFound[1] = true;
                }
            }

            if (materialsFound[1] == false)
            {
                textureIndexing.push_back(-1);
            }

            for (auto texture : textureInMaterial)
            {
                if (texture.first.empty() == false &&
                    texture.second == Microsoft::glTF::TextureType::MetallicRoughness)
                {
                    int index = std::stoi(document->textures[texture.first].imageId, &sz);
                    textureIndexing.push_back(index);
                    materialsFound[2] = true;
                }
            }

            if (materialsFound[2] == false)
            {
                textureIndexing.push_back(-1);
            }

            for (auto texture : textureInMaterial)
            {
                if (texture.first.empty() == false &&
                    texture.second == Microsoft::glTF::TextureType::Emissive)
                {
                    int index = std::stoi(document->textures[texture.first].imageId, &sz);
                    textureIndexing.push_back(index);
                    materialsFound[3] = true;
                }
            }

            if (materialsFound[3] == false)
            {
                textureIndexing.push_back(-1);
            }

            uniformMaterials.back().validBits = 0;
            if (materialsFound[0] == false)
            {
                uniformMaterials.back().validBits |= ColorValidBit; 
            }
            if (materialsFound[1] == false)
            {
                uniformMaterials.back().validBits |= NormalValidBit;
            }
            // Roughness and metallic are stored in the same second texture
            if (materialsFound[2] == false)
            {
                uniformMaterials.back().validBits |= RoughnessValidBit;
                uniformMaterials.back().validBits |= MetallicValidBit;
            }
            if (materialsFound[3] == false)
            {
                // Always use uniform emissive until textures are supported
                uniformMaterials.back().validBits |= EmissiveValidBit;
            }
            if (material.extensions.empty() == false)
            {
                for (const auto& extension : material.extensions)
                {
                    std::stringstream input_KHR_materials_transmission;
                    input_KHR_materials_transmission << extension.second;

                    nlohmann::json jd;
                    input_KHR_materials_transmission >> jd;

                    auto transmissionFactor = jd["transmissionFactor"];

                    uniformMaterials.back().transmittance = transmissionFactor;
                }
            }
            else
            {
                uniformMaterials.back().transmittance = 0.0;
            }

            uniformMaterials.back().baseColor[0]  = material.metallicRoughness.baseColorFactor.r;
            uniformMaterials.back().baseColor[1]  = material.metallicRoughness.baseColorFactor.g;
            uniformMaterials.back().baseColor[2]  = material.metallicRoughness.baseColorFactor.b;
            uniformMaterials.back().metallic      = material.metallicRoughness.metallicFactor;
            uniformMaterials.back().roughness     = material.metallicRoughness.roughnessFactor;
            uniformMaterials.back().emissiveColor[0] = material.emissiveFactor.r;
            uniformMaterials.back().emissiveColor[1] = material.emissiveFactor.g;
            uniformMaterials.back().emissiveColor[2] = material.emissiveFactor.b;

            texturesPerMaterial.push_back(textureIndexing.size() - texturesPrevSize);
        }

        if (materialIndices.size() == 0)
        {
            uniformMaterials.push_back(UniformMaterial());
            uniformMaterials.back().validBits = 0xFF;
            uniformMaterials.back().baseColor[0]     = 1.0;
            uniformMaterials.back().baseColor[1]     = 1.0;
            uniformMaterials.back().baseColor[2]     = 1.0;
            uniformMaterials.back().metallic         = 0.0;
            uniformMaterials.back().roughness        = 0.0;
            uniformMaterials.back().emissiveColor[0] = 0.0;
            uniformMaterials.back().emissiveColor[1] = 0.0;
            uniformMaterials.back().emissiveColor[2] = 0.0;
        }

        if (loadType == ModelLoadType::Collection || loadType == ModelLoadType::Scene)
        {
            const auto& mesh              = document->meshes.Elements()[modelIndex];
            std::string meshName          = mesh.name;
            std::string strippedExtension = pathFile.substr(0, pathFile.find_last_of("."));
            std::string name =
                strippedExtension + std::to_string(modelIndex) + meshName + "collection";
            model = ModelBroker::instance()->getModel(name);
        }

        model->getRenderBuffers()->set32BitIndices(is32BitIndices);

        int i = 0;
        int materialIndex = 0;
        int textureIndex  = 0;
        std::vector<std::string> materialTextureNames;
        std::set<std::string> materialRepeatCount;

        for (const auto& textureSlot : textureIndexing)
        {
            auto modelName            = model->getName();
            auto lodStrippedModelName = modelName.substr(0, modelName.find_last_of("_"));

            std::string filename;

            if (textureSlot != -1)
            {
                const auto& image = document->images.Elements()[textureSlot];


                if (image.uri.empty())
                {
                    assert(!image.bufferViewId.empty());

                    auto& bufferView = document->bufferViews.Get(image.bufferViewId);
                    auto& buffer     = document->buffers.Get(bufferView.bufferId);

                    filename += buffer.uri; // NOTE: buffer uri is empty if image is stored in
                                            // GLB binary chunk
                }
                else if (IsUriBase64(image.uri))
                {
                    filename = "Data URI";
                }
                else
                {
                    filename = image.uri;
                }
            }
            else
            {
                filename = "DefaultBaseColor.dds";
            }

            if (loadType == ModelLoadType::Collection || loadType == ModelLoadType::Scene)
            {
                auto textureName = TEXTURE_LOCATION + "collections/" + filename;
                if (materialRepeatCount.find(textureName) != materialRepeatCount.end())
                {
                    if (textureIndex % (TexturesPerMaterial) == 1)
                    {
                        materialTextureNames.push_back(TEXTURE_LOCATION + "collections/DefaultNormal.dds");
                    }
                    else if (textureIndex % (TexturesPerMaterial) == 2)
                    {
                        materialTextureNames.push_back(TEXTURE_LOCATION + "collections/DefaultAORoughness.dds");
                    }
                    else if (textureIndex % (TexturesPerMaterial) == 3)
                    {
                        materialTextureNames.push_back(TEXTURE_LOCATION + "collections/DefaultEmissiveColor.dds");
                    }
                }
                else
                {
                    materialTextureNames.push_back(textureName);
                }
            }
            else
            {
                materialTextureNames.push_back(TEXTURE_LOCATION + lodStrippedModelName + "/" + filename);
            }

            materialIndex++;
            if (materialIndex == texturesPerMaterial[i])
            {
                model->addMaterial(materialTextureNames,
                                    vertexStrides[i],
                                    vertexStrides[i],
                                    indexStrides[i],
                                    uniformMaterials[i]);

                i++;
                materialIndex = 0;
                materialTextureNames.clear();
            }
            textureIndex++;
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

    if (loadType == ModelLoadType::SingleModel)
    {
        buildModel(vertices, normals, textures, indices, model);
        model->_isLoaded = true;
    }

    std::map<int, int> meshIdsAssigned;
    std::vector<PathWaypoint> currNodeWayPoints;

    if (loadType == ModelLoadType::Scene)
    {
        ViewEventDistributor::CameraSettings camSettings;
        bool camSettingsFound = false;

        // Use the resource reader to get each mesh primitive's position data
        for (int nodeIndex = 0; nodeIndex < document->nodes.Elements().size(); nodeIndex++)
        {
            Node node = document->nodes.Elements()[nodeIndex];

            if (node.cameraId.size() > 0 || node.name == "Camera")
            {
                camSettingsFound = true;
                Vector4 cameraPosition(node.translation.x, node.translation.y, node.translation.z);
                Vector4 quaternion(node.rotation.x,
                                   node.rotation.y,
                                   node.rotation.z,
                                   node.rotation.w);

                Vector4 baseQuaternion(-0.7071067690849304, 0.0, 0.0, 0.7071067690849304);

                camSettings.bobble           = false;
                camSettings.lockedEntity     = -1;
                camSettings.lockedEntityName = "";
                camSettings.lockOffset       = Vector4(0.0, 0.0, 0.0, 0.0);
                camSettings.path             = "";
                camSettings.position         = cameraPosition;
                // Default camera orients in the negative Y direction
                camSettings.rotation =
                    Vector4(GetRoll(baseQuaternion) + GetRoll(quaternion),
                            GetPitch(baseQuaternion) + GetPitch(quaternion),
                            GetYaw(baseQuaternion) + GetYaw(quaternion));

                auto viewMan     = ModelBroker::getViewManager();
                camSettings.type = ViewEventDistributor::CameraType::WAYPOINT;

                if (node.children.size() == 0)
                {
                    camSettings.fov = 30.0f;
                }
                else
                {
                    int cameraNodeIndex = stoi(node.children[0]);
                    auto tempNode        = node;

                    while (tempNode.children.size() != 0)
                    {
                        cameraNodeIndex = stoi(tempNode.children[0]);

                        tempNode = document->nodes.Elements()[cameraNodeIndex];
                    }
                    int cameraId = stoi(document->nodes.Elements()[cameraNodeIndex].cameraId);

                    camSettings.fov =
                        document->cameras[cameraId].GetPerspective().yfov * 180.0 / PI;
                }
                viewMan->setCamera(camSettings, &nodeWayPoints[nodeIndex]);
            }
            else if (node.meshId.empty() == false)
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
                    Vector4(node.translation.x, node.translation.y, node.translation.z);
                sceneEntity.rotation =
                    Vector4(GetRoll(quaternion), GetPitch(quaternion), GetYaw(quaternion));
                sceneEntity.scale = Vector4(node.scale.x, node.scale.y, node.scale.z);

                sceneEntity.useTransform = true;
                sceneEntity.transform    = meshNodeTransforms[nodeIndex];

                if (nodeWayPoints.find(nodeIndex) != nodeWayPoints.end())
                {
                    sceneEntity.waypointVectors = nodeWayPoints[nodeIndex];
                }
                EngineManager::instance()->addEntity(sceneEntity);
            }
            else if (node.children.size() > 0)
            {
                ProcessWayPoints(node, nodeIndex, modelsPending, document, nodeWayPoints,
                                 meshIdsAssigned, currNodeWayPoints);
            }
        }

        if (camSettingsFound == false)
        {
            Vector4 cameraPosition(0, 0, 0);
            Vector4 quaternion(0, 0, 0, 0);

            camSettings.bobble           = false;
            camSettings.lockedEntity     = -1;
            camSettings.lockedEntityName = "";
            camSettings.lockOffset       = Vector4(0.0, 0.0, 0.0, 0.0);
            camSettings.path             = "";
            camSettings.position         = cameraPosition;
            camSettings.fov              = 30.0f;

            Vector4 baseQuaternion(-0.7071067690849304, 0.0, 0.0, 0.7071067690849304);

            // Default camera orients in the negative Y direction
            camSettings.rotation = Vector4(GetRoll(baseQuaternion),
                                           GetPitch(baseQuaternion),
                                           GetYaw(baseQuaternion));

            auto viewMan     = ModelBroker::getViewManager();
            camSettings.type = ViewEventDistributor::CameraType::WAYPOINT;
            viewMan->setCamera(camSettings, nullptr);
        }

        // If camera has no keyframes then just load camera position with no waypoints
        if (document->animations.Elements().size() == 0)
        {
            auto viewMan = ModelBroker::getViewManager();
            camSettings.type = ViewEventDistributor::CameraType::GOD;
            viewMan->setCamera(camSettings, nullptr);
        }

        // Use the resource reader to get each mesh primitive's position data
        int nodeIndex  = 0;
        int lightIndex = 0;
        for (const auto& extension : document->extensions)
        {
            std::stringstream input_KHR_lights_punctual;
            input_KHR_lights_punctual << extension.second;

            nlohmann::json jd;
            input_KHR_lights_punctual >> jd;

            for (auto& light : jd["lights"])
            {
                SceneLight sceneLight;
                
                auto color           = light["color"];
                auto lightType       = light["type"];
                auto intensity       = light["intensity"];

                sceneLight.color     = Vector4(color[0], color[1], color[2]);
                sceneLight.lightType  = lightType == "point" ? LightType::POINT : LightType::DIRECTIONAL;
                sceneLight.effectType = EffectType::Fire;
                sceneLight.name      = light["name"];
                sceneLight.scale     = Vector4(intensity, intensity, intensity);
                sceneLight.lockedIdx = -1;

                bool foundLight = false;
                nodeIndex       = 0;
                int lightNodeIndex = 0;
                // Use the resource reader to get each mesh primitive's position data
                while (nodeIndex < document->nodes.Elements().size())
                {
                    auto node = document->nodes.Elements()[nodeIndex];
                    if (node.children.empty() == false)
                    {
                        std::string::size_type sz; // alias of size_t

                        auto childNodeIndex = std::stoi(node.children[0], &sz);

                        auto childNode = document->nodes.Elements()[childNodeIndex];
                        
                        while (childNode.children.empty() == false)
                        {
                            childNodeIndex = std::stoi(childNode.children[0], &sz);

                            childNode = document->nodes.Elements()[childNodeIndex];
                        }

                        auto extensions = document->nodes.Elements()[childNodeIndex].extensions;

                        for (const auto& nodeExtension : document->nodes.Elements()[childNodeIndex].extensions)
                        {
                            std::stringstream data;
                            data << nodeExtension.second;

                            nlohmann::json nodeLightJd;
                            data >> nodeLightJd;

                            auto readLightIndex = nodeLightJd["light"];

                            if (readLightIndex == lightIndex)
                            {
                                Vector4 quaternion(node.rotation.x, node.rotation.y, node.rotation.z, node.rotation.w);
                                sceneLight.position = Vector4(node.translation.x, node.translation.y, node.translation.z);
                                //sceneLight.rotation = Vector4(-GetRoll(quaternion), -GetPitch(quaternion), -GetYaw(quaternion));
                                lightNodeIndex = nodeIndex;
                                foundLight = true;
                            }
                        }
                    }
                    nodeIndex++;
                }
                if (foundLight == true)
                {
                    if (nodeWayPoints.find(lightNodeIndex) != nodeWayPoints.end())
                    {
                        sceneLight.waypointVectors = nodeWayPoints[lightNodeIndex];
                    }
                    EngineManager::instance()->addLight(sceneLight);
                }
                lightIndex++;
            }
        }
    }

    int threadIndex = 0;
    for (auto modelPending : modelsPending)
    {
        modelPending->_isLoaded = true;
    }
}

void LoadGltfBlob(const std::filesystem::path& path,
                  Model*                       model,
                  ModelLoadType                loadType)
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

    BuildGltfMeshes(&document,
                    resourceReader.get(),
                    model,
                    loadType,
                    pathFile.string());
}

GltfLoader::GltfLoader(std::string name) : _fileName(name)
{
}

GltfLoader::~GltfLoader()
{
}

void GltfLoader::buildModels(Model* masterModel, ModelLoadType loadType)
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

    LoadGltfBlob(path, masterModel, loadType);
}
