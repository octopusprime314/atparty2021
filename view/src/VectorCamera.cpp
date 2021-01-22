#define _USE_MATH_DEFINES
#include "VectorCamera.h"
#include "EngineManager.h"
#include "IOEventDistributor.h"
#include "Logger.h"

VectorCamera::VectorCamera() : Camera() {}

void VectorCamera::reset()
{
    auto state = getState();
    _vectorPath.resetState(state);
}

void VectorCamera::setInversion(const Matrix& inversion) { _vectorPath.setInversion(inversion); }

void VectorCamera::setVectors(const std::vector<PathVector>& waypoints)
{
    _vectorPath.resetVectors(waypoints);
    reset();
}

void VectorCamera::setVectorsFromFile(const std::string& file)
{
    _vectorPath.resetVectorsFromFile(file);
    reset();
}

void VectorCamera::updateState(int milliseconds)
{
    auto state = getState();
    _vectorPath.updateState(milliseconds, state);
}