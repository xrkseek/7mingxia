#!/usr/bin/env python3
"""Reset the board and capture serial output for a bounded time.

`idf.py monitor` block-buffers its stdout when it is piped, which hides the
output until the process exits. This script reads the port directly and flushes
every chunk, so it works when redirected to a file or captured by a tool.

Usage:
    python serial_capture.py [PORT] [--baud 115200] [--seconds 75]
"""

from __future__ import annotations

import argparse
import sys
import time

import serial


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("port", nargs="?", default="COM6")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=75.0)
    ap.add_argument("--no-reset", action="store_true")
    args = ap.parse_args()

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    try:
        if not args.no_reset:
            # esptool's hard-reset sequence: IO0=HIGH, pulse EN low, release.
            ser.setDTR(False)   # IO0 = HIGH -> normal boot, not download mode
            ser.setRTS(True)    # EN = LOW  -> chip held in reset
            time.sleep(0.15)
            ser.reset_input_buffer()
            ser.setRTS(False)   # EN = HIGH -> run

        print(f"--- capturing {args.port} @ {args.baud} for {args.seconds:.0f}s ---",
              flush=True)

        deadline = time.time() + args.seconds
        reconnects = 0
        while time.time() < deadline:
            try:
                chunk = ser.read(4096)
            except (serial.SerialException, OSError) as exc:
                # CH34x adapters occasionally invalidate the handle mid-read
                # (GetOverlappedResult -> PermissionError). Reopen and keep
                # capturing instead of aborting the whole run.
                reconnects += 1
                if reconnects > 5:
                    print(f"\n--- giving up after {reconnects} reconnects: {exc} ---",
                          flush=True)
                    break
                print(f"\n--- serial error ({exc}); reopening {args.port} "
                      f"[{reconnects}] ---", flush=True)
                try:
                    ser.close()
                except Exception:
                    pass
                time.sleep(0.5)
                try:
                    ser = serial.Serial(args.port, args.baud, timeout=0.2)
                except Exception as reopen_exc:
                    print(f"--- reopen failed: {reopen_exc} ---", flush=True)
                    time.sleep(1.0)
                continue
            if chunk:
                sys.stdout.write(chunk.decode("utf-8", "replace"))
                sys.stdout.flush()

        print(f"\n--- capture finished ({args.seconds:.0f}s) ---", flush=True)
    finally:
        try:
            ser.close()
        except Exception:
            pass
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
