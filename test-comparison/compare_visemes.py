#!/usr/bin/env python3
"""
Compare visemes output from improved rhubarb with base example.
Focuses on pause detection improvements.
"""

import json
import os
from typing import Dict, List, Tuple
from collections import Counter

def load_visemes(filepath: str) -> Dict:
    """Load visemes JSON file."""
    with open(filepath, 'r') as f:
        return json.load(f)

def extract_viseme_sequence(visemes_data: Dict) -> List[Tuple[float, float, str]]:
    """Extract viseme sequence as list of (start, end, value) tuples."""
    # Handle both formats: "visemes" (base) and "mouthCues" (rhubarb output)
    if 'visemes' in visemes_data:
        return [(v['start'], v['end'], v['value']) for v in visemes_data['visemes']]
    elif 'mouthCues' in visemes_data:
        return [(v['start'], v['end'], v['value']) for v in visemes_data['mouthCues']]
    else:
        raise KeyError("Neither 'visemes' nor 'mouthCues' found in data")

def find_pauses(sequence: List[Tuple[float, float, str]]) -> List[Tuple[float, float, str]]:
    """Find all pauses (Shape X or A) in the sequence."""
    pauses = []
    for start, end, shape in sequence:
        if shape in ['X', 'A']:
            duration = end - start
            pauses.append((start, duration, shape))
    return pauses

def find_micro_pauses(sequence: List[Tuple[float, float, str]], threshold: float = 0.1) -> List[Tuple[float, float, str]]:
    """Find micro-pauses (very short X or A shapes)."""
    micro_pauses = []
    for start, end, shape in sequence:
        duration = end - start
        if shape in ['X', 'A'] and duration <= threshold:
            micro_pauses.append((start, duration, shape))
    return micro_pauses

def analyze_transitions(sequence: List[Tuple[float, float, str]]) -> Dict:
    """Analyze transitions between shapes."""
    transitions = []
    for i in range(len(sequence) - 1):
        curr = sequence[i]
        next_shape = sequence[i + 1]
        transition = f"{curr[2]}->{next_shape[2]}"
        gap = next_shape[0] - curr[1]
        transitions.append((curr[1], transition, gap))
    
    # Count transition types
    transition_counts = Counter([t[1] for t in transitions])
    return {
        'transitions': transitions,
        'counts': dict(transition_counts)
    }

def compare_sequences(base_seq: List, new_seq: List) -> Dict:
    """Compare two viseme sequences."""
    base_shapes = Counter([s[2] for s in base_seq])
    new_shapes = Counter([s[2] for s in new_seq])
    
    comparison = {
        'base_count': len(base_seq),
        'new_count': len(new_seq),
        'shape_counts': {
            'base': dict(base_shapes),
            'new': dict(new_shapes),
            'differences': {}
        }
    }
    
    # Calculate differences
    all_shapes = set(base_shapes.keys()) | set(new_shapes.keys())
    for shape in all_shapes:
        base_count = base_shapes.get(shape, 0)
        new_count = new_shapes.get(shape, 0)
        diff = new_count - base_count
        if diff != 0:
            comparison['shape_counts']['differences'][shape] = {
                'change': diff,
                'percent': (diff / base_count * 100) if base_count > 0 else float('inf')
            }
    
    return comparison

