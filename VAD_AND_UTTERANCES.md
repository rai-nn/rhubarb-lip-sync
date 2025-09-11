# Voice Activity Detection (VAD) and Utterances in Rhubarb

## Overview

This document explains how Rhubarb detects voice activity, creates utterances, and handles special non-speech sounds during the speech recognition pipeline.

## VAD to Utterances Pipeline

### 1. Raw Audio Analysis
The VAD analyzes audio in 10ms frames using WebRTC VAD, marking each frame as active or inactive:

```
Time:     0    10   20   30   40   50   60   70   80   90  100  110  120  130 (ms)
Activity: [OFF][OFF][ON ][ON ][ON ][OFF][OFF][OFF][ON ][ON ][ON ][ON ][OFF][OFF]
```

### 2. Gap Filling
Small gaps between active segments ≤ `vadMaxGap` are filled to prevent splitting continuous speech:

```
Before:   [ON ][ON ][ON ][OFF][OFF][OFF][ON ][ON ]
          └─segment 1─┘   └─40ms gap─┘   └segment 2┘

After:    [ON ][ON ][ON ][ON ][ON ][ON ][ON ][ON ]
          └────────merged segment──────────┘
```

### 3. Short Segment Removal
Segments shorter than `vadMinSegmentLength` are discarded as noise:

```
Before:   [ON ][ON ][OFF]...[OFF][ON ][OFF]...[OFF][ON ][ON ][ON ][ON ]
          └─20ms─┘            └10ms┘              └─────40ms──────┘

After:    [OFF][OFF][OFF]...[OFF][OFF][OFF]...[OFF][ON ][ON ][ON ][ON ]
          (removed)           (removed)            └─────kept──────┘
```

### 4. Final Utterances
The remaining continuous segments become utterances directly:

```
VAD Activity Timeline:
[────segment1────]  gap  [seg2]  gap  [────segment3────]  gap  [──segment4──]

Becomes Utterances:
Utterance 1          ✗    Utt 2   ✗    Utterance 3         ✗    Utterance 4
```

## Key VAD Parameters

### vadMinSegmentLength (default: 30ms)
- **Purpose**: Minimum duration for a detected speech segment to be kept
- **Effect**: Segments shorter than this are discarded as noise
- **Example**: With 30ms, a 20ms blip is ignored, but 110ms segment is kept

### vadMaxGap (default: 60ms)
- **Purpose**: Maximum gap between segments that will be automatically filled
- **Effect**: Gaps smaller than this are filled, joining segments together
- **Example**: With 60ms, a 50ms pause between words gets filled, but 70ms gap creates separate utterances

### vadAggressiveness (default: 2, range: 0-3)
- **Purpose**: WebRTC VAD sensitivity level
- **Effect**: Higher values cut off more aggressively (more likely to classify as silence)
- **Example**: Use 3 for noisy environments, 1 for clean studio recordings

## Real-World Example: The 110ms Problem

From actual processing of `resources/audio.wav`:

```
Timeline:
[Utterance 1: 0.17-2.61s] <--70ms--> [Noise: 2.68-2.79s] <--150ms--> [Utterance 3: 2.94-6.65s]
"alright guys let's..."              (110ms, no text)              "three strong green bars..."
```

### Why the empty 110ms utterance exists:
1. **VAD detected activity** at 2.68-2.79s (110ms duration)
2. **Not filtered**: 110ms > 30ms `vadMinSegmentLength` → KEPT
3. **Not merged**: 
   - 70ms gap before > 60ms `vadMaxGap` → NOT merged with Utterance 1
   - 150ms gap after > 60ms `vadMaxGap` → NOT merged with Utterance 3
4. **Result**: Isolated utterance with no recognizable speech (wastes ~2s processing)

### Solutions:
```bash
# Option 1: Increase minimum segment to discard short noises
./rhubarb audio.wav --vadMinSegment 150

# Option 2: Increase max gap to merge with neighbors
./rhubarb audio.wav --vadMaxGap 100

# Option 3: Both for balanced approach
./rhubarb audio.wav --vadMinSegment 120 --vadMaxGap 80
```

## Special Non-Speech Phones

Rhubarb recognizes 4 types of non-speech sounds that are important for lip-sync:

### Phone Types

| Phone Type | Appears in Text As | Viseme Shape | Description |
|------------|-------------------|--------------|-------------|
| `Phone::Breath` | `[BREATH]` | C (open mouth) | Breathing sounds |
| `Phone::Cough` | `[COUGH]` | C (open mouth) | Coughing sounds |
| `Phone::Smack` | `[SMACK]` | C (open mouth) | Lip smacking/mouth sounds |
| `Phone::Noise` | `[NOISE]` | B (slightly open) | General non-speech noise |

