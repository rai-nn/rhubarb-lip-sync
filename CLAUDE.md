# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Rhubarb Lip Sync is a 2D mouth animation tool that analyzes audio files, recognizes speech, and automatically generates lip sync information. It's written in C++17 and uses CMake as the build system.

## Build Commands

### Recommended: Automated Build Script
Use the `build-local.sh` script for building and deploying to the visemes server:

```bash
# Build and deploy to ../lib/generators/implementations/rhubarb/binary/
./build-local.sh

# Clean build and deploy
./build-local.sh --clean

# Clean build, deploy, and test
./build-local.sh --clean --test
```

This script automatically:
- Builds only the Rhubarb executable (avoiding Spine integration issues)
- Copies the binary to the visemes server location
- Works on both macOS and Linux
- Handles all configuration automatically

### Manual Build (Alternative Method)
Use manual build only when you need specific control over the build process:

#### Quick Build (All Platforms)
```bash
# Build just the main Rhubarb executable (recommended)
cd build
cmake --build . --config Release --target rhubarb

# The executable will be at: build/rhubarb/rhubarb
```

#### macOS
```bash
# Build release version with packaging (builds everything)
./package-osx.sh

# Or manually:
mkdir build && cd build
cmake .. -G Xcode
cmake --build . --config Release --target rhubarb  # Main executable only
# OR
cmake --build . --config Release  # Build everything (may fail on Spine integration)
```

#### Linux
```bash
mkdir build && cd build
cmake .. -D CMAKE_C_COMPILER=gcc-10 -D CMAKE_CXX_COMPILER=g++-10
cmake --build . --config Release --target rhubarb  # Main executable only
# OR
cmake --build . --config Release  # Build everything (may fail on Spine integration)
```

## Build Quick Reference

- **Preferred**: Use `./build-local.sh` for automated build and deployment
- To build just the Rhubarb executable manually: `cd build && cmake --build . --target rhubarb`
- The executable will be located at: `build/rhubarb/rhubarb`
- Use `--target rhubarb` to avoid Java version issues with the Spine integration component

## Testing

```bash
# Build and run tests
cd build
cmake --build . --config Release --target runTests
./runTests

# Run example test comparison
./test-comparison/run_test.sh

# Test different speech speeds
./test-comparison/test_speech_speeds.sh
```

## Architecture

### Core Components

- **rhubarb/src/rhubarb/**: Main application entry point and CLI
  - `main.cpp`: Command-line interface and argument parsing
  - `RecognizerType.h/cpp`: Speech recognizer selection (PocketSphinx or Phonetic)
  - `PauseDetectionConfig.h`: Configuration for silence detection

- **rhubarb/src/recognition/**: Speech recognition implementations
  - `PocketSphinxRecognizer`: Default recognizer using CMU PocketSphinx
  - `PhoneticRecognizer`: Alternative phonetic-based recognizer
  - `g2p.cpp`: Grapheme-to-phoneme conversion

- **rhubarb/src/animation/**: Mouth shape animation logic
  - `mouthAnimation.cpp`: Core animation generation
  - `animationRules.h`: Rules for shape transitions
  - `roughAnimation.cpp`: Initial animation pass
  - `timingOptimization.cpp`: Animation timing refinement
  - `targetShapeSet.cpp`: Target mouth shape definitions

- **rhubarb/src/audio/**: Audio processing
  - `AudioClip.cpp`: Audio data representation
  - `voiceActivityDetection.cpp`: Voice/silence detection
  - `WaveFileReader.cpp` / `OggVorbisFileReader.cpp`: Audio file readers

- **rhubarb/src/exporters/**: Output format exporters
  - JSON, XML, TSV, and DAT format exporters

### Dependencies (in rhubarb/lib/)
- CMU PocketSphinx & sphinxbase for speech recognition
- Ogg/Vorbis for audio decoding
- Google Test for unit testing
- TCLAP for command-line parsing
- cppformat for string formatting

### Integration Extensions (in extras/)
- Adobe After Effects script integration
- Spine animation tool integration
- Vegas Pro plugin scripts

## Key Implementation Details

- Uses 6-9 mouth shapes (A-H, X) based on Preston Blair phoneme system
- Supports WAV and OGG audio formats
- Recognizers work with phonemes that are mapped to visual mouth shapes
- Animation timing is optimized to reduce rapid shape changes
- Configurable pause/silence detection for better animation quality

## Development Notes

- C++17 standard required
- CMake 3.24+ required
- Boost headers required (1.54+)
- Cross-platform: macOS (Xcode), Linux (GCC/Clang)
- CI/CD via GitHub Actions (.github/workflows/ci.yml)
- when running rhubarb command line for testing purposes is needed, you can find wav files in /resources/audio.wav.
- When you need to run rhubarb command line, you SHOULD use the local built version from ./build/rhubarb/ folder.