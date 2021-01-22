#include "AssetTexture.h"
#include "DXLayer.h"
#include <assert.h>

AssetTexture::AssetTexture() {}

AssetTexture::AssetTexture(std::string textureName, bool cubeMap)
    : Texture(textureName), _alphaValues(false)
{

}

AssetTexture::AssetTexture(std::string textureName, ComPtr<ID3D12GraphicsCommandList4>& cmdList,
                           ComPtr<ID3D12Device>& device, TextureBlock* texData, bool cubeMap)
    : Texture(textureName), _alphaValues(false)
{
    bool loadedTexture = true;
    if (texData == nullptr)
    {
        loadedTexture = _getTextureData(_name);
    }
    else
    {
        _bits            = texData->data->data();
        _width           = texData->width;
        _height          = texData->height;
        _name            = texData->name;
        _sizeInBytes     = texData->data->size();
        _alphaValues     = texData->alphaValues;
        _imageBufferSize = _width * _height * 3;
    }

    if (loadedTexture)
    {
        _build2DTextureDX(_name, cmdList, device);
    }
}

AssetTexture::AssetTexture(void* data, UINT width, UINT height,
                           ComPtr<ID3D12GraphicsCommandList4>& cmdList, ComPtr<ID3D12Device>& device)
    : Texture(""), _alphaValues(false)
{

    _build2DTextureDX(data, width, height, cmdList, device);
    buildMipLevels();
}

AssetTexture::AssetTexture(void* data, UINT width, UINT height) : Texture(""), _alphaValues(false)
{
}

AssetTexture::~AssetTexture() {}

