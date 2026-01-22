#!/usr/bin/env python3
"""
Test script for rhubarb-stream binary.

Sends audio + sentence frames and displays viseme output.
"""

import subprocess
import struct
import wave
import sys
import json
import os

# Frame types
FRAME_AUDIO = 0x01
FRAME_SENTENCE = 0x02
FRAME_CONFIG = 0x03
FRAME_RESET = 0x04
FRAME_END = 0xFF

def encode_frame(frame_type: int, payload: bytes) -> bytes:
    """Encode a binary frame: [type:1][length:4 LE][payload:N]"""
    return struct.pack('<BI', frame_type, len(payload)) + payload

def load_wav_as_pcm16(wav_path: str) -> tuple[bytes, int]:
    """Load WAV file and return PCM data + sample rate."""
    with wave.open(wav_path, 'rb') as wav:
        sample_rate = wav.getframerate()
        n_channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        n_frames = wav.getnframes()

        print(f"WAV: {sample_rate}Hz, {n_channels}ch, {sample_width*8}bit, {n_frames} frames")

        # Read all frames
        pcm_data = wav.readframes(n_frames)

        # If stereo, convert to mono (take left channel)
        if n_channels == 2:
            import array
            samples = array.array('h')
            samples.frombytes(pcm_data)
            mono_samples = samples[::2]  # Take every other sample (left channel)
            pcm_data = mono_samples.tobytes()
            print(f"Converted stereo to mono: {len(mono_samples)} samples")

        return pcm_data, sample_rate

def generate_test_tone(duration_seconds: float = 2.0, sample_rate: int = 16000) -> bytes:
    """Generate a simple test tone (440Hz sine wave with some silence)."""
    import array
    import math

    samples = array.array('h')
    n_samples = int(duration_seconds * sample_rate)

    # Generate pattern: silence, tone, silence, tone
    for i in range(n_samples):
        t = i / sample_rate
        # Create segments
        if 0.1 < t < 0.5 or 0.7 < t < 1.1 or 1.3 < t < 1.8:
            # Tone segment (440Hz)
            val = int(16000 * math.sin(2 * math.pi * 440 * t))
        else:
            # Silence
            val = 0
        samples.append(val)

    print(f"Generated test tone: {n_samples} samples, {duration_seconds}s")
    return samples.tobytes()

def resample_if_needed(pcm_data: bytes, src_rate: int, target_rate: int = 16000) -> bytes:
    """Resample PCM data if sample rate doesn't match."""
    if src_rate == target_rate:
        return pcm_data

    # Simple resampling using linear interpolation
    import array
    samples = array.array('h')
    samples.frombytes(pcm_data)

    ratio = src_rate / target_rate
    new_length = int(len(samples) / ratio)
    new_samples = array.array('h')

    for i in range(new_length):
        src_idx = i * ratio
        idx = int(src_idx)
        if idx >= len(samples) - 1:
            new_samples.append(samples[-1])
        else:
            frac = src_idx - idx
            val = int(samples[idx] * (1 - frac) + samples[idx + 1] * frac)
            new_samples.append(max(-32768, min(32767, val)))

    print(f"Resampled from {src_rate}Hz to {target_rate}Hz: {len(new_samples)} samples")
    return new_samples.tobytes()

def create_sentence_json(text: str, start: float, end: float) -> str:
    """Create sentence JSON payload."""
    return json.dumps({
        "text": text,
        "start": start,
        "end": end,
        "words": []  # Empty words array for now
    })

def main():
    # Find the binary
    script_dir = os.path.dirname(os.path.abspath(__file__))
    binary_path = os.path.join(script_dir, "build", "rhubarb-stream")

    if not os.path.exists(binary_path):
        print(f"Error: Binary not found at {binary_path}")
        print("Run: mkdir build && cd build && cmake .. && make")
        sys.exit(1)

    print(f"Binary: {binary_path}")
    print()

    # Try to find test audio, fall back to generated tone
    wav_path = os.path.join(script_dir, "..", "resources", "audio.wav")
    use_generated = True

    if os.path.exists(wav_path):
        # Check if it's a real WAV file (not LFS pointer)
        with open(wav_path, 'rb') as f:
            header = f.read(4)
            if header == b'RIFF':
                use_generated = False
                print(f"Audio: {wav_path}")

    if use_generated:
        print("Audio: Generated test tone (no real WAV available)")
        pcm_data = generate_test_tone(2.0, 16000)
    else:
        # Load and prepare audio
        pcm_data, sample_rate = load_wav_as_pcm16(wav_path)
        pcm_data = resample_if_needed(pcm_data, sample_rate, 16000)

    duration = len(pcm_data) // 2 / 16000  # 2 bytes per sample, 16kHz
    print(f"Audio duration: {duration:.2f}s")
    print()

    # Create sentence (covering the full audio)
    sentence_text = "Hello world, this is a test of the lip sync system."
    sentence_json = create_sentence_json(sentence_text, 0.0, duration)

    # Build frames
    frames = bytearray()

    # Send audio in chunks (simulate streaming)
    chunk_size = 16000  # 1 second of audio (16kHz * 2 bytes = 32000 bytes per second)
    chunk_bytes = chunk_size * 2

    for i in range(0, len(pcm_data), chunk_bytes):
        chunk = pcm_data[i:i + chunk_bytes]
        frames.extend(encode_frame(FRAME_AUDIO, chunk))

    print(f"Encoded {len(pcm_data) // chunk_bytes + 1} audio chunks")

    # Send sentence
    frames.extend(encode_frame(FRAME_SENTENCE, sentence_json.encode('utf-8')))
    print(f"Sentence: '{sentence_text}'")

    # Send END
    frames.extend(encode_frame(FRAME_END, b''))

    print()
    print("=" * 60)
    print("Running rhubarb-stream...")
    print("=" * 60)
    print()

    # Run binary
    proc = subprocess.Popen(
        [binary_path],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE
    )

    stdout, stderr = proc.communicate(input=bytes(frames))

    # Parse output
    print("Output:")
    viseme_count = 0
    for line in stdout.decode('utf-8').strip().split('\n'):
        if not line:
            continue
        try:
            msg = json.loads(line)
            if msg.get('type') == 'viseme':
                viseme_count += 1
                print(f"  viseme: {msg['value']} ({msg['start']:.2f}s - {msg['end']:.2f}s)")
            elif msg.get('type') == 'debug':
                print(f"  [debug] {msg.get('message', '')}")
            elif msg.get('type') == 'error':
                print(f"  [ERROR] {msg.get('message', '')}")
            elif msg.get('type') == 'ready':
                print(f"  [ready]")
            elif msg.get('type') == 'end':
                print(f"  [end] duration={msg.get('duration', 0):.2f}s")
            else:
                print(f"  {line}")
        except json.JSONDecodeError:
            print(f"  (raw) {line}")

    print()
    print(f"Total visemes: {viseme_count}")

    if stderr:
        print()
        print("Stderr:")
        print(stderr.decode('utf-8'))

    return proc.returncode

if __name__ == "__main__":
    sys.exit(main())
