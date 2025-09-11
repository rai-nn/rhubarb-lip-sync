# Rhubarb Lip Sync - Complete Pipeline Documentation

This document provides a comprehensive overview of the Rhubarb Lip Sync pipeline, from audio input to viseme output. This documentation is designed to be loaded into context for quick reference without needing to read the source code.

## Table of Contents
1. [Overview](#overview)
2. [Pipeline Stages](#pipeline-stages)
3. [Key Components](#key-components)
4. [Command-Line Arguments](#command-line-arguments)
5. [Shape System](#shape-system)
6. [Implementation Details](#implementation-details)

## Overview

Rhubarb Lip Sync is a C++17 application that converts audio recordings into mouth shape animations (visemes) for 2D animation. The pipeline follows these main stages:

```
Audio Input → Voice Activity Detection → Speech Recognition (with Phoneme Detection) → 
Animation (Shape Rules → Rough Animation → Optimization → Tweening) → Export
```

## Pipeline Stages

### 1. Entry Point and Initialization (`main.cpp`)

The main entry point handles:
- Command-line argument parsing using TCLAP
- Logging setup (file, console, machine-readable modes)
- Recognizer selection (PocketSphinx, Phonetic, or WordTiming)
- Export format selection (JSON, XML, TSV, DAT)
- Threading configuration

#### Audio Loading (`audio/audioFileReading.cpp`)
After initialization, audio loading occurs:
- Supports WAV and OGG Vorbis formats
- Creates `AudioClip` object containing audio samples
- Loads entire audio file into memory
- Initial sample rate preserved (will be resampled as needed later)

Key entry functions:
- `main()` - Entry point at `rhubarb/src/rhubarb/main.cpp:139`
- Creates recognizer based on type
- Calls `animateWaveFile()` which loads audio via `createAudioFileClip()`

### 2. Audio Processing (Voice Activity Detection)

#### Voice Activity Detection (`audio/voiceActivityDetection.cpp`)
Detects speech segments in the loaded audio:

**Sub-phases:**
1. **Audio Preparation**: Resample to 8kHz, remove DC offset
2. **WebRTC VAD Processing**: Frame-by-frame activity detection
3. **Activity Refinement**: Fill gaps, remove short segments
4. **Timeline Generation**: Create voice activity boundaries

- Uses WebRTC VAD library
- Configurable aggressiveness (0-3)
- Configuration via `PauseDetectionConfig`:
  - `vadMaxGap`: Maximum gap to fill (default: 60ms)
  - `vadMinSegmentLength`: Minimum segment to keep (default: 30ms)
  - `vadAggressiveness`: WebRTC VAD level (default: 2)

### 3. Speech Recognition

The speech recognition phase converts audio segments into phonemes with precise timing. This phase includes:
- Decoder initialization with language models
- Parallel utterance processing
- Word recognition (for word-based recognizers)
- Phoneme extraction with timing
- Noise sound detection for unrecognized segments

Three recognizer implementations:

#### PocketSphinx Recognizer (`recognition/PocketSphinxRecognizer.cpp`)
- Default recognizer for English
- Uses CMU PocketSphinx with acoustic model
- Creates biased language model if dialog text provided
- Performs forced alignment to get phoneme timings
- Dictionary-based with G2P fallback for unknown words
- Detects noise sounds in utterances without recognized phones

#### Phonetic Recognizer (`recognition/PhoneticRecognizer.cpp`)
- Language-independent recognizer
- Uses allphone mode for phoneme detection
- No word recognition, only individual sounds
- Better for non-English audio

#### WordTiming Recognizer (`recognition/WordTimingRecognizer.cpp`)
- Uses pre-computed word/character timings
- Performs forced alignment based on known boundaries
- Most accurate when timing data available

**For detailed breakdown of all speech recognition sub-steps, see [SPEECH_RECOGNITION_PHASE.md](SPEECH_RECOGNITION_PHASE.md)**

### 4. Animation Generation

The animation phase transforms phonemes into visual mouth shapes:

#### Shape Rules (`animation/ShapeRule.cpp`)
- `getShapeRules()` creates timeline of shape rules from phonemes
- Maps each phoneme to possible mouth shapes
- Detects sentence boundaries in fast speech
- Inserts micro-pauses for visual separation
- Each phoneme can map to multiple valid shapes

#### Animation Rules (`animation/animationRules.cpp`)
Key mappings:
```cpp
// Vowels
AO → E (rounded)
AA → D (wide open)
IY → B (teeth)
UW → F (puckered)
EH → C (open)
AH → C/D (duration-dependent)

// Consonants
P/B → A (closed) with plosive
F/V → G (teeth on lip)
L → H (tongue raised)
S/Z/SH/ZH → B/F (teeth/puckered)

// Diphthongs (two shapes)
EY → C→B
AY → C/D→B
OW → E→F
```

#### Main Animation Pipeline (`animation/mouthAnimation.cpp`)
The `animate()` function orchestrates these steps:
1. Create shape rules from phonemes (`getShapeRules()`)
2. Convert to target shape set (add Shape::X for pauses)
3. Generate rough animation (`animateRough()`)
4. Optimize timing (`optimizeTiming()`, optional)
5. Animate pauses (`animatePauses()`)
6. Insert tweens (`insertTweens()`, optional)
7. Apply word simplification (`simplifyByDensity()`, optional)
8. Final conversion to target shape set

#### Rough Animation (`animation/roughAnimation.cpp`)
- Initial shape assignment
- Selects best shape from available options
- No timing optimization yet

#### Timing Optimization (`animation/timingOptimization.cpp`)
- Consolidates very short shapes
- Minimum shape duration: 7cs (centiseconds)
- Selects representative shape for merged segments
- Can be skipped with `--skipTimingOptimization`

#### Pause Animation (`animation/pauseAnimation.cpp`)
- Handles silence periods
- Inserts appropriate closed shapes (A or X)
- Configurable micro-pause detection

#### Tweening (`animation/tweening.cpp`)
- Inserts transition shapes between main shapes
- Smooths animation
- Can be disabled with `--noTweening`
- Uses predefined tween rules (e.g., D→A inserts C)

#### Word Simplification (`animation/wordSimplification.cpp`)
- Limits visemes per word
- Controlled by `--maxVisemesPerWord`
- Keeps most prominent shapes

### 5. Export

#### Export Formats

**JSON** (`exporters/JsonExporter.cpp`):
```json
{
  "metadata": {
    "soundFile": "/path/to/audio.wav",
    "duration": 3.24
  },
  "mouthCues": [
    { "start": 0.00, "end": 0.12, "value": "X" },
    { "start": 0.12, "end": 0.24, "value": "B" }
  ]
}
```

**TSV** (`exporters/TsvExporter.cpp`):
```
0.00	X
0.12	B
0.24	C
```

**XML** (`exporters/XmlExporter.cpp`):
```xml
<rhubarbResult>
  <metadata>
    <soundFile>/path/to/audio.wav</soundFile>
    <duration>3.24</duration>
  </metadata>
  <mouthCues>
    <mouthCue start="0.00" end="0.12">X</mouthCue>
  </mouthCues>
</rhubarbResult>
```

**DAT** (`exporters/DatExporter.cpp`):
- Moho/OpenToonz format
- Frame-based output
- Optional Preston Blair naming

## Phase Boundaries and Responsibilities

### Speech Recognition Phase
**Outputs**: Timeline of phonemes with precise timing
**Responsibilities**:
- Voice activity detection
- Word recognition (if applicable)
- Phoneme extraction and alignment
- Noise sound detection for unrecognized segments
- Parallel utterance processing

**Does NOT handle**:
- Shape rules or visual decisions
- Sentence boundary detection
- Any mouth shape mapping

### Animation Phase
**Input**: Timeline of phonemes from speech recognition
**Outputs**: Timeline of mouth shapes (visemes)
**Responsibilities**:
- Shape rule generation from phonemes
- Sentence boundary detection and micro-pauses
- Rough animation generation
- Timing optimization
- Pause animation
- Tweening between shapes
- Word-based simplification

## Key Components

### Core Data Structures

#### Shape Enum (`core/Shape.h`)
```cpp
enum class Shape {
    A,  // Closed mouth (M, B, P)
    B,  // Clenched teeth (consonants, EE)
    C,  // Open mouth (EH, AE)
    D,  // Wide open (AA, AH)
    E,  // Rounded (AO, ER)
    F,  // Puckered (UW, OW, W)
    G,  // F/V (teeth on lip)
    H,  // L (tongue raised)
    X   // Idle/rest
}
```

#### Timeline Classes
- `Timeline<T>`: Basic timeline container
- `BoundedTimeline<T>`: Timeline with defined range
- `ContinuousTimeline<T>`: No gaps allowed
- `JoiningTimeline<T>`: Automatically joins adjacent segments

#### Phone Enum (`core/Phone.h`)
- 39 phonemes based on CMU pronunciation dictionary
- Maps to standard phonetic alphabet
- Special values: Noise, Silence

### Configuration

#### PauseDetectionConfig
Singleton configuration for pause detection:
- Speech speed presets: "slow", "normal", "fast"
- VAD parameters
- Micro-pause thresholds
- Sentence boundary detection

#### Command-Line Arguments

Main parameters:
- `-r, --recognizer`: PocketSphinx (default), Phonetic, or WordTiming
- `-f, --exportFormat`: tsv (default), xml, json, dat
- `-d, --dialogFile`: Text file with dialog
- `--extendedShapes`: Which optional shapes to use (default: "GHX")
- `--threads`: Worker thread count
- `--noTweening`: Disable shape transitions
- `--skipTimingOptimization`: Keep all short visemes
- `--maxVisemesPerWord`: Limit shapes per word

Speech speed and VAD:
- `--speechSpeed`: slow/normal/fast presets
- `--vadMaxGap`: Max gap to fill (ms)
- `--vadMinSegment`: Min segment length (ms)
- `--vadAggressiveness`: WebRTC VAD level (0-3)

## Implementation Details

### File Organization

```
rhubarb/src/
├── rhubarb/          # Main application
│   ├── main.cpp     # Entry point
│   ├── RecognizerType.cpp
│   └── ExportFormat.cpp
├── animation/        # Animation generation
│   ├── mouthAnimation.cpp
│   ├── ShapeRule.cpp
│   ├── animationRules.cpp
│   ├── timingOptimization.cpp
│   └── tweening.cpp
├── audio/           # Audio processing
│   ├── AudioClip.cpp
│   ├── voiceActivityDetection.cpp
│   └── audioFileReading.cpp
├── recognition/     # Speech recognition
│   ├── PocketSphinxRecognizer.cpp
│   ├── PhoneticRecognizer.cpp
│   └── WordTimingRecognizer.cpp
├── exporters/       # Output formats
│   ├── JsonExporter.cpp
│   ├── TsvExporter.cpp
│   └── XmlExporter.cpp
├── core/           # Core types
│   ├── Shape.cpp
│   └── Phone.cpp
└── lib/            # Main library
    ├── rhubarbLib.cpp
    └── rhubarbLib.h
```

### Processing Flow Functions

1. **Main Pipeline**:
   ```cpp
   main() → animateWaveFile() → animateAudioClip() → 
   recognizer.recognizePhones() → animate() → exporter.exportAnimation()
   ```
   - `recognizePhones()` returns `BoundedTimeline<Phone>`
   - `animate()` takes phones and returns `JoiningContinuousTimeline<Shape>`

2. **Speech Recognition Pipeline** (`pocketSphinxTools.cpp`):
   ```cpp
   recognizePhones() → detectVoiceActivity() → 
   parallel: utteranceToPhones() → [
     recognizeWords() → getPhoneAlignment() → getNoiseSounds()
   ] → merge results to BoundedTimeline<Phone>
   ```
   - Each utterance processed in parallel
   - `getNoiseSounds()` called per utterance, not globally
   - Results merged via mutex-protected timeline

3. **Animation Pipeline** (`mouthAnimation.cpp`):
   ```cpp
   animate(phones) → 
     getShapeRules(phones) →           // First step: convert phones to shape rules
     animateRough(shapeRules) → 
     optimizeTiming(animation) → 
     animatePauses(animation) → 
     insertTweens(animation) → 
     simplifyByDensity(animation) → 
     convertToTargetShapeSet(animation)
   ```
   - Input: phonemes with timing
   - Output: visemes with timing
   - All visual decisions made here

### Key Algorithms

#### Shape Selection (`animationRules.cpp`)
- Effort matrix for shape transitions
- Closest shape selection based on effort
- Considers visual similarity and transition smoothness
- Part of ANIMATION phase, not speech recognition

#### Timing Optimization (`timingOptimization.cpp`)
- Minimum duration: 7 centiseconds
- Representative shape selection by duration
- Prioritizes visually distinct shapes (D over C)
- Operates on shapes, not phonemes

#### Tween Generation (`tweening.cpp`)
- Predefined transition rules
- Early/centered/late timing options
- Asymmetric rules (fast open, slow close)
- Purely visual animation concern

#### Noise Detection (`pocketSphinxTools.cpp::getNoiseSounds`)
- Finds utterance segments without recognized phones
- Minimum duration threshold: 12 centiseconds
- Executed WITHIN utterance processing loop
- Part of SPEECH RECOGNITION phase

### Performance Considerations

- Multi-threaded processing (configurable per utterance)
- Progress reporting via ProgressSink
- Efficient timeline operations with joining
- Audio resampling only when needed (8kHz for VAD, 16kHz for recognition)
- Caching of language models
- Decoder pooling for efficient parallel processing
- Mutex-protected timeline updates for thread safety

### Error Handling

- Exceptions with nested context
- Detailed logging at multiple levels
- Graceful fallbacks for missing features
- Input validation at entry point

## Usage Examples

### Basic Usage
```bash
./rhubarb audio.wav -o output.json
```

### With Dialog Text
```bash
./rhubarb audio.wav -d dialog.txt -o output.json
```

### Phonetic Mode (Non-English)
```bash
./rhubarb audio.wav -r phonetic -o output.json
```

### Fast Speech Settings
```bash
./rhubarb audio.wav --speechSpeed fast --vadMaxGap 40 -o output.json
```

### Simplified Animation
```bash
./rhubarb audio.wav --maxVisemesPerWord 2 --noTweening -o output.json
```

### Voice Activity Only
```bash
./rhubarb audio.wav -f voiceActivity -o vad.json
```

## Notes for Development

### Adding New Recognizers
1. Inherit from `Recognizer` base class
2. Implement `recognizePhones()` method
3. Add to `RecognizerType` enum
4. Update `createRecognizer()` in main.cpp

### Adding New Export Formats
1. Inherit from `Exporter` base class
2. Implement `exportAnimation()` method
3. Add to `ExportFormat` enum
4. Update `createExporter()` in main.cpp

### Modifying Animation Rules
1. Edit phoneme mappings in `animationRules.cpp`
2. Adjust shape sets for each phoneme
3. Modify tween rules in `getTween()`
4. Update effort matrix for shape transitions

### Performance Tuning
- Adjust `minShapeDuration` in timing optimization
- Modify VAD parameters in PauseDetectionConfig
- Change thread count for parallel processing
- Tune recognizer beam widths

This documentation provides a complete reference for understanding and modifying the Rhubarb Lip Sync pipeline without needing to read through all source files.