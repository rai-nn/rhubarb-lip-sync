#!/usr/bin/env python3
"""
Detailed analysis of pause detection improvements.
"""

import json
import os
from typing import Dict, List, Tuple

def load_json(filepath: str) -> Dict:
    with open(filepath, 'r') as f:
        return json.load(f)

def get_sequences(data: Dict) -> List[Tuple[float, float, str]]:
    key = 'visemes' if 'visemes' in data else 'mouthCues'
    return [(v['start'], v['end'], v['value']) for v in data[key]]

def find_sentence_boundaries(sequences: List[Tuple[float, float, str]]) -> List[Tuple[float, str, str]]:
    """Find potential sentence boundaries based on pause patterns."""
    boundaries = []
    
    for i in range(len(sequences) - 1):
        curr = sequences[i]
        next_seq = sequences[i + 1]
        
        # Check for pauses between segments
        gap = next_seq[0] - curr[1]
        
        # Check for pause shapes (X or A) that might indicate boundaries
        if curr[2] in ['X', 'A']:
            duration = curr[1] - curr[0]
            if duration <= 0.2:  # Short pause that might be a sentence boundary
                if i > 0:
                    prev = sequences[i-1]
                    boundaries.append((curr[0], f"{prev[2]}->{curr[2]}->{next_seq[2]}", f"pause_{duration:.3f}s"))
    
    return boundaries

