#!/usr/bin/env python3
"""
Verbose test script for rhubarb-stream binary.

Shows the complete data flow:
  INPUT  → what we send to stdin (binary frames)
  PROCESS → debug messages from rhubarb-stream
  OUTPUT → visemes coming out of stdout

Usage:
  ./test-verbose.py                          # Use default test audio
  ./test-verbose.py /path/to/audio.wav       # Use custom audio
  ./test-verbose.py audio.wav "Hello world"  # Custom audio + sentence text
"""

import subprocess
import struct
import wave
import sys
import json
import os
import array
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

FRAME_NAMES = {
    FRAME_AUDIO: 'AUDIO',
    FRAME_SENTENCE: 'SENTENCE',
    FRAME_CONFIG: 'CONFIG',
    FRAME_RESET: 'RESET',
    FRAME_END: 'END'
}

def log_input(msg):
    print(f"{BLUE}→ INPUT{RESET}   {msg}")

def log_process(msg):
    print(f"{YELLOW}⚙ PROCESS{RESET} {msg}")

def log_output(msg):
    print(f"{GREEN}← OUTPUT{RESET}  {msg}")

def log_error(msg):
    print(f"{RED}✗ ERROR{RESET}   {msg}")

def log_section(title):
    print(f"\n{BOLD}{CYAN}{'═' * 60}{RESET}")
    print(f"{BOLD}{CYAN}  {title}{RESET}")
    print(f"{BOLD}{CYAN}{'═' * 60}{RESET}\n")

def encode_frame(frame_type: int, payload: bytes) -> bytes:
    """Encode a binary frame: [type:1][length:4 LE][payload:N]"""
    return struct.pack('<BI', frame_type, len(payload)) + payload

def load_wav(wav_path: str) -> tuple[bytes, int, dict]:
    """Load WAV file and return PCM data, sample rate, and info dict."""
    with wave.open(wav_path, 'rb') as wav:
        sample_rate = wav.getframerate()
        n_channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        n_frames = wav.getnframes()

        info = {
            'sample_rate': sample_rate,
            'channels': n_channels,
            'bit_depth': sample_width * 8,
            'frames': n_frames,
            'duration': n_frames / sample_rate
        }

        pcm_data = wav.readframes(n_frames)

        # Convert stereo to mono if needed
        if n_channels == 2:
            samples = array.array('h')
            samples.frombytes(pcm_data)
            mono_samples = samples[::2]
            pcm_data = mono_samples.tobytes()
            info['converted'] = 'stereo→mono'

        return pcm_data, sample_rate, info

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

