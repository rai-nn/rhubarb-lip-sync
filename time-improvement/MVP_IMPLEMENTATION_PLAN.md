# Rhubarb WordTimingRecognizer Implementation Plan

## Executive Summary

This document outlines the implementation of a WordTimingRecognizer that bypasses expensive speech recognition (71.5s) while preserving acoustic alignment for natural lip sync. By using character-level timing from TTS systems to identify word boundaries and then applying PocketSphinx's forced alignment, we can achieve a 35-70x speedup (1-2s vs 72s) with nearly identical output quality.

## Problem Analysis

### Current Performance Bottleneck
- **Total processing time**: 72 seconds for 23.54 seconds of audio
- **Speech recognition**: ~71.5 seconds (99% of time)
- **Forced alignment**: ~0.5 seconds
- **Other processing**: <0.5 seconds

### Key Insight
PocketSphinx performs two distinct operations:
1. **Speech Recognition** (expensive): Determines what words were spoken
2. **Forced Alignment** (fast): Determines phoneme boundaries using acoustic analysis

When using TTS, we already know the words and their boundaries, so we can skip recognition but must keep alignment for natural results.

## Architecture Overview

### Data Flow Pipeline

```
Character Timing (ElevenLabs) → Word Boundaries → WordTimingRecognizer → Forced Alignment → Phone Timeline → Animation
```

### Why Previous Approach Failed
The initial character timing implementation distributed phonemes mathematically within word boundaries, producing mechanical, unnatural timing. It completely bypassed acoustic analysis, missing crucial speech rhythm information.

### Correct Approach
1. Parse character timing to extract word boundaries
2. Convert words to PocketSphinx word IDs
3. Use existing `getPhoneAlignment()` for acoustic analysis
4. Preserve all downstream processing

## Implementation Details

### Phase 1: Data Structures and Parsing

#### 1.1 Character Timing Parser (Keep Existing)
- **File**: `rhubarb/src/lib/characterTiming.h/cpp`
- **Purpose**: Parse ElevenLabs JSON and group characters into words
- **Input Format**:
```json
{
  "alignment": [
    {"start": 0.174, "end": 0.244, "value": "L"},
    {"start": 0.244, "end": 0.29, "value": "o"},
    {"start": 0.29, "end": 0.337, "value": "o"},
    {"start": 0.337, "end": 0.383, "value": "k"}
  ]
}
```
- **Output**: `WordTiming` structures with word text and boundaries

#### 1.2 Word Timing Structure
```cpp
struct WordTiming {
    std::string word;
    centiseconds startTime;
    centiseconds endTime;
};
```

### Phase 2: WordTimingRecognizer Implementation

#### 2.1 Recognizer Interface
- **File**: `rhubarb/src/recognition/WordTimingRecognizer.h`
```cpp
class WordTimingRecognizer : public Recognizer {
public:
    WordTimingRecognizer(
        const std::vector<WordTiming>& wordTimings,
        const std::string& audioPath
    );
    
    BoundedTimeline<Phone> recognizePhones(
        const AudioClip& audioClip,
        boost::optional<std::string> dialog,
        int maxThreadCount,
        ProgressSink& progressSink
    ) const override;
    
private:
    std::vector<WordTiming> wordTimings_;
    std::string audioPath_;
};
```

#### 2.2 Core Implementation Logic
```cpp
BoundedTimeline<Phone> WordTimingRecognizer::recognizePhones(...) {
    // Step 1: Load PocketSphinx decoder and dictionary
    auto decoder = createDecoder(dialog);
    
    // Step 2: Convert words to word IDs
    std::vector<s3wid_t> wordIds;
    for (const auto& wordTiming : wordTimings_) {
        s3wid_t wordId = getWordId(decoder, wordTiming.word);
        wordIds.push_back(wordId);
    }
    
    // Step 3: Prepare audio buffer
    auto audioBuffer = audioClip.getSamples();
    
    // Step 4: Call existing forced alignment
    optional<Timeline<Phone>> phoneAlignment = 
        getPhoneAlignment(wordIds, audioBuffer, *decoder);
    
    // Step 5: Convert to BoundedTimeline
    return createBoundedTimeline(phoneAlignment);
}
```

#### 2.3 Key Functions to Reuse
- `createDecoder()` - Initialize PocketSphinx decoder
- `getPhoneAlignment()` - Perform acoustic alignment (from PocketSphinxRecognizer.cpp:152-232)
- Dictionary lookup functions for word-to-ID conversion

### Phase 3: Integration

#### 3.1 RecognizerType Enum
- **File**: `rhubarb/src/rhubarb/RecognizerType.h`
```cpp
enum class RecognizerType {
    PocketSphinx,
    Phonetic,
    WordTiming  // Add this
};
```

#### 3.2 Factory Pattern Update
- **File**: `rhubarb/src/rhubarb/main.cpp`
```cpp
unique_ptr<Recognizer> createRecognizer(
    RecognizerType type,
    const boost::optional<std::string>& wordTimingPath = boost::none,
    const boost::optional<std::string>& audioPath = boost::none
) {
    switch (type) {
        case RecognizerType::WordTiming:
            if (!wordTimingPath || !audioPath) {
                throw std::runtime_error("WordTiming recognizer requires timing and audio paths");
            }
            auto timings = parseWordTimingJson(*wordTimingPath);
            return make_unique<WordTimingRecognizer>(timings, *audioPath);
        // ... existing cases
    }
}
```

