# rhubarb-stream Progress

## Project Overview

**RAI-1014**: Real-time streaming lip-sync viseme generation using PocketSphinx.

This C++ binary receives audio chunks and transcription sentences via stdin (binary protocol), processes them in parallel using PocketSphinx speech recognition, and outputs JSON visemes via stdout.

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           Node.js Host                                  │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │ Audio Buffer + Sentence Queue → stdin (binary frames)              │ │
│  └────────────────────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────────────────────┘
                                    ↓
┌─────────────────────────────────────────────────────────────────────────┐
│                         rhubarb-stream                                  │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │ FrameReader → Parse binary frames (AUDIO, SENTENCE, CONFIG, etc.) │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                                    ↓                                    │
│  ┌────────────────────────────────────────────────────────────────────┐ │
│  │ SentenceQueue + WorkerPool → Parallel sentence processing          │ │
│  └────────────────────────────────────────────────────────────────────┘ │
│                                    ↓                                    │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │ Per-Sentence Pipeline:                                            │   │
│  │   AudioSlicer → PhoneRecognizer → SentenceAnimator → FrameWriter │   │
│  └──────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
                                    ↓
┌─────────────────────────────────────────────────────────────────────────┐
│                           stdout (JSON Lines)                           │
│  {"type":"viseme","start":0.15,"end":0.25,"value":"B"}                 │
│  {"type":"end","total_visemes":42,"duration":3.25}                     │
└─────────────────────────────────────────────────────────────────────────┘
```

## Binary Frame Protocol

```
[Type: 1 byte][Length: 4 bytes LE][Payload: N bytes]

Types:
- 0x01 AUDIO:    PCM chunk (16-bit, 16kHz, mono, little-endian)
- 0x02 SENTENCE: JSON with text and word timestamps
- 0x03 CONFIG:   JSON configuration (optional)
- 0x04 RESET:    Clear audio buffer (start new stream)
- 0xFF END:      Finalize and exit
```

---

## Phase 0: Project Setup ✅

**Completed**: Initial project structure, CMakeLists.txt, build system.

**Files created:**
- `CMakeLists.txt` - Build configuration with rhubarb lib/ dependencies
- Basic source structure

---

## Phase 1: Core Infrastructure ✅

**Completed**: Time types, Phone/Shape enums, binary protocol.

**Files created:**
- `src/core/Phone.h/.cpp` - ARPAbet phone enum
- `src/core/Shape.h/.cpp` - Preston Blair 9-shape viseme system (A-H, X)
- `src/time/centiseconds.h/.cpp` - Centisecond time unit
- `src/time/TimeRange.h/.cpp` - Time range with validation
- `src/time/Timed.h` - Value with time range
- `src/time/Timeline.h` - Collection of timed values
- `src/time/BoundedTimeline.h` - Timeline with bounds
- `src/time/ContinuousTimeline.h` - Gapless timeline
- `src/protocol/FrameTypes.h` - Frame type enum and limits
- `src/protocol/FrameReader.h/.cpp` - Binary frame parser
- `src/protocol/FrameWriter.h/.cpp` - JSON output emitter
- `src/tools/EnumConverter.h` - Enum/string conversion utility
- `src/tools/array.h` - Array utilities
- `src/tools/tools.h` - Lambda unique_ptr helper

---

## Phase 2: Animation Rules ✅

**Completed**: Phone-to-shape mapping, animation rules from rhubarb.

**Files created:**
- `src/animation/animationRules.h/.cpp` - `getShapeRules()` and `getClosestShape()` from rhubarb
- `src/animation/ShapeRule.h/.cpp` - Shape selection rule with weight
- `src/animation/shapeShorthands.h` - Convenience macros for shape rules

---

## Phase 3: Parallel Processing ✅

**Completed**: Thread-safe sentence queue, worker pool, main frame loop.

**Files created:**
- `src/queue/Sentence.h` - Sentence struct with JSON parsing
- `src/queue/SentenceQueue.h/.cpp` - Thread-safe FIFO queue with finish signal
- `src/queue/WorkerPool.h/.cpp` - Parallel worker threads with processor callback
- `src/main.cpp` - Frame processing loop, AudioBuffer, FrameProcessor

**Key patterns:**
- RAI-739: Thread-local decoder storage for parallel sentence processing
- Worker pool dynamically sized based on core count (capped at 4 for streaming)

---

## Phase 4: Sentence Processing ✅

**Completed**: Full viseme generation pipeline with PocketSphinx.

### Files Created

**`src/processing/AudioSlicer.h/.cpp`** - PCM segment extraction

Extracts audio slices with configurable padding (default 30ms before/after) for acoustic context:
```cpp
class AudioSlicer {
public:
    AudioSlicer(const int16_t* audioData, size_t sampleCount, int paddingMs = 30);
    std::vector<int16_t> slice(double startSeconds, double endSeconds) const;
    double getActualStart(double startSeconds) const;  // Returns padded start time
    double getActualEnd(double endSeconds) const;      // Returns padded end time
};
```

**`src/processing/PhoneRecognizer.h/.cpp`** - PocketSphinx wrapper

Thread-safe phone recognition with decoder pooling:
```cpp
class PhoneRecognizer {
public:
    BoundedTimeline<Phone> recognizePhones(
        const std::vector<int16_t>& audioSlice,
        const std::string& sentenceText,    // Dialog hint for better accuracy
        double sliceStartSeconds            // Offset for absolute timestamps
    );
private:
    // Thread-local decoder storage (RAI-739 pattern)
    std::unordered_map<std::thread::id, DecoderEntry> threadDecoders_;
    std::mutex decoderMapMutex_;
};
```

Key features:
- Thread-local decoder storage for parallel processing
- Dialog hint (sentence text) improves recognition accuracy
- Automatic G2P fallback for unknown words
- Dynamic path resolution for sphinx models (handles both nested and flat structures)

**`src/animation/SentenceAnimator.h/.cpp`** - Phone to viseme conversion

Converts phone timeline to visemes using rhubarb's animation rules:
```cpp
struct Viseme {
    double start, end;
    Shape shape;
};

