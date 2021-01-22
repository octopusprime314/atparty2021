#include "ViewEvents.h"

namespace Factory
{
extern ViewEvents* _viewEventWrapper = nullptr;
}

void ViewEvents::subscribeToView(void* thisPointer, std::function<void(Matrix)> func)
{
    _viewLock.lock();
    _viewFuncs[thisPointer] = func;
    _viewLock.unlock();
}
void ViewEvents::subscribeToProjection(void* thisPointer, std::function<void(Matrix)> func)
{
    _viewLock.lock();
    _projectionFuncs[thisPointer] = func;
    _viewLock.unlock();
}

void ViewEvents::unsubscribeToView(void* thisPointer)
{
    _viewLock.lock();
    _viewFuncs.erase(thisPointer);
    _viewLock.unlock();
}
void ViewEvents::unsubscribeToProjection(void* thisPointer)
{
    _viewLock.lock();
    _projectionFuncs.erase(thisPointer);
    _viewLock.unlock();
}

// Blast all subscribers that have overriden the updateView function
void ViewEvents::updateView(Matrix view)
{
    _viewLock.lock();
    for (auto func : _viewFuncs)
    {
        func.second(view); // Call view/camera update
    }
    _viewLock.unlock();
}
// Blast all subscribers that have overriden the updateProjection function
void ViewEvents::updateProjection(Matrix proj)
{
    _viewLock.lock();
    for (auto func : _projectionFuncs)
    {
        func.second(proj); // Call projection update
    }
    _viewLock.unlock();
}
