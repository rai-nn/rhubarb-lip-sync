#!/bin/bash

# Test script for comparing different animation modes in Rhubarb
# This tests the --rawAnimation and --noTweening flags

set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
RHUBARB_BIN="$PROJECT_ROOT/build/rhubarb"
EXAMPLES_DIR="$PROJECT_ROOT/examples"
OUTPUT_DIR="$SCRIPT_DIR"

echo "=========================================="
echo "Rhubarb Animation Modes Test"
echo "=========================================="
echo ""

# Check if rhubarb binary exists
if [ ! -f "$RHUBARB_BIN" ]; then
    echo "Error: Rhubarb binary not found at $RHUBARB_BIN"
    echo "Please build the project first."
    exit 1
fi

# Check if example files exist
if [ ! -f "$EXAMPLES_DIR/audio.wav" ]; then
    echo "Error: audio.wav not found in examples directory"
    exit 1
fi

if [ ! -f "$EXAMPLES_DIR/dialogue.txt" ]; then
    echo "Error: dialogue.txt not found in examples directory"
    exit 1
fi

echo "Testing different animation modes..."
echo "Input: $EXAMPLES_DIR/audio.wav"
echo "Dialogue: $EXAMPLES_DIR/dialogue.txt"
echo ""

# Test 1: Normal mode (with optimization and tweening)
echo "1. Running NORMAL mode (default settings)..."
"$RHUBARB_BIN" \
    -f json \
    -d "$EXAMPLES_DIR/dialogue.txt" \
    -o "$OUTPUT_DIR/temp-normal.json" \
    "$EXAMPLES_DIR/audio.wav" 2>&1 | grep -E "Progress:|Done." || true

# Test 2: No tweening only
echo "2. Running with --noTweening only..."
"$RHUBARB_BIN" \
    -f json \
    --noTweening \
    -d "$EXAMPLES_DIR/dialogue.txt" \
    -o "$OUTPUT_DIR/temp-notween.json" \
    "$EXAMPLES_DIR/audio.wav" 2>&1 | grep -E "Progress:|Done." || true

# Test 3: Skip timing optimization only
echo "3. Running with --skipTimingOptimization only..."
"$RHUBARB_BIN" \
    -f json \
    --skipTimingOptimization \
    -d "$EXAMPLES_DIR/dialogue.txt" \
    -o "$OUTPUT_DIR/temp-raw.json" \
    "$EXAMPLES_DIR/audio.wav" 2>&1 | grep -E "Progress:|Done." || true

# Test 4: Both flags (skip timing optimization + no tweening)
echo "4. Running with --skipTimingOptimization --noTweening..."
"$RHUBARB_BIN" \
    -f json \
    --skipTimingOptimization \
    --noTweening \
    -d "$EXAMPLES_DIR/dialogue.txt" \
    -o "$OUTPUT_DIR/temp-raw-notween.json" \
    "$EXAMPLES_DIR/audio.wav" 2>&1 | grep -E "Progress:|Done." || true

# Test 5: Max visemes per word (2 visemes per word)
echo "5. Running with --maxVisemesPerWord 2..."
"$RHUBARB_BIN" \
    -f json \
    --maxVisemesPerWord 2 \
    -d "$EXAMPLES_DIR/dialogue.txt" \
    -o "$OUTPUT_DIR/temp-max2.json" \
    "$EXAMPLES_DIR/audio.wav" 2>&1 | grep -E "Progress:|Done." || true

# Test 6: Max visemes per word with no tweening
echo "6. Running with --maxVisemesPerWord 2 --noTweening..."
"$RHUBARB_BIN" \
    -f json \
    --maxVisemesPerWord 2 \
    --noTweening \
    -d "$EXAMPLES_DIR/dialogue.txt" \
    -o "$OUTPUT_DIR/temp-max2-notween.json" \
    "$EXAMPLES_DIR/audio.wav" 2>&1 | grep -E "Progress:|Done." || true

echo ""
echo "Converting to visual tool format..."
echo ""

# Create conversion script that can handle any input file
cat << 'EOF' > "$OUTPUT_DIR/convert_file.py"
#!/usr/bin/env python3
import json
import sys
from datetime import datetime

def convert_rhubarb_to_base_format(rhubarb_file, output_file):
    with open(rhubarb_file, 'r') as f:
        rhubarb_data = json.load(f)
    
    converted = {
        "metadata": {
            "audio_duration": rhubarb_data["metadata"]["duration"],
            "visemes_count": len(rhubarb_data["mouthCues"]),
            "exported_at": datetime.now().isoformat() + "Z"
        },
        "visemes": []
    }
    
    for cue in rhubarb_data["mouthCues"]:
        converted["visemes"].append({
            "start": round(cue["start"], 2),
            "end": round(cue["end"], 2),
            "value": cue["value"]
        })
    
    with open(output_file, 'w') as f:
        json.dump(converted, f, indent=2)
    
    print(f"  ✓ Converted to {output_file} ({converted['metadata']['visemes_count']} visemes)")

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python convert_file.py <input> <output>")
        sys.exit(1)
    convert_rhubarb_to_base_format(sys.argv[1], sys.argv[2])
