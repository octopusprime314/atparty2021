#include "IOEvents.h"
#include "EngineManager.h"
#include "ShaderBroker.h"

std::map<void*, std::function<void(int, int, int)>>      IOEvents::_keyboardReleaseFuncs;
std::map<void*, std::function<void(int, int, int, int)>> IOEvents::_mouseButtonFuncs;
std::map<void*, std::function<void(EngineStateFlags)>>   IOEvents::_gameStateFuncs;
std::map<void*, std::function<void(int, int, int)>>      IOEvents::_keyboardFuncs;
std::map<void*, std::function<void(double, double)>>     IOEvents::_mouseFuncs;
std::map<void*, std::function<void()>>                   IOEvents::_drawFuncs;

std::function<void()>                                IOEvents::_viewUpdateDrawFunc;
std::function<void()>                                IOEvents::_preDrawCallback;
std::function<void()>                                IOEvents::_postDrawCallback;
std::mutex                                           IOEvents::_eventLock;


// Use this call to connect functions to key updates
void IOEvents::subscribeToKeyboard(void* thisPointer, std::function<void(int, int, int)> func)
{
    _eventLock.lock();
    _keyboardFuncs[thisPointer] = func;
    _eventLock.unlock();
}
// Use this call to connect functions to key updates
void IOEvents::subscribeToReleaseKeyboard(void* thisPointer,std::function<void(int, int, int)> func)
{
    _eventLock.lock();
    _keyboardReleaseFuncs[thisPointer] = func;
    _eventLock.unlock();
}
// Use this call to connect functions to mouse updates
void IOEvents::subscribeToMouse(void* thisPointer, std::function<void(double, double)> func)
{
    _eventLock.lock();
    _mouseFuncs[thisPointer] = func;
    _eventLock.unlock();
}
// Use this call to connect functions to draw updates
void IOEvents::subscribeToDraw(void* thisPointer, std::function<void()> func)
{
    _eventLock.lock();
    _drawFuncs[thisPointer] = func;
    _eventLock.unlock();
}
// Use this call to connect functions to key updates
void IOEvents::subscribeToGameState(void* thisPointer, std::function<void(EngineStateFlags)> func)
{
    _eventLock.lock();
    _gameStateFuncs[thisPointer] = func;
    _eventLock.unlock();
}
// Use this call to connect functions to mouse button updates
void IOEvents::subscribeToMouseClick(void*                                   thisPointer,
                                     std::function<void(int, int, int, int)> func)
{
    _eventLock.lock();
    _mouseButtonFuncs[thisPointer] = func;
    _eventLock.unlock();
}

void IOEvents::unsubscribeToKeyboard(void* thisPointer)
{
    _eventLock.lock();
    _keyboardFuncs.erase(thisPointer);
    _eventLock.unlock();
}
void IOEvents::unsubscribeToReleaseKeyboard(void* thisPointer)
{
    _eventLock.lock();
    _keyboardReleaseFuncs.erase(thisPointer);
    _eventLock.unlock();
}
void IOEvents::unsubscribeToMouse(void* thisPointer)
{
    _eventLock.lock();
    _mouseFuncs.erase(thisPointer);
    _eventLock.unlock();
}
void IOEvents::unsubscribeToDraw(void* thisPointer)
{
    _eventLock.lock();
    _drawFuncs.erase(thisPointer);
    _eventLock.unlock();
}
void IOEvents::unsubscribeToGameState(void* thisPointer)
{
    _eventLock.lock();
    _gameStateFuncs.erase(thisPointer);
    _eventLock.unlock();
}
void IOEvents::unsubscribeToMouseClick(void* thisPointer)
{
    _eventLock.lock();
    _mouseButtonFuncs.erase(thisPointer);
    _eventLock.unlock();
}

void IOEvents::setPreDrawCallback(std::function<void()> func) { _preDrawCallback = func; }
void IOEvents::setPostDrawCallback(std::function<void()> func) { _postDrawCallback = func; }

// All keyboard input from glfw will be notified here
void IOEvents::updateKeyboard(int key, int x, int y)
{
    _eventLock.lock();
    for (auto func : _keyboardFuncs)
    {
        if (func.second != nullptr)
        {
            func.second(key, x, y);
        }
    }
    _eventLock.unlock();
}

// All keyboard input from glfw will be notified here
void IOEvents::releaseKeyboard(int key, int x, int y)
{
    _eventLock.lock();
    for (auto func : _keyboardReleaseFuncs)
    {
        if (func.second != nullptr)
        {
            func.second(key, x, y);
        }
    }
    _eventLock.unlock();
}

// One frame draw update call
void IOEvents::updateDraw(GLFWwindow* _window)
{

    // Call scene manager to go any global operations before drawing
    _preDrawCallback();

    if (EngineManager::getGraphicsLayer() != GraphicsLayer::DXR_PATHTRACER)
    {
        _eventLock.lock();
        for (auto func : _drawFuncs)
        {
            if (func.second != nullptr)
            {
                func.second();
            }
        }
        _eventLock.unlock();
    }
    else
    {
        // Only call view update for path tracer
        _viewUpdateDrawFunc();
    }

    // Call scene manager to go any global operations after drawing
    _postDrawCallback();
}

// All mouse movement input will be notified here
void IOEvents::updateMouse(double x, double y)
{
    _eventLock.lock();
    for (auto func : _mouseFuncs)
    {
        if (func.second != nullptr)
        {
            func.second(x, y);
        }
    }
    _eventLock.unlock();
}

// All mouse button input will be notified here
void IOEvents::updateMouseClick(int button, int action, int x, int y)
{
    _eventLock.lock();
    for (auto func : _mouseButtonFuncs)
    {
        if (func.second != nullptr)
        {
            func.second(button, action, x, y);
        }
    }
    _eventLock.unlock();
}

void IOEvents::updateGameState(EngineStateFlags state)
{
    _eventLock.lock();
    // first update static game state
    EngineState::setEngineState(state);
    for (auto func : _gameStateFuncs)
    {
        if (func.second != nullptr)
        {
            func.second(state);
        }
    }
    _eventLock.unlock();
}
