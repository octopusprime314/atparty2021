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
    ImageData imageInfo = {};
    // Bind read textures
    imageInfo.readOnly = true;
    if (format == TextureFormat::RGBA_UNSIGNED_BYTE || format == TextureFormat::RGBA_FLOAT)
    {
        imageInfo.format = 0;
    }
    else if (format == TextureFormat::R_FLOAT || format == TextureFormat::R_UNSIGNED_BYTE)
    {
        imageInfo.format = 1;
    }
    _shader->updateData("readTexture", 0, readTexture, imageInfo);
    imageInfo.readOnly = false;
    _shader->updateData("writeTexture", 1, writeTexture, imageInfo);

    Vector4 threadGroupSize =_shader->getThreadGroupSize();

    // Dispatch the shader
    _shader->dispatch(ceilf(static_cast<float>(writeTexture->getWidth())  / threadGroupSize.getx()),
                      ceilf(static_cast<float>(writeTexture->getHeight()) / threadGroupSize.gety()),
                      threadGroupSize.getz());
    _shader->unbind();
}
