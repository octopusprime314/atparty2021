#include "DeferredShader.h"
#include "EngineManager.h"
#include "HLSLShader.h"
#include "ResourceManager.h"

DeferredShader::DeferredShader(std::string shaderName)
{
    std::vector<DXGI_FORMAT>* formats = new std::vector<DXGI_FORMAT>();
    formats->push_back(DXGI_FORMAT_R8G8B8A8_UNORM);
    formats->push_back(DXGI_FORMAT_D32_FLOAT);
    // Ray tracing debugging render target
    formats->push_back(DXGI_FORMAT_R32G32B32A32_FLOAT);
    formats->push_back(DXGI_FORMAT_R32G32B32A32_FLOAT);
    _shader = new HLSLShader(shaderName, "", formats);
}

DeferredShader::~DeferredShader() {}

void DeferredShader::runShader(PointLightList*       pointLightList,
                               ViewEventDistributor* viewEventDistributor,
                               MRTFrameBuffer&       gBuffers,
                               RenderTexture*        ssaoTexture)
{
    _shader->bind();
    _shader->bindAttributes(nullptr, false);

    _shader->updateData("numPointLights", &pointLightList->lightCount, false);
    _shader->updateData("pointLightColors", pointLightList->lightColorsArray, false);
    _shader->updateData("pointLightRanges", pointLightList->lightRangesArray, false);
    _shader->updateData("pointLightPositions", pointLightList->lightPosArray, false);

    // Change of basis from camera view position back to world position
    Matrix viewToModelSpace      = viewEventDistributor->getView().inverse();
    Matrix projectionToViewSpace = viewEventDistributor->getProjection().inverse();

    _shader->updateData("inverseProjection", projectionToViewSpace.getFlatBuffer());
    _shader->updateData("inverseView", viewToModelSpace.getFlatBuffer());

    auto textures = gBuffers.getTextures();

    _shader->updateData("diffuseTexture",  0, &textures[0]);
    _shader->updateData("normalTexture",   0, &textures[1]);
    _shader->updateData("positionTexture", 0, &textures[2]);
    _shader->updateData("depthTexture",    0, &textures[4]);
    _shader->updateData("ssaoTexture",     0, ssaoTexture);

    _shader->draw(0, 1, 3);

    _shader->unbindAttributes();
    _shader->unbind();
}
