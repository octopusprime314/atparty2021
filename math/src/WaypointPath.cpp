#include "WaypointPath.h"
#include "IOEvents.h"
#include "Logger.h"
#include "ShaderBroker.h"
#include <cmath>
#include <fstream>
#include <iterator>
#include <sstream>
#include <algorithm>

// Refactor to prop struct
WaypointPath::WaypointPath(const std::string& name, bool visualize, bool accelerate)
    : _name(name), _accelerate(accelerate)
{
    _name            = name;
    _accelerate      = accelerate;
    _elapsedTime     = 0;
    _currentWaypoint = 0;

}

WaypointPath::WaypointPath(const std::string& name, const std::string& pathFile, bool visualize,
                           bool accelerate)
    : _name(name), _accelerate(accelerate)
{
    _name = name;
    _loadWaypointsFromFile(pathFile);
    _elapsedTime     = 0;
    _currentWaypoint = 0;

}

WaypointPath::WaypointPath(const std::string& name, Vector4 finalPos, Vector4 finalRotation,
                           float time, bool visualize,
                           bool accelerate)
    : _name(name), _accelerate(accelerate)
{
    _name = name;
    _waypoints.emplace_back(finalPos, finalRotation, Vector4(1.0, 1.0, 1.0), time, Matrix());
    _elapsedTime     = 0;
    _currentWaypoint = 0;
}

WaypointPath::WaypointPath(const std::string& name, const std::vector<PathWaypoint>& waypoints,
                           bool visualize, bool accelerate)
    : _name(name), _accelerate(accelerate), _waypoints(waypoints)
{
    _name            = name;
    _elapsedTime     = 0;
    _currentWaypoint = 0;
}

float WaypointPath::ConvertToDegrees(float radian) { return (180.0f / PI) * radian; }

float WaypointPath::GetRoll(Vector4 q)
{
    float x = ConvertToDegrees(atan2(2 * q.getx() * q.getw() - 2 * q.gety() * q.getz(),
                                     1 - 2 * pow(q.getx(), 2.0f) - 2 * pow(q.getz(), 2.0f)));

    if (q.getx() * q.gety() + q.getz() * q.getw() == 0.5)
    {
        x = ConvertToDegrees((float)(2 * atan2(q.getx(), q.getw())));
    }

    else if (q.getx() * q.gety() + q.getz() * q.getw() == -0.5)
    {
        x = ConvertToDegrees((float)(-2 * atan2(q.getx(), q.getw())));
    }
    return x;
}

float WaypointPath::GetPitch(Vector4 q)
{
    float y = ConvertToDegrees(atan2(2 * q.gety() * q.getw() - 2 * q.getx() * q.getz(),
                                     1 - 2 * pow(q.gety(), 2.0f) - 2 * pow(q.getz(), 2.0f)));
    if (q.getx() * q.gety() + q.getz() * q.getw() == 0.5)
    {
        y = 0;
    }
    else if (q.getx() * q.gety() + q.getz() * q.getw() == -0.5)
    {
        y = 0;
    }
    return y;
}

float WaypointPath::GetYaw(Vector4 q)
{
    float z = ConvertToDegrees(
        asin(std::clamp(2 * q.getx() * q.gety() + 2 * q.getz() * q.getw(), -1.0f, 1.0f)));
    return z;
}

