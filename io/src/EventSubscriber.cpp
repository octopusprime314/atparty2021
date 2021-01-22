#include "EventSubscriber.h"
#include "IOEvents.h"
#include "ViewEventDistributor.h"

EventSubscriber::EventSubscriber()
{
    // Input events
    IOEvents::subscribeToKeyboard(this, std::bind(&EventSubscriber::_updateKeyboard, this, _1, _2, _3));
    IOEvents::subscribeToReleaseKeyboard(this, std::bind(&EventSubscriber::_updateReleaseKeyboard, this, _1, _2, _3));
    IOEvents::subscribeToMouse(this, std::bind(&EventSubscriber::_updateMouse, this, _1, _2));
    IOEvents::subscribeToDraw(this, std::bind(&EventSubscriber::_updateDraw, this));
    IOEvents::subscribeToGameState(this, std::bind(&EventSubscriber::_updateGameState, this, _1));
}

EventSubscriber::EventSubscriber(ViewEvents* eventWrapper)
{
    // Input events
    IOEvents::subscribeToKeyboard(this, std::bind(&EventSubscriber::_updateKeyboard, this, _1, _2, _3));
    IOEvents::subscribeToReleaseKeyboard(this, std::bind(&EventSubscriber::_updateReleaseKeyboard, this, _1, _2, _3));
    IOEvents::subscribeToMouse(this, std::bind(&EventSubscriber::_updateMouse, this, _1, _2));
    IOEvents::subscribeToDraw(this, std::bind(&EventSubscriber::_updateDraw, this));
    IOEvents::subscribeToGameState(this, std::bind(&EventSubscriber::_updateGameState, this, _1));

    // View/Camera events
    _eventWrapper = eventWrapper;
    _eventWrapper->subscribeToView(this, std::bind(&EventSubscriber::_updateView, this, _1));
    _eventWrapper->subscribeToProjection(this, std::bind(&EventSubscriber::_updateProjection, this, _1));
}

EventSubscriber::~EventSubscriber()
{
    _eventWrapper->unsubscribeToView(this);
    _eventWrapper->unsubscribeToProjection(this);

    IOEvents::unsubscribeToKeyboard(this);
    IOEvents::unsubscribeToReleaseKeyboard(this);
    IOEvents::unsubscribeToMouse(this);
    IOEvents::unsubscribeToDraw(this);
    IOEvents::unsubscribeToGameState(this);
}
void EventSubscriber::_updateView(Matrix view) {}
void EventSubscriber::_updateProjection(Matrix view) {}
