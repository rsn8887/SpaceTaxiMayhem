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

# Voice sets, indexed by the game (see VOICE_SETS in src/main.cpp):
#   (espeak voice, pitch 0-99, speed wpm, extra pitch-up factor applied by resampling)
VOICES = [
    ("en-us+m3", 50, 150, 1.0),        # 0 man
    ("en-gb+m7", 32, 140, 1.0),        # 1 man, deeper
    ("en-us+f4", 72, 160, 1.0),        # 2 woman
    ("en-us+f1", 58, 155, 1.0),        # 3 woman, a bit lower
    ("en-us+f5", 80, 170, 1.30),       # 4 kid (boy): high pitch, pitched up further
    ("en-us+f4", 95, 170, 1.30),       # 5 kid (girl)
    ("en-us+grandpa", 30, 100, 1.0),   # 6 old man: slow
    ("en-us+grandma", 55, 100, 1.0),   # 7 old woman: slow
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

def process(x, sr, target=11025, shift=1.0):
    # trim silence
    idx = np.where(np.abs(x) > 0.02)[0]
    if len(idx):
        x = x[max(0, idx[0] - 200): idx[-1] + 400]
    # simple low-pass (moving average) before decimation
    k = max(1, int(sr / target))
    x = np.convolve(x, np.ones(k) / k, mode="same")
    # resample (linear)
    # shift > 1 plays the clip faster, i.e. raises the pitch (and tempo) like a sampler would
    n = int(len(x) * target / sr / shift)
    t = np.linspace(0, len(x) - 1, n)
    y = np.interp(t, np.arange(len(x)), x)
    # a touch of saturation for that crunchy sampler feel, then normalise
    y = np.tanh(y * 2.2)
    y = y / (np.max(np.abs(y)) + 1e-9) * 0.95
    # 8-bit unsigned
    return np.clip(np.round(y * 127 + 128), 0, 255).astype(np.uint8), target

for vi, (voice, pitch, speed, shift) in enumerate(VOICES):
    for key, text in PHRASES.items():
        x, sr = synth(text, voice, pitch, speed)
        y, tsr = process(x, sr, shift=shift)
        fn = os.path.join(OUT, f"v{vi}_{key}.wav")
        with wave.open(fn, "wb") as w:
            w.setnchannels(1); w.setsampwidth(1); w.setframerate(tsr)
            w.writeframes(y.tobytes())
print("voices written to", OUT)
