#!/usr/bin/env python3
"""
Real-time streaming test for rhubarb-stream with rhubarb-lite benchmark.

Simulates realistic streaming: sends audio in random 2-4 second chunks
and shows responses as they arrive in real-time. Then runs rhubarb-lite
on the same audio for comparison.

Usage:
  ./test-realtime.py /path/to/audio.wav
  ./test-realtime.py /path/to/audio.wav "The sentence text"
"""

import subprocess
import struct
import wave
import sys
import json
import os
import array
import random
import time
import threading
import tempfile
from datetime import datetime

# ANSI colors
BLUE = '\033[94m'
GREEN = '\033[92m'
YELLOW = '\033[93m'
RED = '\033[91m'
CYAN = '\033[96m'
MAGENTA = '\033[95m'
RESET = '\033[0m'
BOLD = '\033[1m'
DIM = '\033[2m'

# Frame types
FRAME_AUDIO = 0x01
FRAME_SENTENCE = 0x02
FRAME_CONFIG = 0x03
FRAME_RESET = 0x04
FRAME_END = 0xFF

def encode_frame(frame_type: int, payload: bytes) -> bytes:
    """Encode a binary frame: [type:1][length:4 LE][payload:N]"""
    return struct.pack('<BI', frame_type, len(payload)) + payload

def load_wav(wav_path: str) -> tuple[bytes, int, float]:
    """Load WAV file and return PCM data, sample rate, duration."""
    with wave.open(wav_path, 'rb') as wav:
        sample_rate = wav.getframerate()
        n_channels = wav.getnchannels()
        n_frames = wav.getnframes()
        duration = n_frames / sample_rate

        pcm_data = wav.readframes(n_frames)

        # Convert stereo to mono
        if n_channels == 2:
            samples = array.array('h')
            samples.frombytes(pcm_data)
            mono_samples = samples[::2]
            pcm_data = mono_samples.tobytes()

        return pcm_data, sample_rate, duration

def resample(pcm_data: bytes, src_rate: int, target_rate: int = 16000) -> bytes:
    """Resample PCM data to target rate."""
    if src_rate == target_rate:
        return pcm_data

    samples = array.array('h')
    samples.frombytes(pcm_data)

    ratio = src_rate / target_rate
    new_length = int(len(samples) / ratio)
    new_samples = array.array('h')

    for i in range(new_length):
        idx = int(i * ratio)
        new_samples.append(samples[min(idx, len(samples) - 1)])

    return new_samples.tobytes()

def timestamp():
    """Get current timestamp string."""
    return datetime.now().strftime('%H:%M:%S.%f')[:-3]

def run_rhubarb_lite(wav_path: str, dialogue: str) -> tuple[list, float]:
    """Run rhubarb-lite benchmark and return visemes + processing time."""
    script_dir = os.path.dirname(os.path.abspath(__file__))
    rhubarb_lite_path = os.path.join(
        script_dir, "..", "..", "lib", "generators", "implementations",
        "rhubarb-lite", "binary", "rhubarb"
    )

    if not os.path.exists(rhubarb_lite_path):
        print(f"{RED}rhubarb-lite not found at {rhubarb_lite_path}{RESET}")
        return [], 0.0

    # Create temp dialogue file
    with tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False) as f:
        f.write(dialogue)
        dialogue_path = f.name

    try:
        start_time = time.time()

        result = subprocess.run(
            [rhubarb_lite_path, "-f", "json", "-d", dialogue_path, wav_path],
            capture_output=True,
            text=True,
            timeout=120
        )

        elapsed = time.time() - start_time

        if result.returncode != 0:
            print(f"{RED}rhubarb-lite failed: {result.stderr}{RESET}")
            return [], elapsed

        # Parse JSON output
        output = json.loads(result.stdout)
        visemes = []
        for mouth_cue in output.get("mouthCues", []):
            visemes.append({
                "start": mouth_cue["start"],
                "end": mouth_cue["end"],
                "value": mouth_cue["value"]
            })

        return visemes, elapsed

    finally:
        os.unlink(dialogue_path)

def output_reader(proc, visemes_list, stop_event):
    """Background thread to read stdout in real-time."""
    while not stop_event.is_set():
        line = proc.stdout.readline()
        if not line:
            break

        line = line.decode('utf-8').strip()
        if not line:
            continue

        try:
            msg = json.loads(line)
            msg_type = msg.get('type', 'unknown')

            if msg_type == 'ready':
                print(f"{DIM}[{timestamp()}]{RESET} {CYAN}● READY{RESET}")
            elif msg_type == 'debug':
                debug_msg = msg.get('message', '')
                # Only show important debug messages
                if 'Recognized' in debug_msg or 'Generated' in debug_msg or 'Processing sentence' in debug_msg:
                    print(f"{DIM}[{timestamp()}]{RESET} {YELLOW}⚙ {debug_msg}{RESET}")
            elif msg_type == 'error':
                print(f"{DIM}[{timestamp()}]{RESET} {RED}✗ ERROR: {msg.get('message', '')}{RESET}")
            elif msg_type == 'viseme':
                visemes_list.append(msg)
                print(f"{DIM}[{timestamp()}]{RESET} {GREEN}← VISEME{RESET} {MAGENTA}{msg['value']}{RESET} ({msg['start']:.2f}s - {msg['end']:.2f}s)")
            elif msg_type == 'end':
                print(f"{DIM}[{timestamp()}]{RESET} {CYAN}● END{RESET} duration={msg.get('duration', 0):.2f}s")

        except json.JSONDecodeError:
            print(f"{DIM}[{timestamp()}]{RESET} {DIM}(raw) {line}{RESET}")

