#!/usr/bin/env python3
"""Convert an audio file (MP3/OGG/FLAC/WAV) to a mono 16-bit PCM WAV sized for the
ESP32-S3 + MAX98357A player.

The firmware's WAV parser (main/main.c) requires:
    * RIFF/WAVE container
    * audio_format == 1 (PCM)
    * bits_per_sample == 16

Default target matches the existing spiffs/cicada.wav: 16 kHz mono 16-bit.

Usage:
    python mp3_to_mcu_wav.py in.mp3 out.wav [--rate 16000] [--peak -1.0] [--fade 10]

Requires: numpy, soundfile (bundles libsndfile with MP3 support).
"""

from __future__ import annotations

import argparse
import sys
import wave
from pathlib import Path

import numpy as np
import soundfile as sf


def design_lowpass(cutoff_hz: float, sample_rate: int, numtaps: int = 257) -> np.ndarray:
    """Windowed-sinc FIR low-pass, normalised to unity DC gain."""
    fc = cutoff_hz / sample_rate
    if not 0.0 < fc < 0.5:
        raise ValueError(f"cutoff {cutoff_hz} Hz is not below Nyquist of {sample_rate} Hz")
    if numtaps % 2 == 0:
        numtaps += 1
    n = np.arange(numtaps, dtype=np.float64)
    mid = (numtaps - 1) / 2.0
    h = 2.0 * fc * np.sinc(2.0 * fc * (n - mid))
    h *= np.blackman(numtaps)
    total = h.sum()
    if total == 0:
        raise ValueError("degenerate filter design")
    return (h / total).astype(np.float32)


def resample_audio(x: np.ndarray, src_rate: int, dst_rate: int) -> np.ndarray:
    """Anti-aliased resample. Integer downsampling uses FIR + decimation;
    anything else falls back to FIR + linear interpolation."""
    if src_rate == dst_rate:
        return x

    # Cutoff safely below the lower of the two Nyquist limits.
    cutoff = 0.45 * min(src_rate, dst_rate)
    h = design_lowpass(cutoff, src_rate)
    y = np.convolve(x, h, mode="same")

    ratio = dst_rate / src_rate
    n_out = int(round(len(y) * ratio))
    if float(src_rate) % float(dst_rate) == 0.0:
        step = src_rate // dst_rate
        out = y[::step]
    else:
        idx = np.arange(n_out, dtype=np.float64) * (src_rate / dst_rate)
        out = np.interp(idx, np.arange(len(y), dtype=np.float64), y)

    return out[:n_out].astype(np.float32)


def to_mono(x: np.ndarray) -> np.ndarray:
    if x.ndim == 1:
        return x
    if x.shape[1] == 1:
        return x[:, 0]
    return x.mean(axis=1)


def apply_fade(x: np.ndarray, sample_rate: int, fade_ms: float) -> np.ndarray:
    n = int(sample_rate * fade_ms / 1000.0)
    if n <= 1 or 2 * n >= len(x):
        return x
    ramp = np.linspace(0.0, 1.0, n, dtype=np.float32)
    x = x.copy()
    x[:n] *= ramp
    x[-n:] *= ramp[::-1]
    return x


def normalize_peak(x: np.ndarray, peak_dbfs: float) -> np.ndarray:
    current = float(np.max(np.abs(x))) if len(x) else 0.0
    if current <= 0.0:
        return x
    target = 10.0 ** (peak_dbfs / 20.0)
    return (x * (target / current)).astype(np.float32)


def write_pcm16_wav(path: Path, samples_f32: np.ndarray, sample_rate: int) -> None:
    """Write a canonical 44-byte-header mono 16-bit PCM WAV."""
    clipped = np.clip(samples_f32, -1.0, 1.0)
    pcm = np.round(clipped * 32767.0).astype("<i2")
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(sample_rate)
        w.writeframes(pcm.tobytes())


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("src", type=Path)
    ap.add_argument("dst", type=Path)
    ap.add_argument("--rate", type=int, default=16000, help="target sample rate (default 16000)")
    ap.add_argument("--peak", type=float, default=-1.0, help="target peak in dBFS (default -1.0)")
    ap.add_argument("--fade", type=float, default=10.0, help="fade in/out in ms (default 10)")
    ap.add_argument("--no-normalize", action="store_true")
    args = ap.parse_args()

    if not args.src.is_file():
        print(f"error: no such file: {args.src}", file=sys.stderr)
        return 1

    x, src_rate = sf.read(str(args.src), dtype="float32", always_2d=True)
    src_channels = x.shape[1]
    x = to_mono(x)

    y = resample_audio(x, src_rate, args.rate)
    y = apply_fade(y, args.rate, args.fade)
    if not args.no_normalize:
        y = normalize_peak(y, args.peak)

    write_pcm16_wav(args.dst, y, args.rate)

    peak = np.max(np.abs(y)) if len(y) else 0.0
    rms = np.sqrt(np.mean(y ** 2)) if len(y) else 0.0
    print(f"{args.src.name}")
    print(f"  in : {src_rate} Hz, {src_channels} ch, {len(x)/src_rate:.2f} s")
    print(f"  out: {args.rate} Hz, 1 ch, 16-bit PCM, {len(y)/args.rate:.2f} s")
    print(f"  peak {20*np.log10(peak+1e-12):+.2f} dBFS, rms {20*np.log10(rms+1e-12):+.2f} dBFS")
    print(f"  -> {args.dst}  ({args.dst.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
