#include "MergeShader.h"
#include "DXLayer.h"
#include "EngineManager.h"
#include "HLSLShader.h"
#include "MRTFrameBuffer.h"
#include "MVP.h"

MergeShader::MergeShader(std::string shaderName)
{
    std::vector<DXGI_FORMAT>* formats = new std::vector<DXGI_FORMAT>();
    formats->push_back(DXGI_FORMAT_R8G8B8A8_UNORM);
    formats->push_back(DXGI_FORMAT_D32_FLOAT);
    _shader = new HLSLShader(shaderName, "", formats);
}

MergeShader::~MergeShader() {}

void MergeShader::runShader(Texture* deferredTexture, Texture* velocityTexture)
{
    // LOAD IN SHADER
    // use context for loaded shader
    _shader->bind();
    _shader->bindAttributes(nullptr, false);

    if (deferredTexture != nullptr)
    {
        _shader->updateData("deferredTexture", 0, deferredTexture);
    }
    if (velocityTexture != nullptr)
    {
        _shader->updateData("velocityTexture", 0, velocityTexture);
    }

    _shader->draw(0, 1, 3);

    _shader->unbindAttributes();
    _shader->unbind();
}
