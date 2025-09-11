# Speech Recognition Phase - Detailed Sub-Steps

This document provides a comprehensive breakdown of all sub-steps in the Rhubarb Lip Sync speech recognition phase, based on analysis of the source code.

Speech recognition phase is 3. phase in overall Rhubarb pipeline.

## Overview

The speech recognition phase converts audio input into a timeline of phonemes with precise timing information. This phase outputs ONLY phonemes with timing - shape rules and visual decisions are handled in the subsequent Animation phase. The speech recognition involves multiple complex subsystems working in concert.

## Phase 1: Voice Activity Detection (VAD)

### 1. Audio Preparation
- Resample audio to 8kHz (WebRTC VAD requirement)
- Remove DC offset to normalize audio signal
- Create audio clip object with truncated range

### 2. WebRTC VAD Processing
- Initialize WebRTC VAD handle
- Set aggressiveness level (0-3, configurable via `vadAggressiveness`)
- Process audio in 10ms frames (80 samples at 8kHz)
- Analyze internal VAD flag for activity detection

### 3. Activity Refinement
- Fill small gaps between active segments (default: 60ms via `vadMaxGap`)
- Remove short segments below threshold (default: 30ms via `vadMinSegmentLength`)
- Generate bounded timeline of voice activity segments
- Log segment statistics (speech vs silence counts)

## Phase 2: Decoder Initialization

### 4. Configuration Setup
- Load acoustic model from `res/sphinx/acoustic-model`
- Load pronunciation dictionary (`cmudict-en-us.dict`)
- Configure decoder parameters:
  - Enable dithering for noise resistance
  - Disable automatic VAD (manual control)
  - Enable batch cepstral mean normalization

### 5. Language Model Selection
- **Without dialog**: Load default language model (`en-us.lm.bin`)
- **With dialog**:
  - Tokenize dialog text
  - Create dialog-specific language model
  - Build weighted model (90% dialog, 10% default)

### 6. Dictionary Enhancement
- Check words against CMU dictionary
- For missing words: Apply G2P (grapheme-to-phoneme) conversion
- Add pronunciations to decoder dictionary dynamically

## Phase 3: Parallel Utterance Processing

### 7. Thread Pool Management
- Create decoder pool (lazy initialization)
- Allocate threads based on utterance count and max thread setting
- Track decoder creation timing and utilization

### 8. Per-Utterance Processing
- Segment audio by VAD boundaries
- Add 30ms padding before/after each utterance
- Resample to 16kHz (PocketSphinx requirement)
- Convert to 16-bit PCM buffer

## Phase 4: Utterance-Level Processing (Per Recognizer Type)

### PocketSphinxRecognizer (Word-based)

#### 9. Word Recognition
- Start utterance processing (`ps_start_utt`)
- Process raw audio through decoder
- Search for word boundaries using n-gram model
- Extract word hypotheses with timing

#### 10. Word-to-Phoneme Mapping
- Fix pronunciation variants (e.g., "into(2)" → "into")
- Convert word strings to word IDs
- Handle compound words and contractions

#### 11. Forced Alignment
- Create alignment structure (`ps_alignment_t`)
- Populate with recognized word IDs
- Initialize state alignment search
- Process audio frames through acoustic model
- Step through Viterbi search for optimal path

#### 12. Phone Extraction
- Iterate through alignment results
- Extract phone IDs and timing (start/duration)
- Convert phone names to Phone enum
- Apply heuristics (e.g., AH < 6cs → Schwa)
- Skip silence phones (SIL)

#### 13. Noise Sound Detection (PocketSphinx)
- Call `getNoiseSounds()` for utterance
- Find utterance segments without recognized phones
- Apply minimum duration threshold (12cs)
- Mark gaps as Phone::Noise in timeline
- Occurs WITHIN utterance processing, not post-processing

### PhoneticRecognizer (Sound-based)

#### 9. Phonetic Recognition
- Use allphone mode configuration
- Set language model weight (0.8)
- Configure beam widths for phonetic search
- Process audio directly for phones (no word step)

#### 10. Direct Phone Detection
- Recognize phones as "words" in allphone mode
- Convert phone strings to Phone enum
- Apply duration-based heuristics

#### 11. Noise Sound Detection (Phonetic)
- Apply same noise detection as PocketSphinx
- Mark unrecognized segments as Phone::Noise

### WordTimingRecognizer (Pre-timed)

#### 9. Timing-based Alignment
- Use provided word/character timings
- Prepare word list from timing data
- Add missing words via G2P if needed

#### 10. Forced Alignment with Known Boundaries
- Convert words to word IDs
- Perform alignment using known timing constraints
- Extract phones within word boundaries

#### 11. Space Character Processing
- Identify space characters from character timings
- Convert spaces to silence gaps in timeline
- Note: Visual shape decisions (X visemes) happen in Animation phase, not here

## Phase 5: Timeline Assembly and Metrics

