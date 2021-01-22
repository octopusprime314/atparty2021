#include "RenderBuffers.h"

RenderBuffers::RenderBuffers() {}
RenderBuffers::~RenderBuffers() {}

std::vector<Vector4>* RenderBuffers::getVertices() { return &_vertices; }

std::vector<Vector4>* RenderBuffers::getNormals() { return &_normals; }

std::vector<Tex2>* RenderBuffers::getTextures() { return &_textures; }

std::vector<Vector4>* RenderBuffers::getDebugNormals() { return &_debugNormals; }

void RenderBuffers::addVertex(Vector4 vertex) { _vertices.push_back(vertex); }

void RenderBuffers::addNormal(Vector4 normal) { _normals.push_back(normal); }

void RenderBuffers::addTexture(Tex2 texture) { _textures.push_back(texture); }

void RenderBuffers::addDebugNormal(Vector4 normal) { _debugNormals.push_back(normal); }

void RenderBuffers::set32BitIndices(bool is32Bit) { _isIndexBuffer32Bit = is32Bit; }

bool RenderBuffers::is32BitIndices() { return _isIndexBuffer32Bit; }

void RenderBuffers::addTextureMapIndex(int textureMapIndex)
{
    _textureMapIndices.push_back(textureMapIndex);
}
void RenderBuffers::addTextureMapName(std::string textureMapName)
{
    // Discard if it already exists
    for (auto textureName : _textureMapNames)
    {
        if (textureMapName.compare(textureName) == 0)
        {
            return;
        }
    }
    _textureMapNames.push_back(textureMapName);
}

int RenderBuffers::getTextureMapIndex(std::string textureMapName)
{
    int index = 0;
    for (auto textureName : _textureMapNames)
    {
        if (textureMapName.compare(textureName) == 0)
        {
            return index;
        }
        index++;
    }
    return -1;
}

std::vector<int>* RenderBuffers::getTextureMapIndices() { return &_textureMapIndices; }

std::vector<std::string>* RenderBuffers::getTextureMapNames() { return &_textureMapNames; }

void RenderBuffers::setVertexIndices(std::vector<uint32_t> indices) { _indices = indices; }

void RenderBuffers::addVertexIndices(std::vector<uint32_t> indices)
{
    _indices.insert(_indices.end(), indices.begin(), indices.end());
}

std::vector<uint32_t>* RenderBuffers::getIndices() { return &_indices; }
void                   RenderBuffers::clearBuffers()
{
    _vertices.resize(0);
    _normals.resize(0);
    _textures.resize(0);
    _debugNormals.resize(0);
    _indices.resize(0);
}

void RenderBuffers::reserveBuffers(int reserveSize)
{
    _vertices.reserve(reserveSize);
    _normals.reserve(reserveSize);
    _textures.reserve(reserveSize);
    _debugNormals.reserve(reserveSize * 2);
    _indices.reserve(reserveSize);
}