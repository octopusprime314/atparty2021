/*
 * Matrix is part of the ReBoot distribution (https://github.com/octopusprime314/ReBoot.git).
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
 *  Matrix class. Left Handed Coordinate System from client point of view!
 *  Handles matrix transforms
 */

#pragma once
#include "Vector4.h"
#include <math.h>

const float PI          = 3.14159265f;
const float PI_OVER_180 = PI / 180.0f;
#define MATRIX_SIZE 25

class Matrix
{

    // Matrix layout
    // P is a dimensional position vector in x,y,z
    // X coordinate vector
    // Y coordinate vector
    // Z coordinate vector
    //| Xx Xy Xz Px |
    //| Yx Yy Yz Py |
    //| Zx Zy Zz Pz |
    //| 0  0  0  1  |

    // To Create a Translation Matrix Change P
    // To Create a Rotation Matrix Change X,Y and Z
    // To Create a Scaling Matrix Change Xx, Yy, Zz

    // Translation Matrix of a +5 change in X position
    //| 1 0 0 5 |
    //| 0 1 0 0 |
    //| 0 0 1 0 |
    //| 0 0 0 1 |

    // Rotation Matrix of a theta change around X axis
    //| 1  0    0   0 |
    //| 0 cos -sin  0 |
    //| 0 sin  cos  0 |
    //| 0  0    0   1 |

    // Rotation Matrix of a theta change around Y axis
    //| cos 0  sin  0 |
    //|  0  1   0   0 |
    //|-sin 0  cos  0 |
    //|  0  0   0   1 |

    // Rotation Matrix of a theta change around Z axis
    //| cos -sin 0  0 |
    //| sin  cos 0  0 |
    //|  0    0  1  0 |
    //|  0    0  0  1 |

    // Scale Matrix of 2
    //| 2 0 0 0 |
    //| 0 2 0 0 |
    //| 0 0 2 0 |
    //| 0 0 0 1 |

    friend class Uniforms;

    // Extend to 25 to house the 4x4 matrix and also
    // the near, far, left, right, top, bottom, angle, aspect ratio and if inverted
    float _matrix[MATRIX_SIZE];

    static Matrix convertToRightHanded(Matrix leftHandedMatrix, bool isViewMatrix);
    Matrix(float* mat);

  public:
    Matrix();
    float*  getFlatBuffer();
    Matrix  transpose();
    Matrix  inverse();
    void    display();
    Matrix  operator*(Matrix mat);
    Vector4 operator*(Vector4 vec);
    Matrix  operator*(double scale);
    Matrix  operator*(float scale);
    Matrix  operator+(Matrix mat);
    Matrix  operator-(Matrix mat);

    static Matrix rotationAroundX(float degrees);
    static Matrix rotationAroundY(float degrees);
    static Matrix rotationAroundZ(float degrees);
    static Matrix translation(float x, float y, float z);
    static Matrix projection(float angleOfView, float aspectRatio, float near, float far);
    static Matrix scale(float scalar);
    static Matrix scale(float x, float y, float z);
    static Matrix ortho(float orthoWidth, float orthoHeight, float n, float f);
    static Matrix ortho(float l, float r, float t, float b, float n, float f);
};