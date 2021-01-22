#pragma once
#include "MVP.h"
#include "Matrix.h"
#include "StateVector.h"
#include <string>
#include <vector>

struct PathWaypoint
{
    Vector4 position;
    Vector4 rotation;
    Vector4 linearVelocity; // calculated
    float   time;

    PathWaypoint(const Vector4& p, const Vector4& r, float t) : position(p), rotation(r), time(t) {}
};

class WaypointPath
{
  public:
    WaypointPath(const std::string& name, bool visualize = false, bool accelerate = false);
    WaypointPath(const std::string& name, const std::string& pathFile, bool visualize = false,
                 bool accelerate = false);
    WaypointPath(const std::string& name, const std::vector<PathWaypoint>& vectors,
                 bool visualize = false, bool accelerate = false);

    WaypointPath(const std::string& name, Vector4 finalPos, Vector4 finalRotation,
                 float time, bool visualize = false, bool accelerate = false);

    void resetWaypointsFromFile(const std::string& pathFile);
    void resetWaypoints(const std::vector<PathWaypoint>& vectors);
    void updateState(int milliseconds, StateVector* state);
    void setInversion(const Matrix& inversion) { _inversion = inversion; }

    void resetState(StateVector* state);

    void updateView(const Matrix& view);             // used with visualization
    void updateProjection(const Matrix& projection); // used with visualization
    bool isMoving();
  private:
    void        _loadWaypointsFromFile(const std::string& file);
    void        _drawPath();
    void        _calculateVelocities(StateVector* state);
    std::string _name;

    bool                      _accelerate;
    bool                      _decelerating;
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
};