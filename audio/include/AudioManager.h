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
#include "fmod.hpp"
#include <fmod_studio.hpp>
#include <fmod_errors.h>
#include <string>
#include <unordered_map>
#include <vector>

class AudioManager
{
  public:
    enum class EventType {
        START = 0,
        STOP
    };

    struct WaypointEvent {
        EventType type;
        std::string eventName;
    };

    using WaypointEvents = std::unordered_map<int, std::vector<WaypointEvent>>;
    AudioManager();
    ~AudioManager();

    void            loadBankFile(const std::string& bankFile);
    void            setWaypointEvents(const WaypointEvents& waypointEvents);
    void            update(int waypointIdx);
    void            update();
    void            playEvent(const std::string& eventName);
    void            stopEvent(const std::string& eventName);

  private:
    using EventDescriptions = std::unordered_map<std::string, FMOD::Studio::EventDescription*>;

    FMOD::Studio::System*                                                       _studioSystem;
    FMOD::Studio::Bank*                                                         _masterBank;
    std::unordered_map<std::string, FMOD::Studio::EventInstance*>               _events;
    EventDescriptions                                                           _eventDescriptions;
    WaypointEvents                                                              _waypointEvents;
};