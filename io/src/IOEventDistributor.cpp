#include "IOEventDistributor.h"
#include "DXLayer.h"
#include "EngineManager.h"
#include "Logger.h"
#include "MasterClock.h"
#include "ViewEvents.h"
#include "Windowsx.h"

// Events that trigger at a specific time
TimeQueue   IOEventDistributor::_timeEvents;
std::mutex  IOEventDistributor::_renderLock;
int         IOEventDistributor::_renderNow        = 0;
GLFWwindow* IOEventDistributor::_window           = nullptr;
bool        IOEventDistributor::_quit             = false;
int         IOEventDistributor::_prevMouseX       = 0;
int         IOEventDistributor::_prevMouseY       = 0;
int         IOEventDistributor::screenPixelWidth  = -1;
int         IOEventDistributor::screenPixelHeight = -1;
bool        IOEventDistributor::_rightMouseButtonPressed = false;

std::chrono::milliseconds nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
}

IOEventDistributor::IOEventDistributor(int* argc, char** argv, HINSTANCE hInstance, int nCmdShow,
                                       std::string appName)
{

    char workingDir[1024] = "";
    GetCurrentDirectory(sizeof(workingDir), workingDir);
    std::cout << "Working directory: " << workingDir << std::endl;

    _prevMouseX = IOEventDistributor::screenPixelWidth / 2;
    _prevMouseY = IOEventDistributor::screenPixelHeight / 2;

    MasterClock* masterClock = MasterClock::instance();
    // Establishes the frame rate of the draw context
    // For some reason i need to double the framerate to get the target frame rate of 60????
    masterClock->setFrameRate(120);
    masterClock->subscribeFrameRate(this,std::bind(_frameRateTrigger, std::placeholders::_1));

    // Hard coded debug events
    TimeEvent::Callback* debugCallback = []() { printf("%g\n", nowMs().count() / 1e3f); };
}

void IOEventDistributor::run() { _drawUpdate(); }

void IOEventDistributor::subscribeToKeyboard(std::function<void(int, int, int)> func)
{
    _events.subscribeToKeyboard(this, func);
}
void IOEventDistributor::subscribeToReleaseKeyboard(std::function<void(int, int, int)> func)
{
    _events.subscribeToKeyboard(this, func);
}
void IOEventDistributor::subscribeToMouse(std::function<void(double, double)> func)
{
    _events.subscribeToMouse(this, func);
}
void IOEventDistributor::subscribeToDraw(std::function<void()> func)
{
    _events.subscribeToDraw(this, func);
}
void IOEventDistributor::subscribeToGameState(std::function<void(EngineStateFlags)> func)
{
    _events.subscribeToGameState(this, func);
}
void IOEventDistributor::updateGameState(EngineStateFlags state)
{
    IOEvents::updateGameState(state);
}
// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK IOEventDistributor::dxEventLoop(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{

    if (ImGui_ImplWin32_WndProcHandler(hWnd, message, wParam, lParam))
        return true;

    switch (message)
    {
        case WM_KEYDOWN:
        {
            int  key      = static_cast<int>(wParam);
            bool isRepeat = (lParam >> 30) & 0x1;
            if (!isRepeat)
            {
                _keyboardUpdate(nullptr, key, 0, 1, 0);
            }
            break;
        }
        case WM_KEYUP:
        {
            int key = static_cast<int>(wParam);
            _keyboardUpdate(nullptr, key, 0, 0, 0);
            break;
        }

        case WM_RBUTTONDOWN:
        {
            _rightMouseButtonPressed = true;

            int xPos                 = GET_X_LPARAM(lParam);
            int yPos                 = GET_Y_LPARAM(lParam);

            _prevMouseX = xPos;
            _prevMouseY = yPos;
           
            break;
        }
        case WM_RBUTTONUP:
        {
            _rightMouseButtonPressed = false;

            break;
        }
        case WM_MOUSEMOVE:
        {
            if (_rightMouseButtonPressed)
            {
                int xPos = GET_X_LPARAM(lParam);
                int yPos = GET_Y_LPARAM(lParam);

                if (_prevMouseX - xPos != 0 || _prevMouseY - yPos != 0)
                {
                    _mouseUpdate(nullptr, _prevMouseX - xPos, _prevMouseY - yPos);
                }

                _prevMouseX = xPos;
                _prevMouseY = yPos;
            }
            break;
        }
        case WM_DESTROY:
        {
            PostQuitMessage(0);
            break;
        }
        default:
            break;
    }

    // Handle any messages the switch statement didn't.
    return DefWindowProc(hWnd, message, wParam, lParam);
}

void IOEventDistributor::quit()
{
    _quit = true;
    exit(0);
}

// All keyboard input from glfw will be notified here
void IOEventDistributor::_keyboardUpdate(GLFWwindow* window, int key, int scancode, int action,
                                         int mods)
{
    if (action == 1)
    {
        // Escape key pressed, hard exit no cleanup, TODO FIX THIS!!!!
        if (key == 27)
        {
            _quit = true;
            exit(0);
        }

        IOEvents::updateKeyboard(key, 0, 0);
    }
    else if (action == 0)
    {
        IOEvents::releaseKeyboard(key, 0, 0);
    }
    else if (action == 2)
    {
        EngineStateFlags engineStateFlags = EngineState::getEngineState();
        if (engineStateFlags.worldEditorModeEnabled)
        {
            IOEvents::updateKeyboard(key, 0, 0);
        }
    }
}

// One frame draw update call
void IOEventDistributor::_drawUpdate()
{

    // this struct holds Windows event messages
    MSG      msg           = {0};
    auto     last          = nowMs();
    uint64_t updateTrigger = KINEMATICS_TIME;

    // main loop
    while (true)
    {
        // check to see if any messages are waiting in the queue
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            // translate keystroke messages into the right format
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            // check to see if it's time to quit
            if (msg.message == WM_QUIT)
                break;
        }

        auto now = nowMs();
        updateTrigger += (now - last).count();
        last = now;

        MasterClock::instance()->update(updateTrigger);
        updateTrigger = 0;

        // Draw frame every second
        IOEvents::updateDraw(_window);
    }
}

// All mouse input will be notified here
void IOEventDistributor::_mouseUpdate(GLFWwindow* window, double x, double y)
{
    IOEvents::updateMouse(x, y);
}

void IOEventDistributor::_mouseClick(GLFWwindow* window, int button, int action, int mods)
{
    double xpos, ypos;

    //IOEvents::updateMouseClick(button, action, static_cast<int>(xpos), static_cast<int>(ypos));
}

void IOEventDistributor::_frameRateTrigger(int milliSeconds)
{
    // Triggers the simple context to draw a frame
    _renderLock.lock();
    _renderNow++;
    _renderLock.unlock();
}
