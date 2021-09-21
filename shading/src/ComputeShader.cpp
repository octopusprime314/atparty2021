#include "ComputeShader.h"
#include "EngineManager.h"
#include "HLSLShader.h"

ComputeShader::ComputeShader(std::string computeShaderName)
{
    std::vector<DXGI_FORMAT>* formats = new std::vector<DXGI_FORMAT>();
    formats->push_back(DXGI_FORMAT_R32_FLOAT);
    _shader = new HLSLShader(computeShaderName, "", formats);
}

ComputeShader::~ComputeShader() {}

void ComputeShader::runShader(Texture* writeTexture, Texture* readTexture, TextureFormat format)
{
    _shader->bind();
   
    _shader->updateData("readTexture", _shader->_resourceIndexes["readTexture"], readTexture, true, false);
    _shader->updateData("writeTexture", _shader->_resourceIndexes["writeTexture"], writeTexture, true, true);

    Vector4 threadGroupSize =_shader->getThreadGroupSize();

    // Dispatch the shader
    _shader->dispatch(ceilf(static_cast<float>(writeTexture->getWidth())  / threadGroupSize.getx()),
                      ceilf(static_cast<float>(writeTexture->getHeight()) / threadGroupSize.gety()),
                      threadGroupSize.getz());
    _shader->unbind();
}
