#include "StaticShader.h"
#include "DXLayer.h"
#include "EngineManager.h"
#include "Entity.h"
#include "HLSLShader.h"
#include "Model.h"
#include "ModelBroker.h"

StaticShader::StaticShader(std::string shaderName)
{
    std::vector<DXGI_FORMAT>* formats = new std::vector<DXGI_FORMAT>();
    formats->push_back(DXGI_FORMAT_R8G8B8A8_UNORM);
    formats->push_back(DXGI_FORMAT_R32G32B32A32_FLOAT);
    formats->push_back(DXGI_FORMAT_R32G32B32A32_FLOAT);
    formats->push_back(DXGI_FORMAT_R16G16_FLOAT);
    formats->push_back(DXGI_FORMAT_D32_FLOAT);

    _shader = new HLSLShader(shaderName, "", formats);
}

StaticShader::~StaticShader() {}

void StaticShader::startEntity()
{
    _entityDrawIndex = 0;
   
    _shader->bind();
    ResourceManager* resourceManager = EngineManager::getResourceManager();
    resourceManager->updateTextureUnbounded(_shader->_resourceIndexes["diffuseTexture"], 0, nullptr, 0, false);
    resourceManager->updateStructuredAttributeBufferUnbounded(_shader->_resourceIndexes["vertexBuffer"], nullptr, false);
    resourceManager->updateStructuredIndexBufferUnbounded(_shader->_resourceIndexes["indexBuffer"], nullptr, false);

    resourceManager->updateAndBindMaterialBuffer(_shader->_resourceIndexes, false);
    resourceManager->updateAndBindAttributeBuffer(_shader->_resourceIndexes, false);
    resourceManager->updateAndBindNormalMatrixBuffer(_shader->_resourceIndexes, false);
    resourceManager->updateAndBindUniformMaterialBuffer(_shader->_resourceIndexes, false);
    resourceManager->updateAndBindModelMatrixBuffer(_shader->_resourceIndexes, false);

    auto             viewEventDistributor = EngineManager::instance()->getViewManager();
    auto             cmdList              = DXLayer::instance()->getCmdList();

    auto projection        = viewEventDistributor->getProjection();
    auto cameraView        = viewEventDistributor->getView();
    auto inverseCameraView = cameraView.inverse();

    auto inverseCameraProj = projection.inverse();

    _shader->updateData("inverseView", inverseCameraView.getFlatBuffer(), false);
    _shader->updateData("viewTransform", cameraView.getFlatBuffer(), false);
    _shader->updateData("projTransform", projection.getFlatBuffer(), false);

    _shader->updateData("prevViewTransform", viewEventDistributor->getPrevCameraView().getFlatBuffer(), false);

    float screenSize[] = {static_cast<float>(IOEventDistributor::screenPixelWidth),
                          static_cast<float>(IOEventDistributor::screenPixelHeight)};

    _shader->updateData("screenSize", screenSize, false);
    int texturesPerMaterial = Material::TexturesPerMaterial;
    _shader->updateData("texturesPerMaterial", &texturesPerMaterial, false);
}


void StaticShader::runShader(Entity* entity)
{
    // LOAD IN SHADER
    _shader->bind();

    _shader->updateData("instanceBufferIndex", &_entityDrawIndex, false);
    //_shader->updateData("modelMatrix", entity->getWorldSpaceTransform().getFlatBuffer(), false);

    // Special vao call that factors in frustum culling for the scene
    std::vector<VAO*>* vao = entity->getFrustumVAO();
    for (auto vaoInstance : *vao)
    {
        _shader->bindAttributes(vaoInstance, false);
        

        auto         indexAndVertexBufferStrides = vaoInstance->getVertexAndIndexBufferStrides();
        unsigned int strideLocation = 0;

        for (auto indexAndVertexBufferStride : indexAndVertexBufferStrides)
        {
            _shader->draw(strideLocation, 1, indexAndVertexBufferStride.second);

            strideLocation += indexAndVertexBufferStride.second;
        }

        _shader->unbindAttributes();
    }
    _shader->unbind();

    _entityDrawIndex++;
}
