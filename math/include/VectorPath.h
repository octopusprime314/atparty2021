#pragma once
#include "Matrix.h"
#include "StateVector.h"
#include <string>
#include <vector>

struct PathVector
{
    Vector4 direction;
    Vector4 rotation;
    float   time;

    PathVector(const Vector4& d, const Vector4& r, float t) : direction(d), rotation(r), time(t) {}
};

class VectorPath
{
  public:
    VectorPath();
    VectorPath(const std::string& pathFile);
    VectorPath(const std::vector<PathVector>& vectors);

    void resetVectorsFromFile(const std::string& pathFile);
    void resetVectors(const std::vector<PathVector>& vectors);
    void updateState(int milliseconds, StateVector* state, bool clearVelocity = false);
    void setInversion(const Matrix& inversion) { _inversion = inversion; }

    void resetState(StateVector* state);

  private:
    void                    _loadVectorsFromFile(const std::string& file);
    int                     _elapsedTime;
    int                     _currentVector;
    Matrix                  _inversion;
    std::vector<PathVector> _vectors;
};