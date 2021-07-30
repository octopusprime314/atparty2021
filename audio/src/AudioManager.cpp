#include "AudioManager.h"

AudioManager::AudioManager()
    : _masterBank(nullptr)
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
    _studioSystem->release();
}

void AudioManager::loadBankFile(const std::string& bankFile)
{
    FMOD_RESULT result;
    result = _studioSystem->loadBankFile(bankFile.c_str(), FMOD_STUDIO_LOAD_BANK_NORMAL, &_masterBank);
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

void AudioManager::update() 
{
    _studioSystem->update();
}

void AudioManager::playEvent(const std::string& eventName)
{
    FMOD_RESULT result;

    if (_eventDescriptions.find(eventName) != _eventDescriptions.end()) {
        FMOD::Studio::EventInstance* eventInstance = NULL;
        result = _eventDescriptions[eventName]->createInstance(&eventInstance);
        if (result != FMOD_OK)
        {
            __debugbreak();
        }

        eventInstance->start();
        _events.push_back(eventInstance);
    }
}