class SentenceAnimator {
public:
    std::vector<Viseme> animate(const BoundedTimeline<Phone>& phones);
private:
    Shape selectShape(const ShapeSet& shapeSet, Shape previousShape);
    std::vector<Viseme> mergeAdjacentVisemes(const std::vector<Viseme>& visemes);
};
```

### Files Modified

**`src/main.cpp`** - Integrated real sentence processor:
```cpp
static PhoneRecognizer g_phoneRecognizer;
static SentenceAnimator g_sentenceAnimator;

void realSentenceProcessor(
    const Sentence& sentence,
    const int16_t* audioData,
    size_t audioSampleCount,
    FrameWriter& writer
) {
    AudioSlicer slicer(audioData, audioSampleCount);
    auto audioSlice = slicer.slice(sentence.start, sentence.end);
    auto phones = g_phoneRecognizer.recognizePhones(audioSlice, sentence.text, actualStart);
    auto visemes = g_sentenceAnimator.animate(phones);
    for (const auto& viseme : visemes) {
        writer.emitViseme(viseme.start, viseme.end, viseme.shape);
    }
}
```

**`CMakeLists.txt`** - Added new source files to build

### Issues Fixed

1. **`s3wid_t` undeclared identifier**
   - Location: `PhoneRecognizer.h:105`
   - Fix: Added `#include <pocketsphinx_internal.h>` to header

2. **"Cannot find sphinx model directory" runtime error**
   - Cause: CMake copies sphinx models with nested structure (`pocketsphinx-rev13216/model/en-us/`)
   - Fix: Updated `findSphinxModelDirectory()` to check both nested and flat paths

### Test Results

All 10 tests pass with visemes being generated:
```
viseme: {"type":"viseme","start":0,"end":0.13,"value":"X"}
viseme: {"type":"viseme","start":0.13,"end":0.96,"value":"B"}
viseme: {"type":"viseme","start":0.96,"end":1,"value":"X"}
...
```

---

## Phase 5: Node.js Integration (Pending)

**Goal**: Create TypeScript wrapper for Node.js and test with real audio from ctuber.

**Tasks:**
- [ ] Create `RhubarbStreamClient` TypeScript class
- [ ] Implement binary frame encoding
- [ ] Handle JSON output parsing
- [ ] Test with real TTS audio
- [ ] Performance benchmarking vs original rhubarb

---

## Build & Test

```bash
# Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Test
cd ..
./test.sh  # Runs all 10 test cases
```

---

## Key Design Decisions

1. **Binary protocol over JSON**: Efficient audio transmission, minimal parsing overhead
2. **Parallel sentence processing**: Utilizes multi-core CPUs for faster throughput
3. **Thread-local decoders**: Avoids decoder contention, follows RAI-739 pattern
4. **30ms audio padding**: Provides acoustic context for better phone boundary detection
5. **Dialog hints**: Sentence text improves PocketSphinx accuracy
6. **Viseme merging**: Reduces output noise by combining adjacent same-shape visemes
