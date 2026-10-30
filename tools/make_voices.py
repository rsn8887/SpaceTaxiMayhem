#!/usr/bin/env python3
"""Generate the passenger voice clips with espeak-ng and crunch them down to
8-bit / 11 kHz mono so they sound like old Amiga samples.

Usage: ESPEAK=/path/to/espeak-ng python3 make_voices.py <outdir>
"""
import os, subprocess, sys, tempfile, wave
import numpy as np

ESPEAK = os.environ.get("ESPEAK", "espeak-ng")
OUT = sys.argv[1] if len(sys.argv) > 1 else "assets/voice"
os.makedirs(OUT, exist_ok=True)

NUM = ["one", "two", "three", "four", "five", "six", "seven", "eight", "nine"]
PHRASES = {"hey": "Hey, taxi!", "up": "Up, please!", "thanks": "Thanks!", "ouch": "Ouch!"}
for i, n in enumerate(NUM):
    PHRASES[f"pad{i+1}"] = f"Pad {n}, please!"

# (voice, pitch, speed, amplitude)
VOICES = [
    ("en-us+m3", 55, 150),
    ("en-us+f4", 72, 160),
    ("en-gb+m7", 38, 145),
]

def synth(text, voice, pitch, speed):
    with tempfile.NamedTemporaryFile(suffix=".wav", delete=False) as f:
        path = f.name
    subprocess.run([ESPEAK, "-v", voice, "-p", str(pitch), "-s", str(speed),
                    "-a", "180", "-w", path, text], check=True)
    with wave.open(path) as w:
        sr = w.getframerate()
        data = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).astype(np.float32) / 32768.0
    os.unlink(path)
    return data, sr

def process(x, sr, target=11025):
    # trim silence
    idx = np.where(np.abs(x) > 0.02)[0]
    if len(idx):
        x = x[max(0, idx[0] - 200): idx[-1] + 400]
    # simple low-pass (moving average) before decimation
    k = max(1, int(sr / target))
    x = np.convolve(x, np.ones(k) / k, mode="same")
    # resample (linear)
    n = int(len(x) * target / sr)
    t = np.linspace(0, len(x) - 1, n)
    y = np.interp(t, np.arange(len(x)), x)
    # a touch of saturation for that crunchy sampler feel, then normalise
    y = np.tanh(y * 2.2)
    y = y / (np.max(np.abs(y)) + 1e-9) * 0.95
    # 8-bit unsigned
    return np.clip(np.round(y * 127 + 128), 0, 255).astype(np.uint8), target

for vi, (voice, pitch, speed) in enumerate(VOICES):
    for key, text in PHRASES.items():
        x, sr = synth(text, voice, pitch, speed)
        y, tsr = process(x, sr)
        fn = os.path.join(OUT, f"v{vi}_{key}.wav")
        with wave.open(fn, "wb") as w:
            w.setnchannels(1); w.setsampwidth(1); w.setframerate(tsr)
            w.writeframes(y.tobytes())
print("voices written to", OUT)
