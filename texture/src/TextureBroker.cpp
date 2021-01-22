#include "TextureBroker.h"
#include "DXLayer.h"
#include "EngineManager.h"
TextureBroker* TextureBroker::_broker = nullptr;

TextureBroker* TextureBroker::instance()
{ // Only initializes the static pointer once
    if (_broker == nullptr)
    {
        _broker = new TextureBroker();
    }
    return _broker;
}
TextureBroker::TextureBroker()
{
    _textureLoadsInFlight = 0;
    _textureLoadsFinished = 0;
}
TextureBroker::~TextureBroker() {}

void TextureBroker::addTexture(std::string textureName, TextureBlock* texData)
{
    _lock.lock();
    _textureLoadsInFlight++;
    _lock.unlock();
    if (_textures.find(textureName) == _textures.end())
    {
        if (EngineManager::getGraphicsLayer() == GraphicsLayer::OPENGL)
        {
            auto texture = new AssetTexture(textureName);
            _lock.lock();
            _textures[textureName] = texture;
            _lock.unlock();
        }
        else if (EngineManager::getGraphicsLayer() >= GraphicsLayer::DX12)
        {
            auto texture = new AssetTexture(textureName,
                                            DXLayer::instance()->getTextureCopyCmdList(),
                                            DXLayer::instance()->getDevice(),
                                            texData);
            _lock.lock();
            _textures[textureName] = texture;
            _lock.unlock();
        }
    }
    _lock.lock();
    _textureLoadsFinished++;
    _lock.unlock();
}

void TextureBroker::releaseUploadBuffers()
{
    for (auto texture : _textures)
    {
        texture.second->getResource()->getUploadResource()->Release();
    }
}

void TextureBroker::addLayeredTexture(std::vector<std::string> textureNames)
{
    _lock.lock();
    _textureLoadsInFlight++;
    _lock.unlock();
    std::string sumString = "Layered";
    for (auto& str : textureNames)
    {
        sumString += str;
    }
    if (_layeredTextures.find(sumString) == _layeredTextures.end())
    {
        std::vector<AssetTexture*> textures;

        if (EngineManager::getGraphicsLayer() == GraphicsLayer::OPENGL)
        {

            // prime the asset textures
            for (auto textureName : textureNames)
            {
                if (getTexture(textureName) == nullptr)
                {
                    addTexture(textureName);
                }
                textures.push_back(getTexture(textureName));
            }

            _layeredTextures[sumString] = new LayeredTexture(textures);
        }
        else if (EngineManager::getGraphicsLayer() >= GraphicsLayer::DX12)
        {

            // prime the asset textures
            for (auto textureName : textureNames)
            {
                if (getTexture(textureName) == nullptr)
                {
                    addTexture(textureName);
                }
                textures.push_back(getTexture(textureName));
            }

            _layeredTextures[sumString] = new LayeredTexture(
                textures, DXLayer::instance()->getCmdList(), DXLayer::instance()->getDevice());
        }
    }
    _lock.lock();
    _textureLoadsFinished++;
    _lock.unlock();
}

bool TextureBroker::areTexturesUploaded()
{
    if(_textureLoadsInFlight == _textureLoadsFinished)
    {
        return true;
    }
    else
    {
        return false;
    }
}


void TextureBroker::addCubeTexture(std::string textureName)
{
    _lock.lock();
    _textureLoadsInFlight++;
    _lock.unlock();
    if (_textures.find(textureName) == _textures.end())
    {
        if (EngineManager::getGraphicsLayer() == GraphicsLayer::OPENGL)
        {
            _textures[textureName] = new AssetTexture(textureName, true);
        }
        else if (EngineManager::getGraphicsLayer() >= GraphicsLayer::DX12)
        {
            _textures[textureName] =
                new AssetTexture(textureName, DXLayer::instance()->getTextureCopyCmdList(),
                                 DXLayer::instance()->getDevice(), nullptr, true);
        }
    }
    _lock.lock();
    _textureLoadsFinished++;
    _lock.unlock();
}

AssetTexture* TextureBroker::getTexture(std::string textureName)
{
    if (_textures.find(textureName) != _textures.end())
    {
        return _textures[textureName];
    }
    else
    {
        return nullptr;
    }
}

LayeredTexture* TextureBroker::getLayeredTexture(std::string textureName)
{

    if (_layeredTextures.find(textureName) != _layeredTextures.end())
    {
        return _layeredTextures[textureName];
    }
    else
    {
        return nullptr;
    }
}

AssetTexture* TextureBroker::getAssetTextureFromLayered(std::string textureName)
{

    for (auto& layeredTexture : _layeredTextures)
    {
        auto assetTextures = layeredTexture.second->getTextures();
        for (auto assetTexture : assetTextures)
        {
            if (assetTexture->getName().find(textureName) != std::string::npos)
            {
                return assetTexture;
            }
        }
    }
    return nullptr;
}