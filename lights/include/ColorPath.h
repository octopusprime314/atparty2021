#pragma once
#include "Matrix.h"
#include <string>
#include <vector>

struct ColorVector
{
    Vector4 color;
    float   time;

    ColorVector(const Vector4& c, float t) : color(c), time(t) {}
};

class ColorPath
{
  public:
    ColorPath();
    ColorPath(const std::string& pathFile);
    ColorPath(const std::vector<ColorVector>& vectors);

    void resetVectorsFromFile(const std::string& pathFile);
    void resetVectors(const std::vector<ColorVector>& vectors);
    void updateColor(int milliseconds, Vector4& color);

  private:
    void                     _loadVectorsFromFile(const std::string& file);
    int                      _elapsedTime;
    int                      _currentVector;
    Vector4                  _diffColor;
    float                    _diffAlpha;
    std::vector<ColorVector> _vectors;
};