#### 3.3 Command-Line Arguments
```cpp
TCLAP::ValueArg<string> wordTimingFile(
    "", "wordTiming",
    "JSON file with word-level timing data (requires audio file for alignment)",
    false, string(), "string", cmd
);
```

### Phase 4: Cleanup

#### 4.1 Remove Failed Implementation
Files to remove:
- `textToPhones.h/cpp` - Mathematical phoneme distribution

#### 4.2 Update Existing Files
- `rhubarbLib.cpp` - Replace `animateFromCharacterTiming` with `animateFromWordTiming`
- `CMakeLists.txt` - Update source file references

## JSON Format Specification

### Input: Character Timing (from ElevenLabs)
```json
{
  "alignment": [
    {"start": 0.174, "end": 0.244, "value": "L"},
    {"start": 0.244, "end": 0.29, "value": "o"},
    ...
  ]
}
```

### Internal: Word Timing (generated)
```json
{
  "words": [
    {"word": "Look", "start": 0.174, "end": 0.383},
    {"word": "at", "start": 0.43, "end": 0.499},
    ...
  ]
}
```

## Critical Implementation Notes

### Forced Alignment Requirements
1. **Audio is mandatory** - Acoustic analysis requires the actual audio file
2. **Acoustic model needed** - PocketSphinx models must be available
3. **Dictionary access** - Word-to-phoneme mappings required

### Why Audio Cannot Be Skipped
The forced alignment process:
- Extracts MFCCs (Mel-frequency cepstral coefficients) from audio
- Compares acoustic features against trained models
- Determines actual phoneme transition points
- This is what creates natural, synchronized lip movement

### Performance Expectations
- **Speech Recognition**: ~71.5s (skipped)
- **Forced Alignment**: ~0.5s (kept)
- **Other Processing**: ~0.5s (unchanged)
- **Total**: ~1-2 seconds

## Testing Strategy

### Test Data
- `alignment.json` - Character timing from ElevenLabs
- `transcript.txt` - Text transcript  
- `audio.wav` - Audio file (required for alignment)
- `visemes.json` - Reference output for comparison

### Test Commands
```bash
# Build with new recognizer
cd visemes/rhubarb-lip-sync
mkdir build && cd build
cmake ..
make

# Test character timing → word timing conversion
./rhubarb --characterTiming ../time-improvement/examples/alignment.json \
          --text ../time-improvement/examples/transcript.txt \
          ../time-improvement/examples/audio.wav \
          -f json -o output.json

# Compare with original
./rhubarb ../time-improvement/examples/audio.wav \
          -d ../time-improvement/examples/transcript.txt \
          -f json -o original.json

diff output.json original.json
```

### Performance Measurement
```bash
# Measure new approach
time ./rhubarb --characterTiming alignment.json --text transcript.txt audio.wav -f json

# Measure traditional approach  
time ./rhubarb audio.wav -d transcript.txt -f json
```

### Quality Validation
1. **Viseme Count**: Should be within 5% of original
2. **Timing Accuracy**: Phoneme boundaries should match acoustic features
3. **Transition Quality**: Natural blending between visemes

## Risk Analysis

### Risk 1: Word-to-ID Conversion Failures
- **Issue**: Words not in PocketSphinx dictionary
- **Mitigation**: Fallback to g2p for unknown words
- **Impact**: Minimal - affects only specific words

### Risk 2: Timing Synchronization
- **Issue**: Character timing might not align perfectly with audio
- **Mitigation**: Add tolerance for small timing discrepancies
- **Impact**: Low - TTS systems are generally accurate

### Risk 3: Performance Variance
- **Issue**: Forced alignment time varies with audio length
- **Mitigation**: Already much faster than recognition
- **Impact**: Acceptable - still 35-70x speedup

## Implementation Timeline

### Day 1-2: Foundation
- Parse character timing to word boundaries
- Create WordTiming data structures
- Set up test environment

### Day 3-4: WordTimingRecognizer
- Implement Recognizer interface
- Integrate with PocketSphinx decoder
- Connect to getPhoneAlignment()

### Day 5-6: Integration
- Update recognizer factory
- Add command-line arguments
- Modify build configuration

### Day 7: Testing & Validation
- Performance benchmarking
- Quality comparison
- Edge case testing

## Success Criteria

✅ **Performance**: 1-2 second processing time (35-70x speedup)
✅ **Quality**: >95% similarity to original output
✅ **Architecture**: Clean integration with existing codebase
✅ **Maintainability**: Follows existing patterns and conventions
✅ **Testing**: Validated with real TTS data

## Future Enhancements

### Phase 2: Optimization
- Cache decoder initialization
- Parallel processing for multiple utterances
- Memory optimization for large files

### Phase 3: Direct TTS Integration
- Real-time processing during TTS generation
- Streaming support for long content
- API endpoint for direct integration

## Conclusion

This implementation leverages the key insight that speech recognition and forced alignment are separate processes. By providing word boundaries from TTS character timing and using PocketSphinx's acoustic alignment, we achieve massive performance improvements while maintaining the natural quality that comes from analyzing actual audio characteristics.

The approach is architecturally clean, maintains backward compatibility, and opens the door for real-time lip sync generation in TTS applications.