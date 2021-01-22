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
#include "VectorPath.h"

class VectorCamera : public Camera
{
  public:
    VectorCamera();

    void         reset();
    void         setInversion(const Matrix& inversion);
    void         setVectorsFromFile(const std::string& file);
    void         setVectors(const std::vector<PathVector>& vectors);
    virtual void updateState(int milliseconds);

  private:
    VectorPath _vectorPath;
};