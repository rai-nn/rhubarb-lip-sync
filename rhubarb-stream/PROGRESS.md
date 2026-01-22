# rhubarb-stream Progress

## Plan

**RAI-1014**: Real-time streaming lip-sync viseme generation using PocketSphinx.

This C++ binary receives audio chunks and transcription sentences via stdin (binary protocol), processes them in parallel using PocketSphinx speech recognition, and outputs JSON visemes via stdout.

### Architecture

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

### Binary Frame Protocol

```
[Type: 1 byte][Length: 4 bytes LE][Payload: N bytes]

Types:
- 0x01 AUDIO:    PCM chunk (16-bit, 16kHz, mono, little-endian)
- 0x02 SENTENCE: JSON with text and word timestamps
- 0x03 CONFIG:   JSON configuration (optional)
- 0x04 RESET:    Clear audio buffer (start new stream)
- 0xFF END:      Finalize and exit
```

### Implementation Phases

1. Phase 0: Project Setup ✅
2. Phase 1: Core Infrastructure ✅
3. Phase 2: Animation Rules ✅
4. Phase 3: Parallel Processing ✅
5. Phase 4: Sentence Processing ✅
6. Phase 5: Node.js Integration (Pending)

---

## Logs

### 2026-01-22: Phase 0-4 Implementation Complete

**Linear**: RAI-1014
**Commit**: 5e1e280 (rhubarb-stream), 2d37b18 (ctuber submodule update)

Completed initial implementation of rhubarb-stream with full viseme generation pipeline.

#### Phase 0: Project Setup
- Created `CMakeLists.txt` with rhubarb lib/ dependencies
- Set up basic source structure

#### Phase 1: Core Infrastructure
Files created:
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

#### Phase 2: Animation Rules
Files created:
- `src/animation/animationRules.h/.cpp` - `getShapeRules()` and `getClosestShape()` from rhubarb
- `src/animation/ShapeRule.h/.cpp` - Shape selection rule with weight
- `src/animation/shapeShorthands.h` - Convenience macros for shape rules

#### Phase 3: Parallel Processing
Files created:
- `src/queue/Sentence.h` - Sentence struct with JSON parsing
- `src/queue/SentenceQueue.h/.cpp` - Thread-safe FIFO queue with finish signal
- `src/queue/WorkerPool.h/.cpp` - Parallel worker threads with processor callback
- `src/main.cpp` - Frame processing loop, AudioBuffer, FrameProcessor

Key patterns:
- RAI-739: Thread-local decoder storage for parallel sentence processing
- Worker pool dynamically sized based on core count (capped at 4 for streaming)

#### Phase 4: Sentence Processing
Files created:
- `src/processing/AudioSlicer.h/.cpp` - PCM segment extraction with 30ms padding
- `src/processing/PhoneRecognizer.h/.cpp` - PocketSphinx wrapper with thread-local decoders
- `src/animation/SentenceAnimator.h/.cpp` - Phone to viseme conversion

Issues fixed:
1. **`s3wid_t` undeclared identifier** - Added `#include <pocketsphinx_internal.h>` to header
2. **"Cannot find sphinx model directory"** - Updated path resolution for nested CMake structure

---

### 2026-01-22: Test Scripts and Benchmark Comparison

**Linear**: RAI-1014

Created comprehensive test suite for validating rhubarb-stream against rhubarb-lite.

#### Test Scripts Created

**`test-stream.py`** - Basic test script
- Loads WAV file, encodes binary frames, sends to rhubarb-stream
- Displays viseme output

**`test-verbose.py`** - Verbose data flow visualization
- Shows INPUT → PROCESS → OUTPUT with colored output
- Displays frame sequence, debug messages, viseme stream
- Summary with shape distribution

**`test-realtime.py`** - Real-time streaming test with benchmark
- Simulates realistic streaming with random 2-4 second audio chunks
- Real-time output display as processing happens
- Runs rhubarb-lite benchmark on same audio for comparison
- Outputs both results in identical JSON format

Usage:
```bash
./test-realtime.py /path/to/audio.wav "Transcript text here"
```

#### Benchmark Results (31.48s audio - summer_camp.wav)

| Metric | rhubarb-stream | rhubarb-lite |
|--------|---------------|--------------|
| Processing time | 7.44s | 3.13s |
| Speed | 4.2x realtime | 10.1x realtime |
| Visemes | 152 | 157 |

Shape distribution comparison:
- Both produce similar distributions (A, B, C, E, F, G, H, X)
- Minor differences in counts due to slightly different animation rules/merging

#### Output Files

Both saved in identical format for drag-and-drop testing tool:
- `rhubarb-stream-visemes.json`
- `rhubarb-lite-visemes.json`

Format:
```json
{
  "duration": 31.48,
  "visemes": [
    {"start": 0.0, "end": 0.11, "value": "X"},
    {"start": 0.11, "end": 0.5, "value": "B"},
    ...
  ]
}
```

#### Why rhubarb-stream is slower

1. Resampling done in Python (rhubarb-lite handles natively)
2. Binary protocol encoding/decoding overhead
3. Thread pool startup for parallel processing
4. In production, streaming architecture enables processing *while* audio arrives, hiding latency

---

## Build & Test

```bash
# Build
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run real-time test with benchmark
cd ..
python3 test-realtime.py /path/to/audio.wav "Transcript text"
```

---

## Key Design Decisions

1. **Binary protocol over JSON**: Efficient audio transmission, minimal parsing overhead
2. **Parallel sentence processing**: Utilizes multi-core CPUs for faster throughput
3. **Thread-local decoders**: Avoids decoder contention, follows RAI-739 pattern
4. **30ms audio padding**: Provides acoustic context for better phone boundary detection
5. **Dialog hints**: Sentence text improves PocketSphinx accuracy
6. **Viseme merging**: Reduces output noise by combining adjacent same-shape visemes

---

## Phase 5: Node.js Integration (Pending)

**Goal**: Create TypeScript wrapper for Node.js and test with real audio from ctuber.

**Tasks:**
- [ ] Create `RhubarbStreamClient` TypeScript class
- [ ] Implement binary frame encoding
- [ ] Handle JSON output parsing
- [ ] Test with real TTS audio
- [ ] Performance benchmarking vs original rhubarb
