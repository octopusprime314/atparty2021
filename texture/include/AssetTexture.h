/*
 * AssetTexture is part of the ReBoot distribution (https://github.com/octopusprime314/ReBoot.git).
 * Copyright (c) 2017 Peter Morley.
 *
 * ReBoot is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * ReBoot is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

/**
 *  The AssetTexture class opens and stores textures associated with loaded models
 */

#pragma once
#include "Texture.h"
#include <iostream>
#include <fstream>
#include <vector>
///////////////////////////////////////////////////////////////////////////////
// RGBA
///////////////////////////////////////////////////////////////////////////////
struct RGBA
{
    size_t                     w;
    size_t                     h;
    std::vector<unsigned char> buf;

    void resize(size_t _w, size_t _h)
    {
        w = _w;
        h = _h;
        buf.resize(w * h * 4);
        std::fill(buf.begin(), buf.end(), 0);
    }

    bool writePPM(const std::string& path)
    {
        std::ofstream io(path.c_str(), std::ios::binary);
        if (!io)
        {
            std::cout << "fail to open: " << path << std::endl;
            return false;
        }

        io << "P6\n" << w << ' ' << h << '\n' << 255 << '\n';
        unsigned char* src = &buf[0];
        for (size_t y = 0; y < h; ++y)
        {
            for (size_t x = 0; x < w; ++x, src += 4)
            {
                io.write((char*)src, 3); // write rgb
            }
        }

        return true;
    }
};

class AssetTexture : public Texture
{

    // Make the default constructor private which forces coder to allocate a Texture with a string
    // name
    AssetTexture();

    void _build2DTextureDX(std::string textureName, ComPtr<ID3D12GraphicsCommandList4>& cmdList,
                           ComPtr<ID3D12Device>& device);

    void _build2DTextureDX(void* data, UINT width, UINT height,
                           ComPtr<ID3D12GraphicsCommandList4>& cmdList,
                           ComPtr<ID3D12Device>&              device);

    void _buildCubeMapTextureDX(std::string skyboxName, ComPtr<ID3D12GraphicsCommandList4>& cmdList,
                                ComPtr<ID3D12Device>& device);


    bool _getTextureData(std::string textureName);
    void _load(const std::string& path);

    unsigned int _imageBufferSize;
    bool         _alphaValues;
    BYTE*        _bits;

  public:
    AssetTexture(std::string textureName, bool cubeMap = false);

    AssetTexture(std::string textureName, ComPtr<ID3D12GraphicsCommandList4>& cmdList,
                 ComPtr<ID3D12Device>& device, TextureBlock* texData = nullptr,
                 bool cubeMap = false);

    AssetTexture(void* data, UINT width, UINT height, ComPtr<ID3D12GraphicsCommandList4>& cmdList,
                 ComPtr<ID3D12Device>& device);

    AssetTexture(void* data, UINT width, UINT height);

    ~AssetTexture();

    bool      getTransparency();
    void      buildMipLevels();
    BYTE*     getBits();
};