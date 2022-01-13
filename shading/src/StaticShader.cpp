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
    int texturesPerMaterial = TexturesPerMaterial;
    _shader->updateData("texturesPerMaterial", &texturesPerMaterial, false);

}

void StaticShader::runShader(std::vector<Entity*> entities)
{
    bool sameModel = false;
    std::string currModelName = "";
    int         instanceCount = 0;
    Entity*     prevEntity    = nullptr;
    int         numEntities   = entities.size();

    for (auto entity : entities)
    {
        
        if (entity->getModel()->getName().compare(currModelName) == 0 || currModelName.empty())
        {
            sameModel = true;
            instanceCount++;
        }
        else
        {
            sameModel = false;
        }

        if (sameModel == false)
        {
            int instanceBufferStartIndex = _entityDrawIndex - instanceCount;
            _shader->updateData("instanceBufferIndex", &instanceBufferStartIndex, false);

            // Special vao call that factors in frustum culling for the scene
            std::vector<VAO*>* vao = prevEntity->getFrustumVAO();
            for (auto vaoInstance : *vao)
            {
                _shader->bindAttributes(vaoInstance, false);

                auto indexAndVertexBufferStrides = vaoInstance->getVertexAndIndexBufferStrides();
                unsigned int strideLocation      = 0;

                for (auto indexAndVertexBufferStride : indexAndVertexBufferStrides)
                {
                    _shader->draw(strideLocation, instanceCount, indexAndVertexBufferStride.second);

                    strideLocation += indexAndVertexBufferStride.second;
                }

                _shader->unbindAttributes();
            }

            instanceCount = 1;
        }

        // Draw the last entity here
        if(entity == entities[numEntities - 1])
        {
            int instanceBufferStartIndex = (_entityDrawIndex + 1) - instanceCount;
            _shader->updateData("instanceBufferIndex", &instanceBufferStartIndex, false);

            // Special vao call that factors in frustum culling for the scene
            std::vector<VAO*>* vao = entity->getFrustumVAO();
            for (auto vaoInstance : *vao)
            {
                _shader->bindAttributes(vaoInstance, false);

                auto indexAndVertexBufferStrides = vaoInstance->getVertexAndIndexBufferStrides();
                unsigned int strideLocation      = 0;

                for (auto indexAndVertexBufferStride : indexAndVertexBufferStrides)
                {
                    _shader->draw(strideLocation, instanceCount, indexAndVertexBufferStride.second);

                    strideLocation += indexAndVertexBufferStride.second;
                }

                _shader->unbindAttributes();
            }
        }

        prevEntity = entity;
        currModelName = entity->getModel()->getName();

        _entityDrawIndex++;
    }
}

void StaticShader::runShader(Entity* entity)
{
    _shader->updateData("instanceBufferIndex", &_entityDrawIndex, false);

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