def main():
    # Parse arguments
    script_dir = os.path.dirname(os.path.abspath(__file__))
    binary_path = os.path.join(script_dir, "build", "rhubarb-stream")

    wav_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(script_dir, "..", "resources", "audio.wav")
    sentence_text = sys.argv[2] if len(sys.argv) > 2 else "Automatic speech recognition test."

    # Header
    print(f"\n{BOLD}rhubarb-stream Verbose Test{RESET}")
    print(f"{DIM}Binary: {binary_path}{RESET}")
    print(f"{DIM}Audio:  {wav_path}{RESET}")
    print(f"{DIM}Time:   {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}{RESET}")

    # Verify binary exists
    if not os.path.exists(binary_path):
        log_error(f"Binary not found: {binary_path}")
        print("Run: mkdir build && cd build && cmake .. && make")
        return 1

    # Verify audio exists
    if not os.path.exists(wav_path):
        log_error(f"Audio not found: {wav_path}")
        return 1

    # ═══════════════════════════════════════════════════════════
    # LOAD AUDIO
    # ═══════════════════════════════════════════════════════════
    log_section("LOADING AUDIO")

    pcm_data, sample_rate, info = load_wav(wav_path)
    print(f"  Source:      {info['sample_rate']}Hz, {info['channels']}ch, {info['bit_depth']}bit")
    print(f"  Frames:      {info['frames']:,}")
    print(f"  Duration:    {info['duration']:.2f}s")
    if 'converted' in info:
        print(f"  Converted:   {info['converted']}")

    # Resample if needed
    if sample_rate != 16000:
        pcm_data = resample(pcm_data, sample_rate, 16000)
        print(f"  Resampled:   {sample_rate}Hz → 16000Hz")

    duration = len(pcm_data) // 2 / 16000
    print(f"  Final:       {len(pcm_data) // 2:,} samples ({duration:.2f}s)")

    # ═══════════════════════════════════════════════════════════
    # BUILD FRAMES
    # ═══════════════════════════════════════════════════════════
    log_section("BUILDING INPUT FRAMES")

    frames = bytearray()
    frame_log = []

    # Audio frames (1 second chunks)
    chunk_samples = 16000
    chunk_bytes = chunk_samples * 2
    chunk_count = 0

    for i in range(0, len(pcm_data), chunk_bytes):
        chunk = pcm_data[i:i + chunk_bytes]
        frame = encode_frame(FRAME_AUDIO, chunk)
        frames.extend(frame)
        chunk_count += 1
        chunk_duration = len(chunk) // 2 / 16000
        frame_log.append(f"AUDIO    {len(chunk):6} bytes  ({chunk_duration:.2f}s)")

    log_input(f"Created {chunk_count} AUDIO frames ({len(pcm_data):,} bytes total)")

    # Sentence frame
    sentence_json = json.dumps({
        "text": sentence_text,
        "start": 0.0,
        "end": duration,
        "words": []
    })
    frame = encode_frame(FRAME_SENTENCE, sentence_json.encode('utf-8'))
    frames.extend(frame)
    frame_log.append(f"SENTENCE {len(sentence_json):6} bytes  \"{sentence_text[:40]}...\"")
    log_input(f"Created SENTENCE frame: \"{sentence_text[:50]}...\"")

    # End frame
    frame = encode_frame(FRAME_END, b'')
    frames.extend(frame)
    frame_log.append(f"END      {0:6} bytes")
    log_input(f"Created END frame")

    print(f"\n  {DIM}Frame sequence:{RESET}")
    for i, entry in enumerate(frame_log[:5]):
        print(f"    {i+1:3}. {entry}")
    if len(frame_log) > 7:
        print(f"    {DIM}... ({len(frame_log) - 7} more AUDIO frames){RESET}")
    for i, entry in enumerate(frame_log[-2:]):
        print(f"    {len(frame_log)-1+i:3}. {entry}")

    print(f"\n  Total: {len(frames):,} bytes → stdin")

    # ═══════════════════════════════════════════════════════════
    # RUN BINARY
    # ═══════════════════════════════════════════════════════════
    log_section("RUNNING RHUBARB-STREAM")

    start_time = datetime.now()

    proc = subprocess.Popen(
        [binary_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    stdout, stderr = proc.communicate(input=bytes(frames))

    elapsed = (datetime.now() - start_time).total_seconds()

    # ═══════════════════════════════════════════════════════════
    # PARSE OUTPUT
    # ═══════════════════════════════════════════════════════════
    log_section("OUTPUT STREAM")

    visemes = []
    debug_msgs = []
    errors = []

    for line in stdout.decode('utf-8').strip().split('\n'):
        if not line:
            continue
        try:
            msg = json.loads(line)
            msg_type = msg.get('type', 'unknown')

            if msg_type == 'ready':
                log_process("Ready signal received")
            elif msg_type == 'debug':
                debug_msg = msg.get('message', '')
                debug_msgs.append(debug_msg)
                log_process(f"{DIM}{debug_msg}{RESET}")
            elif msg_type == 'error':
                error_msg = msg.get('message', '')
                errors.append(error_msg)
                log_error(error_msg)
            elif msg_type == 'viseme':
                visemes.append({
                    'start': msg['start'],
                    'end': msg['end'],
                    'value': msg['value']
                })
                log_output(f"viseme {MAGENTA}{msg['value']}{RESET} ({msg['start']:.2f}s - {msg['end']:.2f}s)")
            elif msg_type == 'end':
                log_process(f"End signal: duration={msg.get('duration', 0):.2f}s")
            else:
                print(f"  {DIM}(unknown) {line}{RESET}")

        except json.JSONDecodeError:
            print(f"  {DIM}(raw) {line}{RESET}")

    # ═══════════════════════════════════════════════════════════
    # SUMMARY
    # ═══════════════════════════════════════════════════════════
    log_section("SUMMARY")

    print(f"  {BOLD}Input:{RESET}")
    print(f"    Audio:     {duration:.2f}s ({len(pcm_data) // 2:,} samples)")
    print(f"    Frames:    {len(frame_log)} ({chunk_count} audio + 1 sentence + 1 end)")
    print(f"    Bytes:     {len(frames):,}")

    print(f"\n  {BOLD}Processing:{RESET}")
    print(f"    Time:      {elapsed:.2f}s")
    print(f"    Speed:     {duration / elapsed:.1f}x realtime")
    print(f"    Debug:     {len(debug_msgs)} messages")

    print(f"\n  {BOLD}Output:{RESET}")
    print(f"    Visemes:   {len(visemes)}")
    print(f"    Errors:    {len(errors)}")

    if visemes:
        # Shape distribution
        shapes = {}
        for v in visemes:
            shapes[v['value']] = shapes.get(v['value'], 0) + 1

        print(f"\n  {BOLD}Shape Distribution:{RESET}")
        for shape in sorted(shapes.keys()):
            count = shapes[shape]
            bar = '█' * (count // 2) if count > 1 else '▌'
            print(f"    {shape}: {bar} {count}")

    if stderr:
        print(f"\n  {BOLD}Stderr:{RESET}")
        print(f"    {stderr.decode('utf-8')}")

    print()
    return 0 if not errors else 1

if __name__ == "__main__":
    sys.exit(main())
