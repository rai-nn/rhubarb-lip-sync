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
Audio Input → Voice Activity Detection → Speech Recognition → Phoneme Detection → 
Shape Rule Generation → Animation → Optimization → Export
```

## Pipeline Stages

### 1. Entry Point and Initialization (`main.cpp`)

The main entry point handles:
- Command-line argument parsing using TCLAP
- Logging setup (file, console, machine-readable modes)
- Recognizer selection (PocketSphinx, Phonetic, or WordTiming)
- Export format selection (JSON, XML, TSV, DAT)
- Threading configuration

Key entry functions:
- `main()` - Entry point at `rhubarb/src/rhubarb/main.cpp:139`
- Creates recognizer based on type
- Calls `animateWaveFile()` or voice activity detection

### 2. Audio Processing

#### Audio Loading (`audio/audioFileReading.cpp`)
- Supports WAV and OGG Vorbis formats
- Creates `AudioClip` object containing audio samples
- Handles resampling to required sample rates (8kHz for VAD, 16kHz for recognition)

#### Voice Activity Detection (`audio/voiceActivityDetection.cpp`)
- Uses WebRTC VAD library
- Configurable aggressiveness (0-3)
- Fills small gaps and removes short segments
- Configuration via `PauseDetectionConfig`:
  - `vadMaxGap`: Maximum gap to fill (default: 60ms)
  - `vadMinSegmentLength`: Minimum segment to keep (default: 30ms)
  - `vadAggressiveness`: WebRTC VAD level (default: 2)

### 3. Speech Recognition

Three recognizer implementations:

#### PocketSphinx Recognizer (`recognition/PocketSphinxRecognizer.cpp`)
- Default recognizer for English
- Uses CMU PocketSphinx with acoustic model
- Creates biased language model if dialog text provided
- Performs forced alignment to get phoneme timings
- Dictionary-based with G2P fallback for unknown words

#### Phonetic Recognizer (`recognition/PhoneticRecognizer.cpp`)
- Language-independent recognizer
- Uses allphone mode for phoneme detection
- No word recognition, only individual sounds
- Better for non-English audio

#### WordTiming Recognizer (`recognition/WordTimingRecognizer.cpp`)
- Uses pre-computed word/character timings
- Performs forced alignment based on known boundaries
- Most accurate when timing data available

### 4. Phoneme-to-Shape Mapping

#### Shape Rules (`animation/ShapeRule.cpp`)
- Maps phonemes to possible mouth shapes
- `getShapeRules()` creates timeline of shape rules
- Handles sentence boundaries and micro-pauses
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

### 5. Animation Generation

#### Main Animation (`animation/mouthAnimation.cpp`)
Pipeline within `animate()`:
1. Create shape rules from phonemes
2. Convert to target shape set
3. Generate rough animation
4. Optimize timing (optional)
5. Animate pauses
6. Insert tweens (optional)
7. Apply word simplification (optional)

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

### 6. Export

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

2. **Animation Pipeline**:
   ```cpp
   animate() → getShapeRules() → animateRough() → 
   optimizeTiming() → animatePauses() → insertTweens() → 
   simplifyByDensity() → convertToTargetShapeSet()
   ```

3. **Recognition Pipeline**:
   ```cpp
   recognizePhones() → detectVoiceActivity() → 
   utteranceToPhones() → getPhoneAlignment()
   ```

### Key Algorithms

#### Shape Selection (`animationRules.cpp`)
- Effort matrix for shape transitions
- Closest shape selection based on effort
- Considers visual similarity and transition smoothness

#### Timing Optimization
- Minimum duration: 7 centiseconds
- Representative shape selection by duration
- Prioritizes visually distinct shapes (D over C)

#### Tween Generation
- Predefined transition rules
- Early/centered/late timing options
- Asymmetric rules (fast open, slow close)

### Performance Considerations

- Multi-threaded processing (configurable)
- Progress reporting via ProgressSink
- Efficient timeline operations with joining
- Audio resampling only when needed
- Caching of language models

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