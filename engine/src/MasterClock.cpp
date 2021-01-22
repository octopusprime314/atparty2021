#include "MasterClock.h"
#include <ctime>
#include <iostream>
#include <wrl.h>
#include "Entity.h"
MasterClock* MasterClock::_clock = nullptr;

MasterClock::MasterClock()
    : _frameTime(DEFAULT_FRAME_TIME), _animationTime(DEFAULT_FRAME_TIME),
      _timeElapsedMilliseconds(0)
{
}

MasterClock::~MasterClock() {}

MasterClock* MasterClock::instance()
{
    if (_clock == nullptr)
    {
        _clock = new MasterClock();
    }
    return _clock;
}

void MasterClock::run()
{
    // Run the clock event processes that are responsible for sending time events to subscribers
    //_physicsThread   = new std::thread(&MasterClock::_physicsProcess, _clock);
    //_fpsThread       = new std::thread(&MasterClock::_fpsProcess, _clock);
    _animationThread = new std::thread(&MasterClock::_animationProcess, _clock);
}

void MasterClock::start() { _startTime = std::chrono::system_clock::now(); }

int MasterClock::getElapsedMilliseconds()
{
    _endTime = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(_endTime - _startTime).count();
}

unsigned int MasterClock::getGameTime() { return _timeElapsedMilliseconds; }

void MasterClock::update(int milliseconds)
{
    _kinematicsRateLock.lock();
    for (auto func : _kinematicsRateFuncs)
    {
        if (func.second != nullptr)
        {
            func.second(milliseconds);
        }
    }
    _kinematicsRateLock.unlock();

    _timeElapsedMilliseconds += milliseconds;

    // Keep this code if we need to debug timing again!
    if (_timeElapsedMilliseconds % 1000 > 0 || _timeElapsedMilliseconds % 1000 < 20)
    {
        char data[256];
        sprintf(data, "Real time: %i, Game Time: %i\n",
                MasterClock::instance()->getElapsedMilliseconds(), _timeElapsedMilliseconds);
        // OutputDebugStringA(data);
    }
}

void MasterClock::_physicsProcess()
{
    int milliSecondCounter = 0;
    while (true)
    {
        auto start = std::chrono::high_resolution_clock::now();
        // If the millisecond amount is divisible by kinematics time,
        // then trigger a kinematic calculation time event to subscribers
        if (milliSecondCounter == KINEMATICS_TIME)
        {
            _kinematicsRateLock.lock();
            milliSecondCounter = 0;
            for (auto func : _kinematicsRateFuncs)
            {
                if (func.second != nullptr)
                {
                    func.second(KINEMATICS_TIME);
                }
            }
            _kinematicsRateLock.unlock();
        }
        auto   end          = std::chrono::high_resolution_clock::now();
        double milliseconds = std::chrono::duration<double, std::milli>(end - start).count();
        int    deltaTime    = static_cast<int>(static_cast<double>(KINEMATICS_TIME) - milliseconds);
        if (deltaTime > 0)
        {
            // std::cout << "left over time: " << deltaTime << std::endl;
            // Wait for remaining milliseconds
            std::this_thread::sleep_for(std::chrono::milliseconds(deltaTime));
        }
        else if (deltaTime < 0)
        {
            std::cout << "Extra time being used on physics calculations: "
                      << milliseconds - KINEMATICS_TIME << std::endl;
        }
        milliSecondCounter += KINEMATICS_TIME;
    }
}

void MasterClock::_fpsProcess()
{
    int milliSecondCounter = 0;
    while (true)
    {
        auto start = std::chrono::high_resolution_clock::now();
        // If the millisecond amount is divisible by frame time then trigger a frame time event to
        // subscribers
        if (milliSecondCounter == _frameTime)
        {
            _frameRateLock.lock();
            milliSecondCounter = 0;
            for (auto func : _frameRateFuncs)
            {
                if (func.second != nullptr)
                {
                    func.second(_frameTime);
                }
            }
            _frameRateLock.unlock();
        }
        auto end = std::chrono::high_resolution_clock::now();
        if (std::chrono::duration<double, std::milli>(end - start).count() <= 1.0f)
        {
            // Wait for a millisecond
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            milliSecondCounter++;
        }
    }
}

void MasterClock::_animationProcess()
{
    int milliSecondCounter = 0;
    while (true)
    {
        auto start = std::chrono::high_resolution_clock::now();
        // If the millisecond amount is divisible by frame time then trigger a frame time event to
        // subscribers
        if (milliSecondCounter == _animationTime)
        {
            _animationRateLock.lock();
            milliSecondCounter = 0;
            for (auto func : _animationRateFuncs)
            {
                if (func.second != nullptr)
                {
                    func.second(_animationTime);
                }
            }
            _animationRateLock.unlock();
        }
        auto end = std::chrono::high_resolution_clock::now();
        if (std::chrono::duration<double, std::milli>(end - start).count() <= 1.0f)
        {
            // Wait for a millisecond
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            milliSecondCounter++;
        }
    }
}

void MasterClock::setFrameRate(int framesPerSecond)
{
    _frameTime = static_cast<int>((1.0 / static_cast<double>(framesPerSecond)) * 1000.0);
}

void MasterClock::subscribeFrameRate(void* thisPointer, std::function<void(int)> func)
{
    _frameRateLock.lock();
    _frameRateFuncs[thisPointer] = func;
    _frameRateLock.unlock();
}

void MasterClock::subscribeAnimationRate(void* thisPointer, std::function<void(int)> func)
{
    _animationRateLock.lock();
    _animationRateFuncs[thisPointer] = func;
    _animationRateLock.unlock();
}

void MasterClock::subscribeKinematicsRate(void* thisPointer, std::function<void(int)> func)
{
    _kinematicsRateLock.lock();
    _kinematicsRateFuncs[thisPointer] = func;
    _kinematicsRateLock.unlock();
}

void MasterClock::unsubscribeFrameRate(void* thisPointer)
{
    _frameRateLock.lock();
    _frameRateFuncs.erase(thisPointer);
    _frameRateLock.unlock();
}

void MasterClock::unsubscribeAnimationRate(void* thisPointer)
{
    _animationRateLock.lock();
    _animationRateFuncs.erase(thisPointer);
    _animationRateLock.unlock();
}

void MasterClock::unsubscribeKinematicsRate(void* thisPointer)
{
    _kinematicsRateLock.lock();
    _kinematicsRateFuncs.erase(thisPointer);
    _kinematicsRateLock.unlock();
}
