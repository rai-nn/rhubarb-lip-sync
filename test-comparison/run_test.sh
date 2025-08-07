#!/bin/bash

# Test script to compare improved rhubarb with base example
set -e

SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
RHUBARB_BIN="$PROJECT_ROOT/build/rhubarb"
EXAMPLES_DIR="$PROJECT_ROOT/examples"
OUTPUT_DIR="$SCRIPT_DIR"

echo "=========================================="
echo "Rhubarb Pause Detection Improvement Test"
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

echo "Running improved rhubarb on example audio..."
echo "Input: $EXAMPLES_DIR/audio.wav"
echo "Dialogue: $EXAMPLES_DIR/dialogue.txt"
echo ""

# Run rhubarb with the dialogue file
"$RHUBARB_BIN" \
    -f json \
    -d "$EXAMPLES_DIR/dialogue.txt" \
    -o "$OUTPUT_DIR/visemes-new.json" \
    "$EXAMPLES_DIR/audio.wav"

echo ""
echo "Rhubarb processing complete!"
echo "Output saved to: $OUTPUT_DIR/visemes-new.json"
echo ""

# Run Python comparison if script exists
if [ -f "$SCRIPT_DIR/compare_visemes.py" ]; then
    echo "Running comparison analysis..."
    python3 "$SCRIPT_DIR/compare_visemes.py"
else
    echo "Note: compare_visemes.py not found. Skipping comparison."
    echo "You can manually compare:"
    echo "  - Base: $EXAMPLES_DIR/visemes-base.json"
    echo "  - New:  $OUTPUT_DIR/visemes-new.json"
fi