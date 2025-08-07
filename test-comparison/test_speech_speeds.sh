#!/bin/bash

# Test script to compare different speech speed settings
set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
RHUBARB_BIN="$PROJECT_ROOT/build/rhubarb"
EXAMPLES_DIR="$PROJECT_ROOT/examples"

echo "=========================================="
echo "Testing Speech Speed Configuration"
echo "=========================================="
echo ""

# Test with slow speech setting
echo "1. Testing with --speechSpeed slow (conservative pause detection)..."
"$RHUBARB_BIN" \
    --speechSpeed slow \
    -f json \
    -d "$EXAMPLES_DIR/dialogue.txt" \
    -o "$SCRIPT_DIR/visemes-slow.json" \
    "$EXAMPLES_DIR/audio.wav" 2>/dev/null

echo "   Generated: visemes-slow.json"

# Test with normal speech setting (default)
echo "2. Testing with --speechSpeed normal (balanced pause detection)..."
"$RHUBARB_BIN" \
    --speechSpeed normal \
    -f json \
    -d "$EXAMPLES_DIR/dialogue.txt" \
    -o "$SCRIPT_DIR/visemes-normal.json" \
    "$EXAMPLES_DIR/audio.wav" 2>/dev/null

echo "   Generated: visemes-normal.json"

# Test with fast speech setting
echo "3. Testing with --speechSpeed fast (aggressive pause detection)..."
"$RHUBARB_BIN" \
    --speechSpeed fast \
    -f json \
    -d "$EXAMPLES_DIR/dialogue.txt" \
    -o "$SCRIPT_DIR/visemes-fast.json" \
    "$EXAMPLES_DIR/audio.wav" 2>/dev/null

echo "   Generated: visemes-fast.json"

# Test with custom VAD settings
echo "4. Testing with custom VAD settings (very aggressive)..."
"$RHUBARB_BIN" \
    --vadMaxGap 30 \
    --vadMinSegment 20 \
    --microPauseThreshold 40 \
    -f json \
    -d "$EXAMPLES_DIR/dialogue.txt" \
    -o "$SCRIPT_DIR/visemes-custom.json" \
    "$EXAMPLES_DIR/audio.wav" 2>/dev/null

echo "   Generated: visemes-custom.json"

echo ""
echo "=========================================="
echo "Comparing Pause Counts"
echo "=========================================="

# Count pauses in each output
count_pauses() {
    local file=$1
    local count=$(grep -o '"value": "[XA]"' "$file" | wc -l | tr -d ' ')
    echo "$count"
}

echo ""
echo "Pause counts (Shape X and A):"
echo "  Slow:    $(count_pauses "$SCRIPT_DIR/visemes-slow.json") pauses"
echo "  Normal:  $(count_pauses "$SCRIPT_DIR/visemes-normal.json") pauses"
echo "  Fast:    $(count_pauses "$SCRIPT_DIR/visemes-fast.json") pauses"
echo "  Custom:  $(count_pauses "$SCRIPT_DIR/visemes-custom.json") pauses"

echo ""
echo "Test complete! Files saved in: $SCRIPT_DIR"
echo ""
echo "You can run the detailed comparison with:"
echo "  python3 $SCRIPT_DIR/compare_visemes.py"