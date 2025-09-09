#include "characterTiming.h"
#include "tools/stringTools.h"
#include "tools/textFiles.h"
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/json_parser.hpp>
#include <algorithm>
#include <sstream>
#include <cctype>

namespace rhubarb {

std::vector<CharacterTiming> parseCharacterTimingJson(const std::string& jsonPath) {
    std::vector<CharacterTiming> result;
    
    // Parse JSON file
    boost::property_tree::ptree pt;
    boost::property_tree::read_json(jsonPath, pt);
    
    // Extract alignment array
    for (const auto& item : pt.get_child("alignment")) {
        double start = item.second.get<double>("start");
        double end = item.second.get<double>("end");
        std::string value = item.second.get<std::string>("value");
        
        result.emplace_back(start, end, value);
    }
    
    return result;
}

std::vector<WordTiming> groupCharactersIntoWords(
    const std::string& text,
    const std::vector<CharacterTiming>& timings
) {
    std::vector<WordTiming> result;
    
    // Tokenize text into words
    std::vector<std::string> words;
    std::stringstream ss(text);
    std::string word;
    while (ss >> word) {
        words.push_back(word);
    }
    
    // Match characters to words
    size_t charIndex = 0;
    for (const auto& rawWord : words) {
        WordTiming wordTiming;
        
        // Strip leading and trailing punctuation from word for storage
        std::string cleanWord = rawWord;
        // Remove leading punctuation
        while (!cleanWord.empty() && !isalnum(cleanWord[0])) {
            cleanWord.erase(0, 1);
        }
        // Remove trailing punctuation
        while (!cleanWord.empty() && !isalnum(cleanWord.back())) {
            cleanWord.pop_back();
        }
        
        wordTiming.word = cleanWord;
        
        // Skip any spaces or punctuation before word
        while (charIndex < timings.size() && 
               (timings[charIndex].character == " " || 
                !isalnum(timings[charIndex].character[0]))) {
            charIndex++;
        }
        
        // Collect characters for this word (use rawWord for matching)
        size_t wordCharIndex = 0;
        while (charIndex < timings.size() && wordCharIndex < rawWord.length()) {
            const auto& timing = timings[charIndex];
            
            // Skip punctuation in the word for matching
            while (wordCharIndex < rawWord.length() && !isalnum(rawWord[wordCharIndex])) {
                wordCharIndex++;
            }
            
            if (wordCharIndex < rawWord.length()) {
                // Match character (case-insensitive)
                if (tolower(rawWord[wordCharIndex]) == tolower(timing.character[0])) {
                    wordTiming.characters.push_back(timing);
                    wordCharIndex++;
                }
            }
            charIndex++;
        }
        
        // Set word timing boundaries
        if (!wordTiming.characters.empty()) {
            wordTiming.startTime = wordTiming.characters.front().startTime;
            wordTiming.endTime = wordTiming.characters.back().endTime;
            result.push_back(wordTiming);
        }
    }
    
    return result;
}

} // namespace rhubarb