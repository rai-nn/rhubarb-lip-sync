#pragma once

#include <string>
#include <vector>
#include "time/TimeRange.h"

namespace rhubarb {

// Represents timing for a single character from TTS
struct CharacterTiming {
    double startTime;  // in seconds
    double endTime;    // in seconds
    std::string character;
    
    CharacterTiming(double start, double end, const std::string& ch)
        : startTime(start), endTime(end), character(ch) {}
};

// Represents timing for a complete word
struct WordTiming {
    std::string word;
    double startTime;
    double endTime;
    std::vector<CharacterTiming> characters;
    
    TimeRange getTimeRange() const {
        return TimeRange(
            centiseconds(static_cast<int>(startTime * 100)),
            centiseconds(static_cast<int>(endTime * 100))
        );
    }
};

// Parse character timing from JSON
std::vector<CharacterTiming> parseCharacterTimingJson(const std::string& jsonPath);

// Group characters into words based on text
std::vector<WordTiming> groupCharactersIntoWords(
    const std::string& text,
    const std::vector<CharacterTiming>& timings
);

} // namespace rhubarb