#include "VectorPath.h"
#include <fstream>
#include <iterator>
#include <sstream>

VectorPath::VectorPath()
{
    _elapsedTime   = 0;
    _currentVector = 0;
}

VectorPath::VectorPath(const std::string& pathFile)
{
    _loadVectorsFromFile(pathFile);
    _elapsedTime   = 0;
    _currentVector = 0;
}

VectorPath::VectorPath(const std::vector<PathVector>& vectors) : _vectors(vectors)
{
    _elapsedTime   = 0;
    _currentVector = 0;
}

void VectorPath::updateState(int milliseconds, StateVector* state, bool clearVelocity)
{
    if (_currentVector != -1 && _currentVector < _vectors.size())
    {
        if (clearVelocity)
        {
            state->setForce(Vector4(0.0, 0.0, 0.0));
            state->setTorque(Vector4(0.0, 0.0, 0.0));
            state->setAngularAcceleration(Vector4(0.0, 0.0, 0.0));
            state->setAngularVelocity(Vector4(0.0, 0.0, 0.0));
            state->setLinearAcceleration(Vector4(0.0, 0.0, 0.0));
            state->setLinearVelocity(Vector4(0.0, 0.0, 0.0));
        }
        // If we also have waypoints, we dont want to double add the velocity
        if (_elapsedTime >= _vectors[_currentVector].time)
        {
            _elapsedTime = 0;
            _currentVector++;

            if (_currentVector >= _vectors.size())
            {
                _currentVector = -1;
                state->setForce(Vector4(0.0, 0.0, 0.0));
                state->setLinearAcceleration(Vector4(0.0, 0.0, 0.0));
                state->setLinearVelocity(Vector4(0.0, 0.0, 0.0));
                state->setAngularAcceleration(Vector4(0.0, 0.0, 0.0));
                state->setAngularVelocity(Vector4(0.0, 0.0, 0.0));
                state->setTorque(Vector4(0.0, 0.0, 0.0));
                return;
            }
        }
        _elapsedTime += milliseconds;

        state->setForce(_vectors[_currentVector].direction);
        state->setTorque(_vectors[_currentVector].rotation);
        state->update(milliseconds);
        if (clearVelocity)
        {
            state->setForce(Vector4(0.0, 0.0, 0.0));
            state->setTorque(Vector4(0.0, 0.0, 0.0));
            state->setAngularAcceleration(Vector4(0.0, 0.0, 0.0));
            state->setAngularVelocity(Vector4(0.0, 0.0, 0.0));
            state->setLinearAcceleration(Vector4(0.0, 0.0, 0.0));
            state->setLinearVelocity(Vector4(0.0, 0.0, 0.0));
        }
    }
}

void VectorPath::resetVectorsFromFile(const std::string& pathFile)
{
    _loadVectorsFromFile(pathFile);
    _elapsedTime   = 0;
    _currentVector = 0;
}

void VectorPath::resetVectors(const std::vector<PathVector>& vectors)
{
    _vectors       = vectors;
    _elapsedTime   = 0;
    _currentVector = 0;
}

void VectorPath::resetState(StateVector* state)
{
    _currentVector = -1;
    _elapsedTime   = 0;
    if (_vectors.size() > 0)
    {
        _currentVector = 0;
        state->setForce(_inversion * _vectors[_currentVector].direction);
        state->setTorque(_vectors[_currentVector].rotation);
    }
}

void VectorPath::_loadVectorsFromFile(const std::string& file)
{
    _vectors.clear();
    std::ifstream infile(file);

    double      v_x, v_y, v_z, r_x, r_y, r_z;
    float       time;
    std::string line;
    while (std::getline(infile, line))
    {
        if (line.size() == 0)
            continue;
        std::stringstream                  ss(line);
        std::istream_iterator<std::string> begin(ss);
        std::istream_iterator<std::string> end;
        std::vector<std::string>           vstrings(begin, end);
        std::copy(vstrings.begin(), vstrings.end(),
                  std::ostream_iterator<std::string>(std::cout, "\n"));

        v_x = ::atof(vstrings[0].c_str());
        v_y = ::atof(vstrings[1].c_str());
        v_z = ::atof(vstrings[2].c_str());

        r_x = ::atof(vstrings[3].c_str());
        r_y = ::atof(vstrings[4].c_str());
        r_z = ::atof(vstrings[5].c_str());

        time = static_cast<float>(::atof(vstrings[6].c_str()));
        Vector4 d(static_cast<float>(v_x), static_cast<float>(v_y), static_cast<float>(v_z));
        Vector4 r(static_cast<float>(r_x), static_cast<float>(r_y), static_cast<float>(r_z));
        _vectors.emplace_back(d, r, time);
    }
}
