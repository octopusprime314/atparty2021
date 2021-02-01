#include "MRTFrameBuffer.h"
#include "DXLayer.h"
#include "EngineManager.h"
#include "IOEventDistributor.h"

MRTFrameBuffer::MRTFrameBuffer()
{

    _gBufferTextures.push_back(RenderTexture(IOEventDistributor::screenPixelWidth,
                                             IOEventDistributor::screenPixelHeight,
                                             TextureFormat::RGBA_UNSIGNED_BYTE));

    _gBufferTextures.push_back(RenderTexture(IOEventDistributor::screenPixelWidth,
                                             IOEventDistributor::screenPixelHeight,
                                             TextureFormat::RGBA_FLOAT));

    _gBufferTextures.push_back(RenderTexture(IOEventDistributor::screenPixelWidth,
                                             IOEventDistributor::screenPixelHeight,
                                             TextureFormat::RGBA_FLOAT));

    _gBufferTextures.push_back(RenderTexture(IOEventDistributor::screenPixelWidth,
                                             IOEventDistributor::screenPixelHeight,
                                             TextureFormat::DEPTH32_FLOAT));
}

MRTFrameBuffer::~MRTFrameBuffer() {}

std::vector<RenderTexture>& MRTFrameBuffer::getTextures() { return _gBufferTextures; }