void AssetTexture::_build2DTextureDX(void* data, UINT width, UINT height,
                                     ComPtr<ID3D12GraphicsCommandList4>& cmdList,
                                     ComPtr<ID3D12Device>&              device)
{

    _textureBuffer = new ResourceBuffer(data, width * height * 4, width, height, width * 4,
                                        DXGI_FORMAT_B8G8R8A8_UNORM, cmdList, device);

    // Create descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc;
    ZeroMemory(&srvHeapDesc, sizeof(srvHeapDesc));
    srvHeapDesc.NumDescriptors = 1; // 1 2D texture
    srvHeapDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(_srvDescriptorHeap.GetAddressOf()));

    // Create view of SRV for shader access
    CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(
        _srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc           = {};
    auto                            textureDescriptor = _textureBuffer->getDescriptor();
    srvDesc.Shader4ComponentMapping                   = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Format                                    = textureDescriptor.Format;
    srvDesc.ViewDimension                             = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip                 = 0;
    srvDesc.Texture2D.MipLevels                       = textureDescriptor.MipLevels;
    srvDesc.Texture2D.ResourceMinLODClamp             = 0;
    device->CreateShaderResourceView(_textureBuffer->getResource().Get(), &srvDesc, hDescriptor);

    // create sampler descriptor heap
    D3D12_DESCRIPTOR_HEAP_DESC descHeapSampler = {};
    descHeapSampler.NumDescriptors             = 1;
    descHeapSampler.Type                       = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    descHeapSampler.Flags                      = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    device->CreateDescriptorHeap(&descHeapSampler,
                                 IID_PPV_ARGS(_samplerDescriptorHeap.GetAddressOf()));

    // create sampler descriptor in the sample descriptor heap
    D3D12_SAMPLER_DESC samplerDesc;
    ZeroMemory(&samplerDesc, sizeof(D3D12_SAMPLER_DESC));
    samplerDesc.Filter         = D3D12_FILTER_ANISOTROPIC; // D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU       = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressV       = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.AddressW       = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    samplerDesc.MinLOD         = 0;
    samplerDesc.MaxLOD         = D3D12_FLOAT32_MAX;
    samplerDesc.MipLODBias     = 0.0f;
    samplerDesc.MaxAnisotropy  = 1;
    samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
    device->CreateSampler(&samplerDesc,
                          _samplerDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
}

void AssetTexture::_build2DTextureDX(std::string                        textureName,
                                     ComPtr<ID3D12GraphicsCommandList4>& cmdList,
                                     ComPtr<ID3D12Device>&              device)
{
    _textureBuffer = new ResourceBuffer(_bits, _imageBufferSize, _width, _height, _rowPitch,
                                        _textureFormat, cmdList, device);
}

void AssetTexture::buildMipLevels() { _textureBuffer->buildMipLevels(this); }

BYTE* AssetTexture::getBits() { return _bits; }

bool AssetTexture::_getTextureData(std::string textureName)
{
    _load(textureName);
    return true;
}

bool AssetTexture::getTransparency() { return _alphaValues; }

///////////////////////////////////////////////////////////////////////////////
// BinaryReader
///////////////////////////////////////////////////////////////////////////////
class BinaryReader
{
    std::istream& io_;

  public:
    BinaryReader(std::istream& io) : io_(io) {}

    std::string getString(size_t size)
    {
        std::vector<char> buf(size);
        io_.read(&buf[0], size);
        return std::string(buf.begin(), buf.end());
    }

    void read(void* p, size_t size) { io_.read((char*)p, size); }

    template <typename T>
    T get()
    {
        T buf;
        io_.read((char*)&buf, sizeof(T));
        return buf;
    }
};

///////////////////////////////////////////////////////////////////////////////
// DDS struct
///////////////////////////////////////////////////////////////////////////////

// from ddraw.h
#define DDSD_CAPS 0x00000001l // default
#define DDSD_HEIGHT 0x00000002l
#define DDSD_WIDTH 0x00000004l
#define DDSD_PITCH 0x00000008l
#define DDSD_PIXELFORMAT 0x00001000l
#define DDSD_LINEARSIZE 0x00080000l

#define DDPF_ALPHAPIXELS 0x00000001l
#define DDPF_FOURCC 0x00000004l
#define DDPF_RGB 0x00000040l

// Instead of adding dxil header files just include this for dxil shader reflection
#define DXIL_FOURCC(ch0, ch1, ch2, ch3)                                                            \
    ((uint32_t)(uint8_t)(ch0) | (uint32_t)(uint8_t)(ch1) << 8 | (uint32_t)(uint8_t)(ch2) << 16 |   \
     (uint32_t)(uint8_t)(ch3) << 24)
constexpr uint32_t DFCC_TextureArrays = DXIL_FOURCC('D', 'X', '1', '0');


#ifdef _MSC_VER
#pragma pack(push, 1)
#else
typedef unsigned int DWORD;
#endif

struct DDS_PIXELFORMAT
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwFourCC;
    DWORD dwRGBBitCount;
    DWORD dwRBitMask;
    DWORD dwGBitMask;
    DWORD dwBBitMask;
    DWORD dwABitMask;
};
#ifdef __GNUG__
__attribute__((packed))
#endif
;
static_assert(sizeof(DDS_PIXELFORMAT) == 32);

struct DDSCAPS2
{
    DWORD dwCaps1;
    DWORD dwCaps2;
    DWORD Reserved[2];
}
#ifdef __GNUG__
__attribute__((packed))
#endif
;
static_assert(sizeof(DDSCAPS2) == 16);

enum D3D11_RESOURCE_DIMENSION
{
    D3D11_RESOURCE_DIMENSION_UNKNOWN,
    D3D11_RESOURCE_DIMENSION_BUFFER,
    D3D11_RESOURCE_DIMENSION_TEXTURE1D,
    D3D11_RESOURCE_DIMENSION_TEXTURE2D,
    D3D11_RESOURCE_DIMENSION_TEXTURE3D
};

struct DDS_HEADER_DXT10
{
    DXGI_FORMAT              dxgiFormat;
    D3D11_RESOURCE_DIMENSION resourceDimension;
    UINT                     miscFlag;
    UINT                     arraySize;
    UINT                     miscFlags2;
}
#ifdef __GNUG__
__attribute__((packed))
#endif
;
static_assert(sizeof(DDS_HEADER_DXT10) == 20);

