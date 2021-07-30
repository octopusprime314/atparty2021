#include "AudioManager.h"

AudioManager::AudioManager()
    : _masterBank(nullptr)
    , _stringsBank(nullptr)
{
    // Core System
    FMOD_RESULT result;

    result = FMOD::Studio::System::create(&_studioSystem);
    if (result != FMOD_OK)
    {
        __debugbreak();
    }

    result = _studioSystem->initialize(1024, FMOD_STUDIO_INIT_NORMAL, FMOD_INIT_NORMAL, NULL);
    if (result != FMOD_OK)
    {
        __debugbreak();
    }
}

AudioManager::~AudioManager()
{
    _masterBank->unload();
    _stringsBank->unload();
    _studioSystem->release();
}

void AudioManager::loadBankFile(const std::string& bankFile, const std::string& metadataFile)
{
    FMOD_RESULT result;
    result = _studioSystem->loadBankFile(bankFile.c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &_masterBank);
    if (result != FMOD_OK)
    {
        __debugbreak();
    }

    result = _studioSystem->loadBankFile(metadataFile.c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &_stringsBank);
    if (result != FMOD_OK)
    {
        __debugbreak();
    }

    int numEventDescriptions = 0;
    _masterBank->getEventCount(&numEventDescriptions);
    if (numEventDescriptions > 0)
    {
        std::vector<FMOD::Studio::EventDescription*> eventDescriptions(numEventDescriptions);
        _masterBank->getEventList(eventDescriptions.data(), numEventDescriptions, &numEventDescriptions);
        char eventDescriptionName[512];
        for (int i = 0; i < numEventDescriptions; ++i)
        {
            FMOD::Studio::EventDescription* eventDescription = eventDescriptions[i];
            eventDescription->getPath(eventDescriptionName, 512, nullptr);
            _eventDescriptions.emplace(eventDescriptionName, eventDescription);
        }
    }
}

void AudioManager::setWaypointEvents(const WaypointEvents& waypointEvents) {
    _waypointEvents = waypointEvents;
}

void AudioManager::update(int waypointIdx) 
{
    if (_waypointEvents.find(waypointIdx) != _waypointEvents.end()) {
        auto& events = _waypointEvents[waypointIdx];
        for (auto& e : events) {
            switch (e.type) {
            case EventType::START:
                playEvent(e.eventName);
                break;
            case EventType::STOP:
                stopEvent(e.eventName);
                break;
            }
        }
    }
    _studioSystem->update();
}

void AudioManager::update()
{
    _studioSystem->update();
}


void AudioManager::playEvent(const std::string& eventName)
{
    FMOD_RESULT result;

    if (_events.find(eventName) != _events.end()) {
        FMOD::Studio::EventInstance* eventInstance = _events[eventName];
        eventInstance->start();
    }
    else if (_eventDescriptions.find(eventName) != _eventDescriptions.end()) {
        FMOD::Studio::EventInstance* eventInstance = NULL;
        result = _eventDescriptions[eventName]->createInstance(&eventInstance);
        if (result != FMOD_OK)
        {
            __debugbreak();
        }

        eventInstance->start();
        _events[eventName] = eventInstance;
    }
}

void AudioManager::stopEvent(const std::string& eventName)
{
    FMOD_RESULT result;

    if (_events.find(eventName) != _events.end()) {
        FMOD::Studio::EventInstance* eventInstance = _events[eventName];
        eventInstance->stop(FMOD_STUDIO_STOP_IMMEDIATE);
    }
}