void WaypointPath::updateState(int milliseconds, StateVector* state)
{
    /*if (_currentWaypoint != -1 && _currentWaypoint < _waypoints.size())
    {
        if (_elapsedTime >= _waypoints[_currentWaypoint].time)
        {
            _decelerating = false;
            _elapsedTime  = 0;
            _currentWaypoint++;
            state->setForce(Vector4(0.0, 0.0, 0.0));
            state->setTorque(Vector4(0.0, 0.0, 0.0));
            state->setLinearVelocity(Vector4(0.0, 0.0, 0.0));
            state->setLinearAcceleration(Vector4(0.0, 0.0, 0.0));
            state->setAngularVelocity(Vector4(0.0, 0.0, 0.0));
            state->setAngularAcceleration(Vector4(0.0, 0.0, 0.0));

            if (_currentWaypoint >= _waypoints.size())
            {
                _currentWaypoint = -1;
                return;
            }

            _calculateVelocities(state);
        }
        _elapsedTime += milliseconds;
        if (!_accelerate)
        {
            state->setLinearVelocity(_currentVelocity);
            state->setAngularVelocity(_currentRotation);
        }
        else
        {
            state->setForce(_currentLinearAcceleration);
            state->setTorque(_currentAngularAcceleration);
        }
        state->update(milliseconds);
    }
    else
    {*/
     
    if (_currentWaypoint != -1 && _currentWaypoint < _waypoints.size())
    {
        if (_elapsedTime >= _waypoints[_currentWaypoint].time)
        {
            _currentWaypoint++;

            if (_currentWaypoint >= _waypoints.size())
            {
                _currentWaypoint = -1;
                
                _currentWaypoint = 0;
                _elapsedTime     = 0;

                return;
            }

            for (int i = _currentWaypoint; i < _waypoints.size(); i++)
            {
                if (_elapsedTime > _waypoints[i].time)
                {
                    _currentWaypoint = i;
                    break;
                }
            }

            //state->setLinearPosition(_waypoints[_currentWaypoint].position);
            //state->setAngularPosition(_waypoints[_currentWaypoint].rotation);

            state->setTransform(_waypoints[_currentWaypoint].transform);
        }
        _elapsedTime += milliseconds;
    }

    if (_currentWaypoint == _waypoints.size())
    {
        _currentWaypoint = 0;
        _elapsedTime     = 0;
    }
}

bool WaypointPath::isMoving() 
{
    return ((_currentWaypoint == -1) ? false : true);
}

void WaypointPath::resetWaypointsFromFile(const std::string& pathFile)
{
    _loadWaypointsFromFile(pathFile);
    _elapsedTime     = 0;
    _currentWaypoint = -1;
}

void WaypointPath::resetWaypoints(const std::vector<PathWaypoint>& waypoints)
{
    _waypoints       = waypoints;
    _elapsedTime     = 0;
    _currentWaypoint = -1;
}

void WaypointPath::resetState(StateVector* state)
{
    _decelerating    = false;
    _currentWaypoint = -1;
    _elapsedTime     = 0;
    _currentVelocity = Vector4();
    _currentRotation = Vector4();
    _initialPosition = state->getLinearPosition();
    if (_waypoints.size() > 0)
    {
        _waypoints[0].linearVelocity =
            (_waypoints[0].position - _initialPosition) / (_waypoints.back().time / 1000);
        _currentWaypoint = 0;
        _calculateVelocities(state);
    }
}

void WaypointPath::_loadWaypointsFromFile(const std::string& file)
{
    _waypoints.clear();
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
        Vector4 s(1.0, 1.0, 1.0);
        _waypoints.emplace_back(d, r, s, time, Matrix());

        // Probably don't know initial position for first entry
        if (_waypoints.size() > 1)
        {
            auto&   prev = _waypoints[_waypoints.size() - 2];
            Vector4 velocity =
                (_waypoints.back().position - prev.position) / (_waypoints.back().time / 1000);
            _waypoints.back().linearVelocity = velocity;
        }
    }
}

void WaypointPath::updateView(const Matrix& view) { _view = view; }

void WaypointPath::updateProjection(const Matrix& projection) { _projection = projection; }

void WaypointPath::_drawPath()
{
}

void WaypointPath::_calculateVelocities(StateVector* state)
{
    auto& wp = _waypoints[_currentWaypoint];
    if (_accelerate)
    {
        float time2                 = (wp.time / 1000) * (wp.time / 1000);
        _currentLinearAcceleration  = ((wp.position - state->getLinearPosition()) * 2) / time2;
        _currentAngularAcceleration = ((wp.rotation - state->getAngularPosition()) * 2) / time2;

        state->setForce(_currentLinearAcceleration);
        state->setTorque(_currentAngularAcceleration);
    }
    else
    {
        _currentVelocity = (wp.position - state->getLinearPosition()) / (wp.time / 1000);
        _currentRotation = (wp.rotation - state->getAngularPosition()) / (wp.time / 1000);

        state->setLinearVelocity(_currentVelocity);
        state->setAngularVelocity(_currentRotation);
    }
}