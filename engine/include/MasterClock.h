/*
 * ModelFactory is part of the ReBoot distribution (https://github.com/octopusprime314/ReBoot.git).
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
 *  MasterClock class. A singleton responsible for updating certain parties
 *  who subscribe to various clock feeds.  A clock feed could be a 60 Hz screen
 *  refresh update that MasterClock would trigger an event every 16.7 milliSeconds.
 */
#pragma once
#include <chrono>
#include <functional>
#include <mutex>
#include <thread>
#include <map>
#include <vector>

class Entity;

const int TWENTY_FOUR_FPS    = 42; // frame time in milliseconds which is 24 frames per second
const int THIRTY_FPS         = 33;      // frame time in milliseconds which is 30 frames per second
const int DEFAULT_FRAME_TIME = 16; // frame time in milliseconds which is 60 frames per second
const int KINEMATICS_TIME    = 5;  // kinematics time in milliseconds

class MasterClock
{
    // Make constructor/destructor private so it can't be instantiated
    MasterClock();
    static MasterClock* _clock;

    // Clock feed subscriber's function pointers
    std::map<void*, std::function<void(int)>> _kinematicsRateFuncs;
    std::mutex                                _kinematicsRateLock;
    // Clock feed subscriber's function pointers
    std::map<void*, std::function<void(int)>> _animationRateFuncs;
    std::mutex                                _animationRateLock;
    // Clock feed subscriber's function pointers
    std::map<void*, std::function<void(int)>> _frameRateFuncs;
    std::mutex                                _frameRateLock;

    unsigned int _timeElapsedMilliseconds;
    std::thread* _animationThread;
    int          _animationTime;
    std::thread* _physicsThread;
    std::thread* _fpsThread;
    int          _frameTime;

    std::chrono::time_point<std::chrono::system_clock> _startTime;
    std::chrono::time_point<std::chrono::system_clock> _endTime;

    void _animationProcess();
    void _physicsProcess();
    void _fpsProcess();

  public:
    ~MasterClock();
    static MasterClock* instance();

    // Subscribe funcs
    void subscribeKinematicsRate(void* thisPointer, std::function<void(int)> func);
    void subscribeAnimationRate(void* thisPointer, std::function<void(int)> func);
    void subscribeFrameRate(void* thisPointer, std::function<void(int)> func);
    // Unsubscribe funcs
    void unsubscribeFrameRate(void* thisPointer);
    void unsubscribeAnimationRate(void* thisPointer);
    void unsubscribeKinematicsRate(void* thisPointer);
    // Gives programmer adjustable framerate
    void         setFrameRate(int framesPerSecond);
    int          getElapsedMilliseconds();
    void         update(int milliseconds);
    unsigned int getGameTime();
    void         start();
    // Kicks off the master clock thread that will asynchronously updates subscribers with clock
    // events
    void run();
};