struct DDS_HEADER
{
    DWORD           dwSize;
    DWORD           dwFlags;
    DWORD           dwHeight;
    DWORD           dwWidth;
    DWORD           dwPitchOrLinearSize;
    DWORD           dwDepth;
    DWORD           dwMipMapCount;
    DWORD           dwReserved1[11];
    DDS_PIXELFORMAT ddspf;
    DWORD           dwCaps;
    DWORD           dwCaps2;
    DWORD           dwCaps3;
    DWORD           dwCaps4;
    DWORD           dwReserved2;
};
#ifdef __GNUG__
__attribute__((packed))
#endif
;
static_assert(sizeof(DDS_HEADER) == 124);
#ifdef _MSC_VER
#pragma pack(pop)
#endif

///////////////////////////////////////////////////////////////////////////////
// load dds file
///////////////////////////////////////////////////////////////////////////////
void AssetTexture::_load(const std::string& path)
{
    RGBA* image;

    std::ifstream io(path.c_str(), std::ios::binary);
    if (!io)
    {
        return;
    }

    BinaryReader reader(io);

    // magic number
    if (reader.getString(4) != "DDS ")
    {
        return;
    }

    // DDSURFACEDESC2
    DDS_HEADER header = reader.get<DDS_HEADER>();
    assert(header.dwSize == 124);

    if (header.ddspf.dwFlags & DDPF_RGB)
    {
        image = new RGBA;
        image->resize(header.dwWidth, header.dwHeight);
        size_t pitch = header.dwWidth * header.ddspf.dwRGBBitCount / 8;

        std::cout << "<normal image>" << std::endl
                  << "BitCount: " << header.ddspf.dwRGBBitCount << std::endl;
        // normal
        if (header.dwFlags & DDSD_PITCH)
        {
            std::cout << "specified line pitch" << std::endl;
            pitch = header.dwPitchOrLinearSize;
        }

        switch (header.ddspf.dwRGBBitCount)
        {
            case 32:
            {
                assert(header.ddspf.dwRBitMask == 0x00ff0000);
                assert(header.ddspf.dwGBitMask == 0x0000ff00);
                assert(header.ddspf.dwBBitMask == 0x000000ff);
                if (header.ddspf.dwFlags & DDPF_ALPHAPIXELS)
                {
                    std::cout << "has alpha channel" << std::endl;
                    assert(header.ddspf.dwABitMask == 0xff000000);
                }
                unsigned char*             dst = &image->buf[0];
                std::vector<unsigned char> pitchBuffer(pitch);
                for (size_t y = 0; y < header.dwHeight; ++y)
                {
                    reader.read(&pitchBuffer[0], pitch);
                    unsigned char* src = &pitchBuffer[0];
                    for (size_t x = 0; x < header.dwWidth; ++x, dst += 4, src += 4)
                    {
                        dst[0] = src[2]; // R
                        dst[1] = src[1]; // G
                        dst[2] = src[0]; // B
                        dst[3] = src[3]; // A
                    }
                }
            }
            break;

            default:
                std::cout << "not implemented: " << header.ddspf.dwRGBBitCount
                          << std::endl;
                break;
        }
    }
    else if (header.ddspf.dwFlags & DDPF_FOURCC)
    {
        int headerOffset = header.dwSize;
        if (header.ddspf.dwFourCC == DFCC_TextureArrays)
        {
            DDS_HEADER_DXT10 dx10Header = reader.get<DDS_HEADER_DXT10>();
            headerOffset += sizeof(DDS_HEADER_DXT10);
        }

        std::ifstream in(path, std::ifstream::ate | std::ifstream::binary);
        int size = in.tellg();
        size -= headerOffset; 

        // retrieve the image data
        _bits = new BYTE[size];

        reader.read(_bits, size);

        //_bits = &_bits[headerOffset];

        // get the image width and height
        _width = header.dwWidth;
        _height = header.dwHeight;

        // if this somehow one of these failed (they shouldn't), return failure
        if ((_bits == 0) || (_width == 0) || (_height == 0))
        {
            // texture load failed
            assert(false);
            return;
        }

        _sizeInBytes     = size;
        _imageBufferSize = size;

        _textureFormat = DXGI_FORMAT_BC7_UNORM;
    }
    else
    {
        std::cout << "invalid header." << std::endl;
        assert(false);
    }
}