def main():
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <audio.wav> [sentence_text]")
        print(f"\nExample:")
        print(f"  {sys.argv[0]} ~/audio.wav")
        print(f"  {sys.argv[0]} ~/audio.wav \"Hello world this is a test\"")
        return 1

    wav_path = sys.argv[1]
    sentence_text = sys.argv[2] if len(sys.argv) > 2 else "Streaming audio test."

    script_dir = os.path.dirname(os.path.abspath(__file__))
    binary_path = os.path.join(script_dir, "build", "rhubarb-stream")

    if not os.path.exists(binary_path):
        print(f"{RED}Error: Binary not found: {binary_path}{RESET}")
        return 1

    if not os.path.exists(wav_path):
        print(f"{RED}Error: Audio not found: {wav_path}{RESET}")
        return 1

    # Header
    print(f"\n{BOLD}═══════════════════════════════════════════════════════════{RESET}")
    print(f"{BOLD}  rhubarb-stream Real-Time Streaming Test{RESET}")
    print(f"{BOLD}═══════════════════════════════════════════════════════════{RESET}")
    print(f"{DIM}Audio: {wav_path}{RESET}")
    print(f"{DIM}Sentence: {sentence_text}{RESET}")
    print()

    # Load audio
    print(f"{BOLD}Loading audio...{RESET}")
    pcm_data, sample_rate, orig_duration = load_wav(wav_path)
    print(f"  Source: {sample_rate}Hz, {orig_duration:.2f}s")

    if sample_rate != 16000:
        pcm_data = resample(pcm_data, sample_rate, 16000)
        print(f"  Resampled to 16000Hz")

    duration = len(pcm_data) // 2 / 16000
    total_samples = len(pcm_data) // 2
    print(f"  Final: {total_samples:,} samples ({duration:.2f}s)")

    # Generate random chunk sizes (2-4 seconds each)
    chunk_boundaries = [0]
    current_pos = 0
    while current_pos < total_samples:
        chunk_seconds = random.uniform(2.0, 4.0)
        chunk_samples = int(chunk_seconds * 16000)
        current_pos += chunk_samples
        if current_pos >= total_samples:
            chunk_boundaries.append(total_samples)
        else:
            chunk_boundaries.append(current_pos)

    num_chunks = len(chunk_boundaries) - 1
    print(f"  Chunks: {num_chunks} (random 2-4s each)")
    print()

    # Start the process
    print(f"{BOLD}Starting rhubarb-stream...{RESET}")
    proc = subprocess.Popen(
        [binary_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        bufsize=0
    )

    # Start output reader thread
    visemes = []
    stop_event = threading.Event()
    reader_thread = threading.Thread(target=output_reader, args=(proc, visemes, stop_event))
    reader_thread.daemon = True
    reader_thread.start()

    # Give process time to start
    time.sleep(0.1)

    print()
    print(f"{BOLD}Streaming audio chunks...{RESET}")
    print(f"{DIM}{'─' * 60}{RESET}")

    # Stream chunks
    start_time = time.time()
    total_sent = 0

    for i in range(num_chunks):
        start_sample = chunk_boundaries[i]
        end_sample = chunk_boundaries[i + 1]

        start_byte = start_sample * 2
        end_byte = end_sample * 2
        chunk_data = pcm_data[start_byte:end_byte]

        chunk_samples = end_sample - start_sample
        chunk_duration = chunk_samples / 16000
        chunk_start_time = start_sample / 16000
        chunk_end_time = end_sample / 16000

        # Send audio frame
        frame = encode_frame(FRAME_AUDIO, chunk_data)
        proc.stdin.write(frame)
        proc.stdin.flush()

        total_sent += len(chunk_data)

        print(f"{DIM}[{timestamp()}]{RESET} {BLUE}→ AUDIO{RESET} chunk {i+1}/{num_chunks}: "
              f"{chunk_duration:.2f}s ({chunk_start_time:.2f}s - {chunk_end_time:.2f}s) "
              f"[{len(chunk_data):,} bytes]")

        # Small delay to simulate streaming (not real-time, just enough to see the flow)
        time.sleep(0.05)

    print(f"{DIM}{'─' * 60}{RESET}")
    print()

    # Send sentence
    print(f"{BOLD}Sending sentence...{RESET}")
    sentence_json = json.dumps({
        "text": sentence_text,
        "start": 0.0,
        "end": duration,
        "words": []
    })
    frame = encode_frame(FRAME_SENTENCE, sentence_json.encode('utf-8'))
    proc.stdin.write(frame)
    proc.stdin.flush()
    print(f"{DIM}[{timestamp()}]{RESET} {BLUE}→ SENTENCE{RESET} \"{sentence_text[:50]}...\"")

    # Small delay
    time.sleep(0.1)

    # Send END
    print()
    print(f"{BOLD}Sending END signal...{RESET}")
    frame = encode_frame(FRAME_END, b'')
    proc.stdin.write(frame)
    proc.stdin.flush()
    print(f"{DIM}[{timestamp()}]{RESET} {BLUE}→ END{RESET}")
    print()

    print(f"{BOLD}Waiting for processing...{RESET}")
    print(f"{DIM}{'─' * 60}{RESET}")

    # Wait for process to complete
    proc.stdin.close()
    proc.wait()

    # Stop reader thread
    stop_event.set()
    reader_thread.join(timeout=1.0)

    elapsed = time.time() - start_time

    print(f"{DIM}{'─' * 60}{RESET}")
    print()

    # Run rhubarb-lite benchmark
    print(f"{BOLD}═══════════════════════════════════════════════════════════{RESET}")
    print(f"{BOLD}  BENCHMARK: Running rhubarb-lite...{RESET}")
    print(f"{BOLD}═══════════════════════════════════════════════════════════{RESET}")

    lite_visemes, lite_elapsed = run_rhubarb_lite(wav_path, sentence_text)

    if lite_visemes:
        print(f"  {GREEN}✓ rhubarb-lite completed: {len(lite_visemes)} visemes in {lite_elapsed:.2f}s{RESET}")
    else:
        print(f"  {RED}✗ rhubarb-lite failed or not found{RESET}")

    # Summary comparison
    print()
    print(f"{BOLD}═══════════════════════════════════════════════════════════{RESET}")
    print(f"{BOLD}  COMPARISON{RESET}")
    print(f"{BOLD}═══════════════════════════════════════════════════════════{RESET}")

    print(f"\n  {BOLD}{'Metric':<25} {'rhubarb-stream':>15} {'rhubarb-lite':>15}{RESET}")
    print(f"  {'─' * 55}")
    print(f"  {'Processing time':<25} {elapsed:>14.2f}s {lite_elapsed:>14.2f}s")
    print(f"  {'Speed (x realtime)':<25} {duration/elapsed:>15.1f} {duration/lite_elapsed if lite_elapsed > 0 else 0:>15.1f}")
    print(f"  {'Visemes count':<25} {len(visemes):>15} {len(lite_visemes):>15}")

    # Shape distribution comparison
    stream_shapes = {}
    for v in visemes:
        stream_shapes[v['value']] = stream_shapes.get(v['value'], 0) + 1

    lite_shapes = {}
    for v in lite_visemes:
        lite_shapes[v['value']] = lite_shapes.get(v['value'], 0) + 1

    all_shapes = sorted(set(list(stream_shapes.keys()) + list(lite_shapes.keys())))

    print(f"\n  {BOLD}Shape Distribution:{RESET}")
    print(f"  {'Shape':<10} {'rhubarb-stream':>15} {'rhubarb-lite':>15}")
    print(f"  {'─' * 40}")
    for shape in all_shapes:
        stream_count = stream_shapes.get(shape, 0)
        lite_count = lite_shapes.get(shape, 0)
        diff = stream_count - lite_count
        diff_str = f"({diff:+d})" if diff != 0 else ""
        print(f"  {shape:<10} {stream_count:>15} {lite_count:>15}  {diff_str}")

    # Timing comparison
    if visemes and lite_visemes:
        print(f"\n  {BOLD}Timing Analysis (first 10 visemes):{RESET}")
        print(f"  {'#':<4} {'Stream':^20} {'Lite':^20} {'Δ Start':>10}")
        print(f"  {'─' * 58}")
        for i in range(min(10, len(visemes), len(lite_visemes))):
            sv = visemes[i]
            lv = lite_visemes[i]
            delta = sv['start'] - lv['start']
            print(f"  {i+1:<4} {sv['value']} {sv['start']:>6.2f}-{sv['end']:<6.2f}s    "
                  f"{lv['value']} {lv['start']:>6.2f}-{lv['end']:<6.2f}s  {delta:>+8.2f}s")

    # Save both to separate JSON files with same format
    stream_output = {
        "duration": duration,
        "visemes": [{"start": v["start"], "end": v["end"], "value": v["value"]} for v in visemes]
    }
    with open("rhubarb-stream-visemes.json", 'w') as f:
        json.dump(stream_output, f, indent=2)

    lite_output = {
        "duration": duration,
        "visemes": [{"start": v["start"], "end": v["end"], "value": v["value"]} for v in lite_visemes]
    }
    with open("rhubarb-lite-visemes.json", 'w') as f:
        json.dump(lite_output, f, indent=2)

    print(f"\n  {GREEN}Saved: rhubarb-stream-visemes.json ({len(visemes)} visemes){RESET}")
    print(f"  {GREEN}Saved: rhubarb-lite-visemes.json ({len(lite_visemes)} visemes){RESET}")

    print()
    return 0

if __name__ == "__main__":
    sys.exit(main())
