#!/bin/bash

# Minimal test script that only outputs two converted files

set -e  # Exit on error

# Paths
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
BUILD_DIR="${SCRIPT_DIR}/../build"
EXAMPLES_DIR="${SCRIPT_DIR}/examples"
OUTPUTS_DIR="${SCRIPT_DIR}/outputs"
RHUBARB="${BUILD_DIR}/rhubarb/rhubarb"
CONVERT_SCRIPT="${SCRIPT_DIR}/convert_format.py"

# Test files
AUDIO_FILE="${EXAMPLES_DIR}/audio.wav"
TRANSCRIPT_FILE="${EXAMPLES_DIR}/transcript.txt"
ALIGNMENT_FILE="${EXAMPLES_DIR}/alignment.json"

# Output files (only these two will remain)
TRADITIONAL_OUTPUT="${OUTPUTS_DIR}/traditional_output.json"
WORDTIMING_OUTPUT="${OUTPUTS_DIR}/fasttiming_output.json"

# Check if rhubarb executable exists
if [ ! -f "$RHUBARB" ]; then
    echo "Error: Rhubarb executable not found at $RHUBARB"
    echo "Please build the project first with: cd build && cmake --build ."
    exit 1
fi

# Create outputs directory if it doesn't exist
mkdir -p "$OUTPUTS_DIR"

# Clean outputs directory first
rm -f "${OUTPUTS_DIR}"/*.json

# Run traditional approach (save directly with final name)
"$RHUBARB" "$AUDIO_FILE" -d "$TRANSCRIPT_FILE" -f json > "$TRADITIONAL_OUTPUT" 2>/dev/null

# Run WordTiming approach (save directly with final name)
"$RHUBARB" --characterTiming "$ALIGNMENT_FILE" --text "$TRANSCRIPT_FILE" "$AUDIO_FILE" -f json > "$WORDTIMING_OUTPUT" 2>/dev/null

# Convert both files (script auto-converts and creates _visemes versions)
python3 "$CONVERT_SCRIPT" >/dev/null 2>&1

# Remove the raw Rhubarb format files, keep only converted versions
rm -f "$TRADITIONAL_OUTPUT"
rm -f "$WORDTIMING_OUTPUT"

# Rename converted files to the desired names
mv "${OUTPUTS_DIR}/traditional_output_visemes.json" "$TRADITIONAL_OUTPUT"
mv "${OUTPUTS_DIR}/wordtiming_output_visemes.json" "$WORDTIMING_OUTPUT"

echo "Test complete. Output files:"
echo "  - ${TRADITIONAL_OUTPUT}"
echo "  - ${WORDTIMING_OUTPUT}"