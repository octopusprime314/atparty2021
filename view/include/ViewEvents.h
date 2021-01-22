/*
 * ViewEvents is part of the ReBoot distribution (https://github.com/octopusprime314/ReBoot.git).
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
 *  ViewEvents class. ViewEventDistributor event functions to be overriden
 */

#pragma once
#include "Matrix.h"
#include <functional>
#include <map>
#include <mutex>

class ViewEvents
{

    std::map<void*, std::function<void(Matrix)>> _projectionFuncs;
    std::map<void*, std::function<void(Matrix)>> _viewFuncs;
    std::mutex                                   _viewLock;


  public:
    // Use this call to connect functions to camera/view updates
    void subscribeToView(void* thisPointer, std::function<void(Matrix)> func);
    // Use this call to connect
    void subscribeToProjection(void* thisPointer, std::function<void(Matrix)> func);
    void unsubscribeToView(void* thisPointer);
    void unsubscribeToProjection(void* thisPointer);
    // Blast all subscribers that have overriden the updateView function
    void updateView(Matrix view);
    // Blast all subscribers that have overriden the updateProjection function
    void updateProjection(Matrix view);
};
