#!/usr/bin/env python3
"""
Flash the FitnessAI Watch without having to know which COM port it landed on.

The board has shown up on COM5, COM6 and COM7 across a single bring-up session,
so hard-coding a port in the flash command just produces confusing errors. This
finds it by USB vendor ID instead (Espressif = 0x303A) and flashes the binary.

    python watch/flash.py                 # flash the ESP32 firmware, then monitor
    python watch/flash.py --no-monitor    # flash and exit
    python watch/flash.py --monitor-only  # just open the serial monitor
    python watch/flash.py --bin PATH      # flash a specific merged.bin

It deliberately uses `python -m esptool` rather than arduino-cli's uploader:
Windows Smart App Control blocks the riscv32 toolchain that ships with the ESP32
core, but esptool is pure Python and runs fine. See watch/README.md.
"""

import argparse
import os
import subprocess
import sys
import time

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    sys.exit("pyserial is missing.  pip install pyserial esptool")

ESPRESSIF_VID = 0x303A
DEFAULT_BIN = os.path.expanduser(
    "~/Downloads/fitness-watch-firmware/fitness_watch/fitness_watch.ino.merged.bin"
)


def find_board(timeout_s=0):
    """Return the port the ESP32 is on, waiting up to timeout_s for it to appear."""
    deadline = time.time() + timeout_s
    while True:
        for p in serial.tools.list_ports.comports():
            if p.vid == ESPRESSIF_VID:
                return p.device
            # Some clones enumerate through a bridge chip rather than native USB.
            if p.hwid and ("303A" in p.hwid.upper() or "CP210" in (p.description or "")):
                return p.device
        if time.time() >= deadline:
            return None
        time.sleep(1)


def monitor(port, baud=115200):
    """Print serial output until interrupted. Mirrors the Arduino monitor."""
    print(f"--- monitor {port} @ {baud} (ctrl-c to stop) ---")
    try:
        with serial.Serial(port, baud, timeout=1) as s:
            while True:
                line = s.readline()
                if line:
                    sys.stdout.write(line.decode("utf-8", "replace"))
                    sys.stdout.flush()
    except KeyboardInterrupt:
        print("\n--- monitor stopped ---")
    except serial.SerialException as e:
        print(f"monitor failed: {e}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--bin", default=DEFAULT_BIN, help="merged .bin to flash")
    ap.add_argument("--port", help="skip auto-detection and use this port")
    ap.add_argument("--baud", type=int, default=460800)
    ap.add_argument("--wait", type=int, default=0,
                    help="seconds to wait for the board to appear")
    ap.add_argument("--no-monitor", action="store_true")
    ap.add_argument("--monitor-only", action="store_true")
    args = ap.parse_args()

    port = args.port or find_board(args.wait)
    if not port:
        print("No ESP32 found on USB.")
        print("Ports currently present:")
        for p in serial.tools.list_ports.comports():
            print(f"    {p.device}  {p.description}")
        print("\nIf the board is plugged in but not listed:")
        print("  - check the board's power LED is lit")
        print("  - try a different USB cable (charge-only cables carry no data)")
        print("  - hold BOOT, tap RESET, release BOOT to force download mode")
        print("  - re-run with --wait 30 to poll while you do that")
        return 1

    print(f"board on {port}")

    if args.monitor_only:
        monitor(port)
        return 0

    if not os.path.isfile(args.bin):
        print(f"binary not found: {args.bin}")
        print("Download it from the Build Watch Firmware run's artifacts.")
        return 1

    cmd = [sys.executable, "-m", "esptool", "--chip", "esp32c3",
           "--port", port, "--baud", str(args.baud),
           "write-flash", "0x0", args.bin]
    print(" ".join(cmd))
    rc = subprocess.call(cmd)
    if rc != 0:
        print("\nFlash failed.")
        print("If it timed out: hold BOOT, tap RESET, release BOOT, then re-run.")
        return rc

    if not args.no_monitor:
        # The board re-enumerates after a flash; give it a moment and re-find it,
        # since it can come back on a different port number.
        time.sleep(2)
        monitor(args.port or find_board(10) or port)
    return 0


if __name__ == "__main__":
    sys.exit(main())
