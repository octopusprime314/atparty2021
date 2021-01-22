#include "Shader.h"
#include "DXLayer.h"
#include "EngineManager.h"
#include "Entity.h"
#include "Light.h"
#include <fstream>
#include <iostream>

// You can hit this in a debugger.
// Set to 'true' to printf every shader that is linked or compiled.
static volatile bool g_VerboseShaders = false;

Shader::Shader() {}

Shader::~Shader() {}

Shader::Shader(const Shader& shader) { *this = shader; }

bool Shader::_fileExists(const std::string& name)
{
    std::ifstream f(name.c_str());
    return f.good();
}
