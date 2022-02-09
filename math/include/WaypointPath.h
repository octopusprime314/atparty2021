#pragma once
#include "MVP.h"
#include "Matrix.h"
#include "StateVector.h"
#include <string>
#include <vector>

struct PathWaypoint
{
    Matrix  transform;
    Vector4 position;
    Vector4 rotation;
    Vector4 scale;
    Vector4 linearVelocity; // calculated
    float   time;

    PathWaypoint() {}
    PathWaypoint(const Vector4& p, const Vector4& r, const Vector4& s, float t, Matrix trans) : position(p), rotation(r), scale(s), time(t), transform(trans) {}
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
    float       ConvertToDegrees(float radian);
    float       GetRoll(Vector4 q);
    float       GetPitch(Vector4 q);
    float       GetYaw(Vector4 q);


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