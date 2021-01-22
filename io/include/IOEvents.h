/*
 * IOEvents is part of the ReBoot distribution (https://github.com/octopusprime314/ReBoot.git).
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
 *  IOEvents class. Input event functions to be overriden
 */

#pragma once
#include "EngineState.h"
#include "Matrix.h"
#include <functional>
#include <map>
#include <mutex>

struct GLFWwindow;

class IOEvents
{

    static std::map<void*, std::function<void(int, int, int)>>      _keyboardReleaseFuncs;
    static std::map<void*, std::function<void(int, int, int, int)>> _mouseButtonFuncs;
    static std::map<void*, std::function<void(EngineStateFlags)>>   _gameStateFuncs;
    static std::map<void*, std::function<void(int, int, int)>>      _keyboardFuncs;
    static std::map<void*, std::function<void(double, double)>>     _mouseFuncs;
    static std::map<void*, std::function<void()>>                   _drawFuncs;

    static std::function<void()>                                    _viewUpdateDrawFunc;
    static std::function<void()>                                    _postDrawCallback;
    static std::function<void()>                                    _preDrawCallback;
    static std::mutex                                               _eventLock;

  public:
    static void subscribeToMouseClick(void* thisPointer, std::function<void(int, int, int, int)> func);
    static void subscribeToGameState(void* thisPointer, std::function<void(EngineStateFlags)> func);
    static void subscribeToMouse(void* thisPointer, std::function<void(double, double)> func);
    static void subscribeToKeyboard(void* thisPointer, std::function<void(int, int, int)> func);
    static void subscribeToReleaseKeyboard(void* thisPointer, std::function<void(int, int, int)> func);
    static void subscribeToDraw(void* thisPointer, std::function<void()> func);

    static void unsubscribeToMouseClick(void* thisPointer);
    static void unsubscribeToGameState(void* thisPointer);
    static void unsubscribeToMouse(void* thisPointer);
    static void unsubscribeToKeyboard(void* thisPointer);
    static void unsubscribeToReleaseKeyboard(void* thisPointer);
    static void unsubscribeToDraw(void* thisPointer);

    static void setPreDrawCallback(std::function<void()> func);
    static void setPostDrawCallback(std::function<void()> func);
    static void viewDrawUpdate(std::function<void()> func) { _viewUpdateDrawFunc = func; }

    static void updateGameState(EngineStateFlags state);
    static void updateDraw(GLFWwindow* _window);
    static void updateMouseClick(int button, int action, int x, int y);
    static void releaseKeyboard(int key, int x, int y);
    static void updateKeyboard(int key, int x, int y);
    static void updateMouse(double x, double y);
};