### 12. Timeline Merging
- Results from parallel utterances added to shared timeline
- Mutex-protected concurrent access
- Natural merging as each utterance completes
- No separate "assembly" phase - happens during processing

### 13. Logging and Metrics
- Log each phone with timestamp
- Track processing duration per utterance
- Generate phase completion statistics
- Save detailed logs for debugging
- Log decoder creation timing and pool statistics

## G2P (Grapheme-to-Phoneme) Utility Subsystem

**Note**: G2P is not a sequential phase but an on-demand utility used during:
- Language model creation (when dialog text provided)
- Dictionary enhancement (for unknown words)
- Word timing recognizer setup

### G2P Processing Steps

#### Text Normalization
- Validate word contains only a-z and apostrophe
- Convert to wide string for rule processing

#### Rule Application
- Apply 2000+ regex replacement rules
- Handle special cases (bigrams, diphthongs)
- Iterate until no more changes

#### Phone Conversion
- Map characters to CMU phone set
- Remove duplicate consecutive phones
- Return phone sequence for word

## Key Configuration Parameters

| Parameter | Description | Default | Range/Type | Phase |
|-----------|-------------|---------|------------|-------|
| `vadAggressiveness` | How aggressively to remove silence | 2 | 0-3 | VAD |
| `vadMaxGap` | Maximum gap to fill between segments (ms) | 60 | integer | VAD |
| `vadMinSegmentLength` | Minimum segment to keep (ms) | 30 | integer | VAD |
| `maxThreadCount` | Parallel processing threads | system | integer | Recognition |
| `recognizer` | Recognition algorithm | PocketSphinx | PocketSphinx/Phonetic/WordTiming | Setup |
| `dialog` | Optional text for biased recognition | none | string | Setup |

## Implementation Files

### Core Recognition Components
- `rhubarb/src/recognition/PocketSphinxRecognizer.cpp` - Word-based recognizer with noise detection
- `rhubarb/src/recognition/PhoneticRecognizer.cpp` - Sound-based recognizer with noise detection
- `rhubarb/src/recognition/WordTimingRecognizer.cpp` - Pre-timed recognizer
- `rhubarb/src/recognition/pocketSphinxTools.cpp` - Shared utilities including `getNoiseSounds()`

### Voice Activity Detection
- `rhubarb/src/audio/voiceActivityDetection.cpp` - WebRTC VAD implementation

### G2P System
- `rhubarb/src/recognition/g2p.cpp` - Grapheme-to-phoneme conversion
- `rhubarb/src/recognition/g2pRules.cpp` - Rule definitions (included)

### Supporting Components
- `rhubarb/src/recognition/languageModels.cpp` - Language model creation
- `rhubarb/src/recognition/tokenization.cpp` - Text tokenization
- `rhubarb/src/audio/processing.cpp` - Audio processing utilities

## Processing Flow

```
Audio Input
    ↓
Voice Activity Detection (VAD)
    ↓
Utterance Segmentation
    ↓
Parallel Processing Setup
    ↓
For Each Utterance (Parallel):
    ├─→ PocketSphinx: Words → Alignment → Phones → Noise Detection
    ├─→ Phonetic: Direct Phone Recognition → Noise Detection
    └─→ WordTiming: Known Boundaries → Alignment → Phones
    ↓
Concurrent Timeline Updates (Mutex-Protected)
    ↓
Final Phone Timeline (Output to Animation Phase)
```

## Performance Considerations

1. **Parallel Processing**: Multiple utterances processed simultaneously using thread pool
2. **Decoder Pooling**: Decoders reused across utterances to reduce initialization overhead
3. **Lazy Initialization**: Decoders created only when needed
4. **Optimized VAD**: Direct access to internal VAD flags for accurate detection
5. **Caching**: Language models cached when using dialog text

## Error Handling

- Graceful fallback for failed alignments (returns noise timeline)
- G2P fallback for unknown words
- Automatic pronunciation variant handling
- Robust handling of silence and non-speech segments

## Key Implementation Notes

1. **Phase Boundary**: The speech recognition phase ends with a timeline of phonemes. All visual decisions (shape rules, sentence boundaries, tweening) happen in the Animation phase.

2. **Noise Detection Placement**: Noise sound detection (`getNoiseSounds()`) happens WITHIN each utterance processing, not as a separate post-processing step.

3. **G2P Usage**: G2P is not a sequential phase but a utility called on-demand when unknown words are encountered.

4. **Parallel Processing**: Utterances are processed in parallel with results naturally merged via mutex-protected timeline updates.

5. **No Shape Rules Here**: Despite some documentation suggesting otherwise, speech recognition never generates shape rules - it only outputs phonemes with timing.

This comprehensive breakdown shows how Rhubarb's speech recognition phase orchestrates multiple complex subsystems to convert raw audio into precisely-timed phoneme sequences, ready for animation generation. The system is designed to be flexible (supporting multiple recognizer types), robust (with multiple fallback strategies), and performant (with parallel processing and pooling).