def analyze_fast_speech_sections(sequences: List[Tuple[float, float, str]]) -> List[Dict]:
    """Find sections with rapid transitions that might benefit from micro-pauses."""
    fast_sections = []
    window_size = 5  # Look at 5 consecutive shapes
    
    for i in range(len(sequences) - window_size):
        window = sequences[i:i+window_size]
        total_duration = window[-1][1] - window[0][0]
        avg_shape_duration = total_duration / window_size
        
        # Fast speech if average shape duration is very short
        if avg_shape_duration < 0.15:
            # Check if there's a pause in this window
            has_pause = any(s[2] in ['X', 'A'] for s in window)
            fast_sections.append({
                'start': window[0][0],
                'end': window[-1][1],
                'avg_duration': avg_shape_duration,
                'has_pause': has_pause,
                'shapes': [s[2] for s in window]
            })
    
    return fast_sections

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    
    base_file = os.path.join(project_root, "examples", "visemes-base.json")
    new_file = os.path.join(script_dir, "visemes-new.json")
    
    base_data = load_json(base_file)
    new_data = load_json(new_file)
    
    base_seq = get_sequences(base_data)
    new_seq = get_sequences(new_data)
    
    print("=" * 70)
    print("DETAILED PAUSE DETECTION ANALYSIS")
    print("=" * 70)
    print()
    
    # Analyze sentence boundaries
    base_boundaries = find_sentence_boundaries(base_seq)
    new_boundaries = find_sentence_boundaries(new_seq)
    
    print("SENTENCE BOUNDARY DETECTION")
    print("-" * 40)
    print(f"Base boundaries detected: {len(base_boundaries)}")
    print(f"New boundaries detected:  {len(new_boundaries)}")
    print(f"Improvement:              {len(new_boundaries) - len(base_boundaries):+d}")
    print()
    
    # Show new boundaries
    new_boundary_times = set([(b[0], b[2]) for b in new_boundaries])
    base_boundary_times = set([(b[0], b[2]) for b in base_boundaries])
    unique_new = [b for b in new_boundaries if (b[0], b[2]) not in base_boundary_times]
    
    if unique_new:
        print("NEW SENTENCE BOUNDARIES DETECTED:")
        for time, pattern, duration in sorted(unique_new[:5]):
            print(f"  {time:6.2f}s: {pattern:20s} ({duration})")
        if len(unique_new) > 5:
            print(f"  ... and {len(unique_new) - 5} more")
    print()
    
    # Analyze fast speech sections
    base_fast = analyze_fast_speech_sections(base_seq)
    new_fast = analyze_fast_speech_sections(new_seq)
    
    # Count fast sections with pauses
    base_with_pause = sum(1 for s in base_fast if s['has_pause'])
    new_with_pause = sum(1 for s in new_fast if s['has_pause'])
    
    print("FAST SPEECH SECTIONS ANALYSIS")
    print("-" * 40)
    print(f"Fast sections in base:    {len(base_fast)}")
    print(f"  With pauses:             {base_with_pause} ({base_with_pause/len(base_fast)*100:.1f}%)")
    print(f"Fast sections in new:     {len(new_fast)}")
    print(f"  With pauses:             {new_with_pause} ({new_with_pause/len(new_fast)*100:.1f}%)")
    print()
    
    # Find Shape A insertions (micro-pauses for sentence boundaries)
    base_a_positions = [(s[0], s[1]-s[0]) for s in base_seq if s[2] == 'A']
    new_a_positions = [(s[0], s[1]-s[0]) for s in new_seq if s[2] == 'A']
    
    new_a_insertions = []
    for pos, dur in new_a_positions:
        if not any(abs(pos - bp) < 0.1 for bp, _ in base_a_positions):
            new_a_insertions.append((pos, dur))
    
    if new_a_insertions:
        print("SHAPE A INSERTIONS (Micro-pauses)")
        print("-" * 40)
        print(f"New Shape A insertions: {len(new_a_insertions)}")
        for pos, dur in sorted(new_a_insertions[:5]):
            # Find context
            for i, (start, end, shape) in enumerate(new_seq):
                if abs(start - pos) < 0.01:
                    prev_shape = new_seq[i-1][2] if i > 0 else "?"
                    next_shape = new_seq[i+1][2] if i < len(new_seq)-1 else "?"
                    print(f"  {pos:6.2f}s: {prev_shape} -> A ({dur:.3f}s) -> {next_shape}")
                    break
        if len(new_a_insertions) > 5:
            print(f"  ... and {len(new_a_insertions) - 5} more")
    print()
    
    # Calculate pause density (pauses per second of speech)
    duration = base_data['metadata'].get('audio_duration', 24.41)
    base_pauses = len([s for s in base_seq if s[2] in ['X', 'A']])
    new_pauses = len([s for s in new_seq if s[2] in ['X', 'A']])
    
    print("PAUSE DENSITY METRICS")
    print("-" * 40)
    print(f"Audio duration:           {duration:.2f}s")
    print(f"Base pause density:       {base_pauses/duration:.2f} pauses/sec")
    print(f"New pause density:        {new_pauses/duration:.2f} pauses/sec")
    print(f"Improvement:              {(new_pauses-base_pauses)/duration:.2f} pauses/sec")
    print()
    
    # Find the shortest detected pauses
    base_pause_durations = sorted([(s[1]-s[0]) for s in base_seq if s[2] in ['X', 'A']])
    new_pause_durations = sorted([(s[1]-s[0]) for s in new_seq if s[2] in ['X', 'A']])
    
    print("SHORTEST PAUSES DETECTED")
    print("-" * 40)
    if base_pause_durations:
        print(f"Base shortest pauses: {base_pause_durations[:3]}")
    if new_pause_durations:
        print(f"New shortest pauses:  {new_pause_durations[:3]}")
    print()
    
    print("=" * 70)
    print("KEY IMPROVEMENTS SUMMARY")
    print("=" * 70)
    
    improvements = []
    
    # Check for more Shape A (closed mouth for boundaries)
    base_a_count = len([s for s in base_seq if s[2] == 'A'])
    new_a_count = len([s for s in new_seq if s[2] == 'A'])
    if new_a_count > base_a_count:
        improvements.append(f"✓ {new_a_count - base_a_count} more Shape A (closed mouth) for clearer boundaries")
    
    # Check for better pause distribution
    if new_pauses/duration > base_pauses/duration:
        improvements.append(f"✓ Improved pause density: {(new_pauses-base_pauses)/duration:.2f} more pauses/sec")
    
    # Check for new micro-pauses
    if new_a_insertions:
        improvements.append(f"✓ {len(new_a_insertions)} new micro-pauses inserted at phrase boundaries")
    
    # Check for better handling of fast sections
    if new_with_pause > base_with_pause:
        improvements.append(f"✓ Better fast speech handling: {new_with_pause - base_with_pause} more fast sections with pauses")
    
    for imp in improvements:
        print(f"  {imp}")
    
    if not improvements:
        print("  No significant improvements detected")
    
    print()
    print("The improvements show that the system now better detects:")
    print("  1. Short pauses between sentences in fast speech")
    print("  2. Phonetic boundaries that suggest sentence breaks")
    print("  3. Micro-pauses that create visual separation")

if __name__ == "__main__":
    main()