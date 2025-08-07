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

analyze_visemes('visemes-normal.json', '1. NORMAL MODE (optimization + tweening)')
analyze_visemes('visemes-notween.json', '2. NO TWEENING MODE (optimization only)')
analyze_visemes('visemes-raw.json', '3. RAW MODE (no optimization, with tweening)')
analyze_visemes('visemes-raw-notween.json', '4. RAW + NO TWEEN MODE (most direct)')

print("\n" + "=" * 60)
print("SUMMARY")
print("=" * 60)
print("- Normal mode: Smoothest animation with all optimizations")
print("- No tweening: Removes transition shapes but keeps timing optimization")
print("- Raw mode: Preserves original phoneme timing but adds tweens")
print("- Raw + No tween: Most direct phoneme-to-viseme mapping")
