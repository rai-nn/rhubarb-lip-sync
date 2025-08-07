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
