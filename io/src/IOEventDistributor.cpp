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

LRESULT CALLBACK IOEventDistributor::dxEventLoop(HWND hWnd, UINT message, WPARAM wParam,
                                                 LPARAM lParam)
{
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
        case WM_MOUSEMOVE:
        {
            int xPos = GET_X_LPARAM(lParam);
            int yPos = GET_Y_LPARAM(lParam);
            _mouseUpdate(nullptr, xPos * 10, yPos * 10);
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

}

// One frame draw update call
void IOEventDistributor::_drawUpdate()
{

    if (EngineManager::getGraphicsLayer() == GraphicsLayer::OPENGL)
    {
        auto     last          = nowMs();
        uint64_t updateTrigger = KINEMATICS_TIME;
        while (!_quit)
        {
            auto now = nowMs();
            updateTrigger += (now - last).count();
            last = now;

            if (updateTrigger >= KINEMATICS_TIME)
            {
                MasterClock::instance()->update(updateTrigger);
                updateTrigger = 0;
            }

            IOEvents::updateDraw(_window);
        }
    }
    else
    {

        // this struct holds Windows event messages
        MSG      msg           = {0};
        auto     last          = nowMs();
        uint64_t updateTrigger = KINEMATICS_TIME;

        uint64_t cyclesWithoutMouseMovement = 0;

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

            // Keep cursor locked inside the window
            RECT rect = {0, 0, IOEventDistributor::screenPixelWidth,
                         IOEventDistributor::screenPixelHeight};
            ClipCursor(&rect);

            /*if (cyclesWithoutMouseMovement > 100)
            {
                SetCursorPos(IOEventDistributor::screenPixelWidth / 2,
                             IOEventDistributor::screenPixelHeight / 2);

                cyclesWithoutMouseMovement = 0;
            }

            cyclesWithoutMouseMovement++;*/

            auto now = nowMs();
            updateTrigger += (now - last).count();
            last = now;

            auto millisecondsPerFrame = 16;
            // if (updateTrigger >= millisecondsPerFrame)
            //{
            // Update kinematics by 1 mS
            // MasterClock::instance()->update(16);
            MasterClock::instance()->update(updateTrigger);
            updateTrigger = 0;

            // Draw frame every second
            IOEvents::updateDraw(_window);
            //}
        }
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

    IOEvents::updateMouseClick(button, action, static_cast<int>(xpos), static_cast<int>(ypos));
}

void IOEventDistributor::_frameRateTrigger(int milliSeconds)
{
    // Triggers the simple context to draw a frame
    _renderLock.lock();
    _renderNow++;
    _renderLock.unlock();
}
