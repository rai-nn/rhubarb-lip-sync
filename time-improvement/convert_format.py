#!/usr/bin/env python3
import json
import sys

def convert_rhubarb_to_visemes_format(input_file, output_file, tool_name="rhubarb-character-timing"):
    """Convert Rhubarb output format to visemes.json format"""
    
    # Read input file
    with open(input_file, 'r') as f:
        rhubarb_data = json.load(f)
    
    # Extract visemes from mouthCues
    visemes = []
    for cue in rhubarb_data.get('mouthCues', []):
        visemes.append({
            "start": cue["start"],
            "end": cue["end"],
            "value": cue["value"]
        })
    
    # Create output structure
    output = {
        "metadata": {
            "audio_duration": rhubarb_data["metadata"]["duration"],
            "visemes_count": len(visemes),
            "tool": tool_name,
            "generator_version": "1.14.0-character-timing",
            "processing_options": {
                "extendedShapes": "",
                "noTweening": False,
                "language": "en",
                "recognizer": "character-timing",
                "skipTimingOptimization": False,
                "maxVisemesPerWord": 0,
                "dialogue": "Character timing from TTS",
                "skipPostProcessing": False
            }
        },
        "visemes": visemes
    }
    
    # Write output file
    with open(output_file, 'w') as f:
        json.dump(output, f, indent=2)
    
    print(f"Converted {input_file} -> {output_file}")
    print(f"  Duration: {output['metadata']['audio_duration']}s")
    print(f"  Visemes: {output['metadata']['visemes_count']}")

if __name__ == "__main__":
    import os
    import glob
    
    # Get script directory
    script_dir = os.path.dirname(os.path.abspath(__file__))
    outputs_dir = os.path.join(script_dir, "outputs")
    
    # Process all JSON files that don't end with _visemes.json
    json_files = glob.glob(os.path.join(outputs_dir, "*.json"))
    for input_file in json_files:
        if not input_file.endswith("_visemes.json"):
            base_name = os.path.basename(input_file).replace(".json", "")
            output_file = os.path.join(outputs_dir, f"{base_name}_visemes.json")
            convert_rhubarb_to_visemes_format(
                input_file,
                output_file,
                "rhubarb-character-timing"
            )