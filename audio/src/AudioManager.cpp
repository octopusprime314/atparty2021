#include "AudioManager.h"
#include "MasterClock.h"

#define THEME_MP3 "assets/audio/divineapprehension.mp3"

#include <Windows.h>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

AudioManager::AudioManager()
{
    // Core System
    FMOD_RESULT result;
    _system = nullptr;
    result  = FMOD::System_Create(&_system);
    if (result != FMOD_OK)
    {
        __debugbreak();
    }

    result = _system->init(512, FMOD_INIT_NORMAL, /*extra*/ nullptr);
    if (result != FMOD_OK)
    {
        __debugbreak();
    }

    _started = false;
}

AudioManager::~AudioManager()
{
    _system->release();
    _system = nullptr;
}

FMOD_RESULT AudioManager::update()
{
    if (_started)
    {

        std::chrono::seconds seconds = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch());

        int time_passed = (seconds.count() - _initial_time.count());

        for (auto& s : _sounds)
        {
            if ((!isPlaying(s.first) || isPaused(s.first)) && time_passed >= s.second.startTime &&
                time_passed < s.second.endTime)
            {
                // END command means the audio is finished and terminate executable
                const std::string teminateString = "END";
                if (s.first.compare(teminateString.c_str()) == 0)
                {
                    exit(0);
                }

                playSound(s.first);
            }

            if (time_passed >= s.second.endTime)
            {
                pauseSound(s.first);
            }
        }
    }

    return _system->update();
}

void AudioManager::startAll()
{
    _initial_time = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch());
    _started = true;
}

void AudioManager::stopAll()
{
    _started = false;
    for (auto& s : _sounds)
    {
        pauseSound(s.first);
        restartSound(s.first);
    }
}

FMOD_RESULT AudioManager::playSound(const std::string& name)
{
    FMOD_RESULT result = FMOD_OK;

    if (_sounds.find(name) == _sounds.end())
    {
        return FMOD_ERR_BADCOMMAND;
    }

    FMOD::Sound* sound = _sounds[name].sound;

    if (isPlaying(name) && isPaused(name))
    {
        result = _sounds[name].channel->setPaused(false);
    }
    else
    {
        result = _system->playSound(sound, nullptr, false, &_sounds[name].channel);
    }

    if (result == FMOD_OK)
    {
        result = _sounds[name].channel->setVolume(_sounds[name].volume);
    }

    return result;
}

FMOD_RESULT AudioManager::pauseSound(const std::string& name)
{
    FMOD_RESULT result = FMOD_OK;

    if (_sounds.find(name) == _sounds.end())
    {
        return FMOD_ERR_BADCOMMAND;
    }

    bool isplaying = false;

    if (_sounds[name].sound != nullptr && _sounds[name].channel != nullptr)
    {
        result = _sounds[name].channel->isPlaying(&isplaying);
        if (isplaying && result == FMOD_RESULT::FMOD_OK)
            result = _sounds[name].channel->setPaused(true);
    }

    return result;
}

FMOD_RESULT AudioManager::restartSound(const std::string& name)
{
    FMOD_RESULT result = FMOD_OK;

    if (_sounds.find(name) == _sounds.end())
    {
        return FMOD_ERR_BADCOMMAND;
    }

    bool isplaying = false;

    if (_sounds[name].sound != nullptr && _sounds[name].channel != nullptr)
    {
        result = _sounds[name].channel->setPosition(0, FMOD_TIMEUNIT_MS);
    }

    return result;
}

bool AudioManager::isPlaying(const std::string& name)
{
    if (_sounds.find(name) == _sounds.end())
    {
        return false;
    }

    bool isplaying = false;
    if (_sounds[name].sound != nullptr && _sounds[name].channel != nullptr)
    {
        FMOD_RESULT result = _sounds[name].channel->isPlaying(&isplaying);
        if (result != FMOD_OK)
            return false;
    }

    return isplaying;
}

bool AudioManager::isPaused(const std::string& name)
{
    if (_sounds.find(name) == _sounds.end())
    {
        return false;
    }

    bool ispaused = false;
    if (_sounds[name].sound != nullptr && _sounds[name].channel != nullptr)
    {
        FMOD_RESULT result = _sounds[name].channel->getPaused(&ispaused);
        if (result != FMOD_OK)
            return false;
    }

    return ispaused;
}

bool AudioManager::updateStartTime(const std::string& name, int startTime)
{
    if (_sounds.find(name) == _sounds.end())
    {
        return false;
    }

    _sounds[name].startTime = startTime;

    return true;
}

bool AudioManager::updateEndTime(const std::string& name, int endTime)
{
    if (_sounds.find(name) == _sounds.end())
    {
        return false;
    }

    _sounds[name].endTime = endTime;

    return true;
}

bool AudioManager::updateVolume(const std::string& name, float volume)
{
    if (_sounds.find(name) == _sounds.end())
    {
        return false;
    }

    _sounds[name].volume = volume;
    _sounds[name].channel->setVolume(volume);

    return true;
}

void AudioManager::restart() {}

void AudioManager::loadSoundConfig(const std::string& file)
{
    std::ifstream infile(file);
    _sounds.clear();

    std::string line;
    while (std::getline(infile, line))
    {
        SoundEntry entry;

        std::stringstream                  ss(line);
        std::istream_iterator<std::string> begin(ss);
        std::istream_iterator<std::string> end;
        std::vector<std::string>           vstrings(begin, end);
        std::copy(vstrings.begin(), vstrings.end(),
                  std::ostream_iterator<std::string>(std::cout, "\n"));

        std::string name     = vstrings[0];
        std::string fileName = vstrings[1];
        entry.startTime      = std::stoi(vstrings[2]);
        entry.endTime        = std::stoi(vstrings[3]);
        entry.volume         = std::stof(vstrings[4]);

        FMOD_RESULT result =
            _system->createStream(fileName.c_str(), FMOD_DEFAULT, nullptr, &entry.sound);

        if (result == FMOD_OK)
        {
            _sounds[name] = entry;
        }
        else if (name == "END")
        {
            _sounds[name] = entry;
        }
    }
}

void AudioManager::saveSoundConfig(const std::string& file) {}
