/*
 * AudioManager is part of the ReBoot distribution (https://github.com/octopusprime314/ReBoot.git).
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
 *  AudioManager class. Simple audio wrapper around FMOD library.
 */

#pragma once
#include "BackgroundTheme.h"
#include "fmod.hpp"
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>

struct SoundEntry
{
    FMOD::Sound*   sound{nullptr};
    FMOD::Channel* channel{nullptr};
    int            startTime;
    int            endTime;
    float          volume;
};

class AudioManager
{
  public:
    using SoundMap = std::unordered_map<std::string, SoundEntry>;
    AudioManager();
    ~AudioManager();

    FMOD_RESULT     update();
    void            startAll();
    void            stopAll();
    FMOD_RESULT     playSound(const std::string& name);
    FMOD_RESULT     pauseSound(const std::string& name);
    FMOD_RESULT     restartSound(const std::string& name);
    bool            isPlaying(const std::string& name);
    bool            isPaused(const std::string& name);
    bool            updateStartTime(const std::string& name, int startTime);
    bool            updateEndTime(const std::string& name, int endTime);
    bool            updateVolume(const std::string& name, float volume);
    void            restart();
    void            loadSoundConfig(const std::string& file);
    void            saveSoundConfig(const std::string& file);
    const SoundMap& getSounds() const { return _sounds; }

  private:
    bool                 _started;
    std::chrono::seconds _initial_time;
    FMOD::System*        _system;
    SoundMap             _sounds;
};