def print_report(base_data: Dict, new_data: Dict):
    """Print comparison report."""
    print("=" * 60)
    print("VISEME COMPARISON REPORT")
    print("=" * 60)
    print()
    
    base_seq = extract_viseme_sequence(base_data)
    new_seq = extract_viseme_sequence(new_data)
    
    # Basic statistics
    print("BASIC STATISTICS")
    print("-" * 40)
    print(f"Base visemes count: {len(base_seq)}")
    print(f"New visemes count:  {len(new_seq)}")
    print(f"Difference:         {len(new_seq) - len(base_seq):+d}")
    print()
    
    # Pause analysis
    base_pauses = find_pauses(base_seq)
    new_pauses = find_pauses(new_seq)
    
    print("PAUSE DETECTION (Shape X and A)")
    print("-" * 40)
    print(f"Base pauses:     {len(base_pauses)}")
    print(f"New pauses:      {len(new_pauses)}")
    print(f"Difference:      {len(new_pauses) - len(base_pauses):+d}")
    print()
    
    # Micro-pause analysis
    base_micro = find_micro_pauses(base_seq, 0.1)
    new_micro = find_micro_pauses(new_seq, 0.1)
    
    print("MICRO-PAUSES (<0.1s)")
    print("-" * 40)
    print(f"Base micro-pauses: {len(base_micro)}")
    print(f"New micro-pauses:  {len(new_micro)}")
    print(f"Difference:        {len(new_micro) - len(base_micro):+d}")
    
    if new_micro:
        print("\nNew micro-pauses detected at:")
        for start, duration, shape in sorted(new_micro[:10]):  # Show first 10
            print(f"  - {start:.2f}s: Shape {shape} for {duration:.3f}s")
        if len(new_micro) > 10:
            print(f"  ... and {len(new_micro) - 10} more")
    print()
    
    # Shape distribution
    comparison = compare_sequences(base_seq, new_seq)
    
    print("SHAPE DISTRIBUTION CHANGES")
    print("-" * 40)
    if comparison['shape_counts']['differences']:
        for shape, diff in sorted(comparison['shape_counts']['differences'].items()):
            base_count = comparison['shape_counts']['base'].get(shape, 0)
            new_count = comparison['shape_counts']['new'].get(shape, 0)
            print(f"Shape {shape}: {base_count} -> {new_count} ({diff['change']:+d}, {diff['percent']:+.1f}%)")
    else:
        print("No changes in shape distribution")
    print()
    
    # Pause duration analysis
    if new_pauses:
        base_durations = [d for _, d, _ in base_pauses]
        new_durations = [d for _, d, _ in new_pauses]
        
        print("PAUSE DURATION STATISTICS")
        print("-" * 40)
        if base_durations:
            print(f"Base avg pause:  {sum(base_durations)/len(base_durations):.3f}s")
            print(f"Base min pause:  {min(base_durations):.3f}s")
            print(f"Base max pause:  {max(base_durations):.3f}s")
        
        if new_durations:
            print(f"New avg pause:   {sum(new_durations)/len(new_durations):.3f}s")
            print(f"New min pause:   {min(new_durations):.3f}s")
            print(f"New max pause:   {max(new_durations):.3f}s")
        print()
    
    # Find newly added pauses
    base_pause_times = set([(round(s, 2), round(d, 2)) for s, d, _ in base_pauses])
    new_pause_times = [(s, d, shape) for s, d, shape in new_pauses 
                       if (round(s, 2), round(d, 2)) not in base_pause_times]
    
    if new_pause_times:
        print("NEWLY DETECTED PAUSES")
        print("-" * 40)
        print(f"Found {len(new_pause_times)} new pauses:")
        for start, duration, shape in sorted(new_pause_times[:10]):
            print(f"  - {start:.2f}s: Shape {shape} for {duration:.3f}s")
        if len(new_pause_times) > 10:
            print(f"  ... and {len(new_pause_times) - 10} more")
    
    print()
    print("=" * 60)
    print("SUMMARY")
    print("=" * 60)
    
    improvements = []
    if len(new_pauses) > len(base_pauses):
        improvements.append(f"✓ Detected {len(new_pauses) - len(base_pauses)} additional pauses")
    if len(new_micro) > len(base_micro):
        improvements.append(f"✓ Detected {len(new_micro) - len(base_micro)} additional micro-pauses")
    if new_durations and base_durations:
        if min(new_durations) < min(base_durations):
            improvements.append(f"✓ Can now detect pauses as short as {min(new_durations):.3f}s")
    
    if improvements:
        print("Improvements detected:")
        for imp in improvements:
            print(f"  {imp}")
    else:
        print("No significant improvements detected in pause detection.")
    
    print()
    print("Analysis complete!")

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    
    base_file = os.path.join(project_root, "examples", "visemes-base.json")
    new_file = os.path.join(script_dir, "visemes-new.json")
    
    # Check if files exist
    if not os.path.exists(base_file):
        print(f"Error: Base file not found: {base_file}")
        return 1
    
    if not os.path.exists(new_file):
        print(f"Error: New file not found: {new_file}")
        print("Please run the test first to generate visemes-new.json")
        return 1
    
    # Load and compare
    try:
        base_data = load_visemes(base_file)
        new_data = load_visemes(new_file)
        
        print_report(base_data, new_data)
        
    except Exception as e:
        print(f"Error during comparison: {e}")
        return 1
    
    return 0

if __name__ == "__main__":
    exit(main())