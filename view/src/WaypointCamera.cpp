#define _USE_MATH_DEFINES
#include "WaypointCamera.h"
#include "EngineManager.h"
#include "Logger.h"

WaypointCamera::WaypointCamera() : Camera()
{
    _elapsedTime     = 0;
    _currentWaypoint = 0;
}

void WaypointCamera::reset()
{
    auto state       = getState();
    _deceleratingX   = false;
    _deceleratingY   = false;
    _deceleratingZ   = false;
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

void WaypointCamera::setInversion(const Matrix& inversion) { _inversion = inversion; }

void WaypointCamera::setWaypoints(const std::vector<PathWaypoint>& waypoints)
{
    _wayPointType    = WaypointType::LinearWaypoints;
    _waypoints       = waypoints;
    _elapsedTime     = 0;
    _currentWaypoint = -1;
    reset();
}

void WaypointCamera::setWaypointsFromFile(const std::string& file)
{
    _wayPointType = WaypointType::AcceleratingWaypoints;
    _loadWaypointsFromFile(file);
    _elapsedTime     = 0;
    _currentWaypoint = -1;
    reset();
}

void WaypointCamera::updateState(int milliseconds)
{
    auto state = getState();
    if (_wayPointType == WaypointType::AcceleratingWaypoints)
    {
        if (_currentWaypoint != -1 && _currentWaypoint < _waypoints.size())
        {
            if ((_deceleratingX || _deceleratingY || _deceleratingZ) && (_elapsedTime >= 1000))
            {
                if (_deceleratingX)
                {
                    _currentAngularAcceleration = Vector4(0, _currentAngularAcceleration.gety(),
                                                          _currentAngularAcceleration.getz(), 1.0);
                }
                if (_deceleratingY)
                {
                    _currentAngularAcceleration = Vector4(_currentAngularAcceleration.getx(), 0,
                                                          _currentAngularAcceleration.getz(), 1.0);
                    state->setAngularVelocity(Vector4(0, 0, 0, 1));
                }
                if (_deceleratingZ)
                {
                    _currentAngularAcceleration = Vector4(_currentAngularAcceleration.getx(),
                                                          _currentAngularAcceleration.gety(), 0, 1.0);
                }
                _deceleratingX = false;
                _deceleratingY = false;
                _deceleratingZ = false;
            }
            if (_elapsedTime >= _waypoints[_currentWaypoint].time)
            {
                _deceleratingX = false;
                _deceleratingY = false;
                _deceleratingZ = false;
                _elapsedTime   = 0;
                _currentWaypoint++;

                if (_currentWaypoint >= _waypoints.size())
                {
                    _currentWaypoint = -1;
                    return;
                }

                _calculateVelocities(state);
            }
            _elapsedTime += milliseconds;
            state->setForce(_currentLinearAcceleration);
            state->setTorque(_currentAngularAcceleration);
            state->update(milliseconds);
        }
        _elapsedTime += milliseconds;
        state->setForce(_currentLinearAcceleration);
        state->setTorque(_currentAngularAcceleration);
        state->update(milliseconds);
    }
    else if (_wayPointType == WaypointType::LinearWaypoints)
    {
        if (_currentWaypoint != -1 && _currentWaypoint < _waypoints.size())
        {
            if (_elapsedTime >= _waypoints[_currentWaypoint].time)
            {
                _currentWaypoint++;

                if (_currentWaypoint >= _waypoints.size())
                {
                    _currentWaypoint = -1;
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

                state->setLinearPosition(_waypoints[_currentWaypoint].position);
                state->setAngularPosition(_waypoints[_currentWaypoint].rotation);
            }
            _elapsedTime += milliseconds;
        }
    }
}

void WaypointCamera::_calculateVelocities(StateVector* state)
{
    auto&   wp      = _waypoints[_currentWaypoint];
    float   time2   = (wp.time / 1000) * (wp.time / 1000);
    Vector4 tempvel = (state->getLinearVelocity() * 2) / (wp.time / 1000);
    _currentLinearAcceleration =
        (((wp.position - state->getLinearPosition()) * 2) / time2) - tempvel;

    tempvel = (state->getAngularVelocity() * 2) / (wp.time / 1000);
    _currentAngularAcceleration =
        (((wp.rotation - state->getAngularPosition()) * 2) / time2) - tempvel;
    if (_currentWaypoint > 0)
    {
        float x       = _currentAngularAcceleration.getx();
        float y       = _currentAngularAcceleration.gety();
        float z       = _currentAngularAcceleration.getz();
        auto  prevpos = _waypoints[_currentWaypoint - 1].rotation;
        if (wp.rotation.getx() - prevpos.getx() == 0)
        {
            _deceleratingX = true;
            x              = -state->getAngularVelocity().getx();
        }
        if (wp.rotation.gety() - prevpos.gety() == 0)
        {
            _deceleratingY = true;
            y              = -state->getAngularVelocity().gety();
        }
        if (wp.rotation.getz() - prevpos.getz() == 0)
        {
            _deceleratingZ = true;
            z              = -state->getAngularVelocity().getz();
        }
        _currentAngularAcceleration = Vector4(x, y, z, 1.0);
    }
    state->setForce(_currentLinearAcceleration);
    state->setTorque(_currentAngularAcceleration);
}

void WaypointCamera::_loadWaypointsFromFile(const std::string& file)
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
        _waypoints.emplace_back(d, r, s, time);

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