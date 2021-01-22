#include "SSCompute.h"
#include "DXLayer.h"
#include "EngineManager.h"
#include "ShaderBroker.h"

SSCompute::SSCompute(std::string computeShader, uint32_t width, uint32_t height,
                     TextureFormat format)
    : _renderTexture(width, height, format),
      _computeShader(
          static_cast<ComputeShader*>(ShaderBroker::instance()->getShader(computeShader)))
{

    _format = format;
}

SSCompute::~SSCompute() {}

unsigned int SSCompute::getTextureContext() { return _renderTexture.getContext(); }

Texture* SSCompute::getTexture() { return &_renderTexture; }

void SSCompute::compute(Texture* readTexture)
{
    _computeShader->runShader(&_renderTexture, readTexture, _format);
}

void SSCompute::compute(Texture* readTexture, Texture* writeTexture)
{
    _computeShader->runShader(writeTexture, readTexture, _format);
}

void SSCompute::uavBarrier()
{

    if (EngineManager::getGraphicsLayer() >= GraphicsLayer::DX12)
    {

        auto                   cmdList = DXLayer::instance()->getCmdList();
        D3D12_RESOURCE_BARRIER barrierDesc;
        ZeroMemory(&barrierDesc, sizeof(barrierDesc));
        barrierDesc.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        cmdList->ResourceBarrier(1, &barrierDesc);
    }
}