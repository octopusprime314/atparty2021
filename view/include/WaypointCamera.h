/*
 * Camera is part of the ReBoot distribution (https://github.com/octopusprime314/ReBoot.git).
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
 *  WaypointCamera class.
 */

#pragma once
#include "Camera.h"
#include "WaypointPath.h"

enum class WaypointType
{
    AcceleratingWaypoints,
    LinearWaypoints
};

class WaypointCamera : public Camera
{
  public:
    WaypointCamera();

    void         reset();
    void         setInversion(const Matrix& inversion);
    void         setWaypointsFromFile(const std::string& file);
    void         setWaypoints(const std::vector<PathWaypoint>& waypoints);
    int          getCurrentWaypointIdx();
    virtual void updateState(int milliseconds);

  private:
    void        _loadWaypointsFromFile(const std::string& file);
    void        _calculateVelocities(StateVector* state);
    std::string _name;

    bool                      _visualize;
    bool                      _deceleratingX{false};
    bool                      _deceleratingY{false};
    bool                      _deceleratingZ{false};
    int                       _elapsedTime;
    int                       _currentWaypoint;
    Matrix                    _inversion;
    Vector4                   _currentVelocity;
    Vector4                   _currentLinearAcceleration;
    Vector4                   _currentAngularAcceleration;
    Vector4                   _currentRotation;
    Vector4                   _initialPosition;
    std::vector<PathWaypoint> _waypoints;
    Matrix                    _view;
    Matrix                    _projection;
    WaypointType              _wayPointType;

};