### Detection Methods

1. **Direct Recognition**: PocketSphinx recognizes special dictionary entries
   - `+BREATH+` in recognition → `[BREATH]` in output
   - `+COUGH+` in recognition → `[COUGH]` in output
   - `+SMACK+` in recognition → `[SMACK]` in output

2. **Noise Detection Algorithm** (`getNoiseSounds()`):
   - Finds utterance segments with no recognized phones
   - Gaps ≥ 120ms are marked as `Phone::Noise`
   - Catches unrecognized sounds between words

3. **Fallback**: When alignment fails, entire utterance → `Phone::Noise`

### Example in Output
```
"three strong green bars climbing from fifty eighty to fifty two ten [BREATH]"
```
PocketSphinx detected a breathing sound at the end and marked it as `[BREATH]`.

## Speech Speed Presets

Different speech speeds require different VAD settings:

### Slow Speech
```cpp
vadMaxGap = 100ms           // More tolerance for pauses
vadMinSegmentLength = 50ms  // Keep longer segments only
```

### Normal Speech (default)
```cpp
vadMaxGap = 60ms
vadMinSegmentLength = 30ms
```

### Fast Speech
```cpp
vadMaxGap = 40ms            // Less tolerance for gaps
vadMinSegmentLength = 20ms  // Keep even short segments
```

## Command-Line Options

### VAD Parameters
```bash
--vadAggressiveness <0-3>   # WebRTC VAD level (higher = more aggressive)
--vadMaxGap <ms>            # Max gap to fill between segments
--vadMinSegment <ms>        # Min segment length to keep
--speechSpeed <slow|normal|fast>  # Preset configurations
```

### Examples
```bash
# For clean audio with clear speech
./rhubarb audio.wav --vadAggressiveness 1 --vadMinSegment 100

# For noisy audio with fast speech
./rhubarb audio.wav --vadAggressiveness 3 --speechSpeed fast

# To eliminate short noise segments
./rhubarb audio.wav --vadMinSegment 200 --vadMaxGap 100
```

## Troubleshooting

### Problem: Empty utterances wasting processing time
**Symptoms**: Short utterances (100-200ms) with no recognized text
**Solution**: Increase `--vadMinSegment` to 150-200ms

### Problem: Words being split across utterances
**Symptoms**: Single words or phrases unnaturally divided
**Solution**: Increase `--vadMaxGap` to 80-100ms

### Problem: Too much being cut off
**Symptoms**: Word beginnings/endings missing
**Solution**: Decrease `--vadAggressiveness` to 1 or 0

### Problem: Background noise creating utterances
**Symptoms**: Many utterances with only `[NOISE]` or empty text
**Solution**: Increase `--vadAggressiveness` to 3

## Technical Implementation Details

### File Locations
- VAD implementation: `rhubarb/src/audio/voiceActivityDetection.cpp`
- Configuration: `rhubarb/src/rhubarb/PauseDetectionConfig.h`
- Phone definitions: `rhubarb/src/core/Phone.h`
- Utterance processing: `rhubarb/src/recognition/pocketSphinxTools.cpp`

### Processing Flow
1. `detectVoiceActivity()` creates utterance timeline
2. Each utterance processed in parallel by thread pool
3. `utteranceToPhones()` performs recognition
4. Empty/noise segments marked with `Phone::Noise`
5. Special sounds (`[BREATH]`, etc.) preserved in output

### Performance Impact
- Each utterance has overhead regardless of content
- Empty 110ms utterance can take 2+ seconds to process
- Thread scheduling, decoder init, and PocketSphinx processing all add latency
- Proper VAD tuning can eliminate wasted processing time

## Best Practices

1. **Analyze your audio first**: Look at the utterance timeline in detailed output to spot issues
2. **Start with defaults**: Only adjust if you see problems
3. **Adjust incrementally**: Change one parameter at a time by small amounts
4. **Consider audio quality**: Noisy audio needs different settings than studio recordings
5. **Test with representative samples**: Settings that work for one speaker may not work for another

## Visual Summary

```
RAW AUDIO:  [═══speech═══] [noise] [═══speech═══]
                  ↓            ↓           ↓
VAD DETECT: [███████████]  [█] [███████████]
                  ↓            ↓           ↓
GAP FILL:   (gaps too big, no filling happens)
                  ↓            ↓           ↓
MIN LENGTH: (all > 30ms, all kept)
                  ↓            ↓           ↓
UTTERANCES: [Utterance 1]  [U2] [Utterance 3]
                  ↓            ↓           ↓
RECOGNIZE:  "words found"  ""  "words found"
                            ↑
                    Problem: 2s wasted!
```