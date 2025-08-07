#!/usr/bin/env python3
"""
Convert rhubarb JSON output to the same format as visemes-base.json
"""

import json
import os
from datetime import datetime

def convert_rhubarb_to_base_format(rhubarb_file: str, output_file: str):
    """
    Convert rhubarb JSON format to base visemes format.
    
    Rhubarb format:
    {
        "metadata": {
            "soundFile": "...",
            "duration": 24.41
        },
        "mouthCues": [
            { "start": 0.00, "end": 0.08, "value": "X" },
            ...
        ]
    }
    
    Base format:
    {
        "metadata": {
            "audio_duration": 24.41,
            "visemes_count": 148,
            "exported_at": "2025-08-06T19:17:13.508Z"
        },
        "visemes": [
            { "start": 0, "end": 0.08, "value": "X" },
            ...
        ]
    }
    """
    
    # Load rhubarb output
    with open(rhubarb_file, 'r') as f:
        rhubarb_data = json.load(f)
    
    # Convert to base format
    converted = {
        "metadata": {
            "audio_duration": rhubarb_data["metadata"]["duration"],
            "visemes_count": len(rhubarb_data["mouthCues"]),
            "exported_at": datetime.now().isoformat() + "Z"
        },
        "visemes": []
    }
    
    # Convert mouthCues to visemes with consistent number formatting
    for cue in rhubarb_data["mouthCues"]:
        converted["visemes"].append({
            "start": round(cue["start"], 2),  # Round to 2 decimal places like base
            "end": round(cue["end"], 2),
            "value": cue["value"]
        })
    
    # Save converted file
    with open(output_file, 'w') as f:
        json.dump(converted, f, indent=2)
    
    print(f"Converted {rhubarb_file} to {output_file}")
    print(f"  - Duration: {converted['metadata']['audio_duration']}s")
    print(f"  - Visemes count: {converted['metadata']['visemes_count']}")
    
    return converted

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    
    # Input and output files
    rhubarb_file = os.path.join(script_dir, "visemes-new.json")
    output_file = os.path.join(script_dir, "visemes-new-converted.json")
    
    if not os.path.exists(rhubarb_file):
        print(f"Error: {rhubarb_file} not found.")
        print("Please run the test first to generate visemes-new.json")
        return 1
    
    try:
        converted_data = convert_rhubarb_to_base_format(rhubarb_file, output_file)
        
        print(f"\nSuccessfully converted to base format!")
        print(f"Output file: {output_file}")
        print("\nYou can now use this file with your visual comparison tool.")
        print("It has the same format as visemes-base.json")
        
    except Exception as e:
        print(f"Error during conversion: {e}")
        return 1
    
    return 0

if __name__ == "__main__":
    exit(main())