EOF

# Convert each output file to final format
python3 "$OUTPUT_DIR/convert_file.py" "$OUTPUT_DIR/temp-normal.json" "$OUTPUT_DIR/visemes-normal.json"
python3 "$OUTPUT_DIR/convert_file.py" "$OUTPUT_DIR/temp-notween.json" "$OUTPUT_DIR/visemes-notween.json"
python3 "$OUTPUT_DIR/convert_file.py" "$OUTPUT_DIR/temp-raw.json" "$OUTPUT_DIR/visemes-raw.json"
python3 "$OUTPUT_DIR/convert_file.py" "$OUTPUT_DIR/temp-raw-notween.json" "$OUTPUT_DIR/visemes-raw-notween.json"
python3 "$OUTPUT_DIR/convert_file.py" "$OUTPUT_DIR/temp-max2.json" "$OUTPUT_DIR/visemes-max2.json"
python3 "$OUTPUT_DIR/convert_file.py" "$OUTPUT_DIR/temp-max2-notween.json" "$OUTPUT_DIR/visemes-max2-notween.json"

# Clean up temporary files
rm -f "$OUTPUT_DIR/temp-*.json"

echo ""
echo "All tests complete! Visual tool format files:"
echo "  - $OUTPUT_DIR/visemes-normal.json (default mode)"
echo "  - $OUTPUT_DIR/visemes-notween.json (no tweening)"
echo "  - $OUTPUT_DIR/visemes-raw.json (skip timing optimization)"
echo "  - $OUTPUT_DIR/visemes-raw-notween.json (skip timing + no tweening)"
echo "  - $OUTPUT_DIR/visemes-max2.json (max 2 visemes per word)"
echo "  - $OUTPUT_DIR/visemes-max2-notween.json (max 2 visemes per word, no tweening)"
echo ""

# Create Python analysis script if it doesn't exist
cat << 'EOF' > "$OUTPUT_DIR/analyze_modes.py"
import json
import sys

def analyze_visemes(filename, label):
    try:
        with open(filename, 'r') as f:
            data = json.load(f)
    except FileNotFoundError:
        print(f"Error: {filename} not found")
        return
    
    # Handle both formats: 'mouthCues' (original) and 'visemes' (converted)
    cues = data.get('visemes', data.get('mouthCues', []))
    print(f"\n{label}")
    print("-" * 60)
    print(f"Total visemes: {len(cues)}")
    
    # Count shape frequencies
    shapes = {}
    for cue in cues:
        shape = cue['value']
        shapes[shape] = shapes.get(shape, 0) + 1
    
    print("Shape distribution:")
    for shape in sorted(shapes.keys()):
        print(f"  {shape}: {shapes[shape]:3d}")
    
    # Check for rapid transitions (< 0.07s duration)
    rapid = 0
    for cue in cues:
        duration = cue['end'] - cue['start']
        if duration < 0.07:
            rapid += 1
    
    print(f"Rapid transitions (<70ms): {rapid}")
    
    # Calculate average duration
    total_duration = 0
    for cue in cues:
        total_duration += cue['end'] - cue['start']
    avg_duration = total_duration / len(cues) if cues else 0
    print(f"Average viseme duration: {avg_duration:.3f}s")
    
    # Sample first 10 visemes
    print("\nFirst 10 visemes:")
    for i, cue in enumerate(cues[:10]):
        duration = cue['end'] - cue['start']
        print(f"  {cue['start']:5.2f}-{cue['end']:5.2f}s: {cue['value']} ({duration:.3f}s)")

# Analyze all modes
print("=" * 60)
print("ANIMATION MODES COMPARISON")
print("=" * 60)

analyze_visemes('visemes-normal.json', '1. NORMAL MODE (with timing optimization + tweening)')
analyze_visemes('visemes-notween.json', '2. NO TWEENING MODE (with timing optimization, no tweening)')
analyze_visemes('visemes-raw.json', '3. SKIP TIMING OPT MODE (no timing optimization, with tweening)')
analyze_visemes('visemes-raw-notween.json', '4. SKIP TIMING + NO TWEEN (most direct phoneme mapping)')
analyze_visemes('visemes-max2.json', '5. MAX 2 VISEMES PER WORD (simplified for phrasing)')
analyze_visemes('visemes-max2-notween.json', '6. MAX 2 + NO TWEEN (simplified phrasing, no tweening)')

print("\n" + "=" * 60)
print("SUMMARY")
print("=" * 60)
print("- Normal mode: Smoothest animation with all optimizations")
print("- No tweening: Removes transition shapes but keeps timing optimization")
print("- Raw mode: Preserves original phoneme timing but adds tweens")
print("- Raw + No tween: Most direct phoneme-to-viseme mapping")
EOF

# Run the analysis
echo "Running analysis..."
cd "$OUTPUT_DIR"
python3 analyze_modes.py