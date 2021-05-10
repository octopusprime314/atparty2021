#include "SSAOShader.h"
#include "EngineManager.h"
#include "HLSLShader.h"
#include "MRTFrameBuffer.h"
#include "MVP.h"
#include "SSAO.h"

SSAOShader::SSAOShader(std::string shaderName)
{
    std::vector<DXGI_FORMAT>* formats = new std::vector<DXGI_FORMAT>();
    formats->push_back(DXGI_FORMAT_R32_FLOAT);
    _shader = new HLSLShader(shaderName, "", formats);
}

SSAOShader::~SSAOShader() {}

void SSAOShader::runShader(SSAO* ssao, MRTFrameBuffer* mrtBuffer,
                           ViewEventDistributor* viewEventDistributor)
{

    // LOAD IN SHADER
    _shader->bind();

    _shader->bindAttributes(nullptr, false);

    _shader->updateData("projection", viewEventDistributor->getProjection().getFlatBuffer());
    _shader->updateData("projectionToViewMatrix",
                        viewEventDistributor->getProjection().inverse().getFlatBuffer());

    auto   kernel      = ssao->getKernel();
    float* kernelArray = nullptr;
   
    kernelArray     = new float[4 * kernel.size()];
    int kernelIndex = 0;
    for (auto& vec : kernel)
    {
        float* kernelBuff = vec.getFlatBuffer();
        for (int i = 0; i < 4; i++)
        {
            if (i < 3)
            {
                kernelArray[kernelIndex] = kernelBuff[i];
            }
            kernelIndex++;
        }
    }

    _shader->updateData("kernel[0]", kernelArray);
    delete[] kernelArray;

    auto textures = mrtBuffer->getTextures();

    _shader->updateData("normalTexture", 0, &textures[1]);
    _shader->updateData("noiseTexture",  0, ssao->getNoiseTexture());
    _shader->updateData("depthTexture",  0, &textures[3]);

    _shader->draw(0, 1, 3);

    _shader->unbindAttributes();
    _shader->unbind();
}
