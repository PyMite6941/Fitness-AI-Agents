#!/usr/bin/env python3
"""
simctl.py — build and run the FitnessAI Watch firmware in the Wokwi simulator.

    python simctl.py build          compile the firmware with -DSIM_BUILD=1
    python simctl.py build --hw     compile the REAL hardware firmware (no sim flag)
    python simctl.py lint           validate diagram.json (no token needed)
    python simctl.py run            open the simulator interactively (needs a token)
    python simctl.py test           run every scenario in scenarios/ and report
    python simctl.py test boot      run one scenario by name

Why a script instead of a bare arduino-cli line: the simulator build differs from
the hardware build in two ways that are easy to get wrong by hand, and getting
either wrong produces a sim that hangs with no output.

  1. -DSIM_BUILD=1  — swaps in the synthetic MAX30105 and drops BLE (Wokwi has no
                      Bluetooth controller to emulate). See firmware/.../sim.h.
  2. CDCOnBoot=default (NOT =cdc, which the hardware build uses)
                    — with CDC-on-boot enabled, `Serial` is the USB-CDC device.
                      Wokwi's serial monitor listens on UART0, so every
                      Serial.print would vanish and every scenario's
                      `wait-serial` would time out. Disabling CDC-on-boot points
                      `Serial` back at UART0, which the emulator does provide.
"""

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

# This project lives under a path containing Japanese characters
# (OneDrive/ドキュメント). Python's default Windows console encoding is cp1252,
# which cannot encode them, so merely echoing the build command would raise
# UnicodeEncodeError before arduino-cli was ever launched.
for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

SIM_DIR = Path(__file__).resolve().parent
SKETCH = SIM_DIR.parent / "firmware" / "fitness_watch"
BUILD = SIM_DIR / "build"
SCENARIOS = SIM_DIR / "scenarios"

# Hardware target, for reference — this is what gets flashed to the real watch.
FQBN_HW = "esp32:esp32:esp32c3:CDCOnBoot=cdc,PartitionScheme=huge_app"
# Simulator target: identical except Serial is routed to UART0 (see docstring).
FQBN_SIM = "esp32:esp32:esp32c3:CDCOnBoot=default,PartitionScheme=huge_app"

LIBS = [
    "U8g2",
    "SparkFun MAX3010x Pulse and Proximity Sensor Library",
    "Adafruit MPU6050",
    "ArduinoJson",
]


def die(msg: str, code: int = 1):
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(code)


def need(tool: str, hint: str) -> str:
    """Resolve a tool to its FULL path.

    The full path matters on Windows: npx and arduino-cli are .cmd shims, and
    subprocess without shell=True will not append the extension, so passing the
    bare name raises FileNotFoundError even though the tool is installed.
    """
    path = shutil.which(tool)
    if not path:
        die(f"{tool} not found on PATH.\n  {hint}")
    return path


def run(cmd, **kw) -> int:
    print("+ " + " ".join(str(c) for c in cmd))
    return subprocess.call(cmd, **kw)


# ── build ────────────────────────────────────────────────────────────────────
def cmd_build(args) -> int:
    cli = need("arduino-cli", "Install from https://arduino.github.io/arduino-cli/")

    sim = not args.hw
    out = BUILD if sim else (SIM_DIR / "build-hw")
    out.mkdir(parents=True, exist_ok=True)

    cmd = [
        cli, "compile",
        "--fqbn", FQBN_SIM if sim else FQBN_HW,
        "--output-dir", str(out),
        "--warnings", "default",
    ]
    if sim:
        # compiler.cpp.extra_flags is empty in the esp32 platform.txt, so
        # replacing it is safe and does not clobber board-level defines.
        flags = "-DSIM_BUILD=1"
        if args.wifi:
            flags += " -DSIM_WIFI=1"
        if args.bpm:
            flags += f" -DSIM_HR_BPM={args.bpm}"
        cmd += ["--build-property", f"compiler.cpp.extra_flags={flags}"]
    cmd.append(str(SKETCH))

    rc = run(cmd)
    if rc != 0:
        return rc

    # Wokwi wants a single flashable image. The esp32 core emits .merged.bin
    # (bootloader + partitions + app); plain .bin is the app only and boots to
    # nothing in the emulator.
    merged = out / "fitness_watch.ino.merged.bin"
    if sim and not merged.exists():
        found = sorted(p.name for p in out.glob("*.bin"))
        die(f"expected {merged.name} but the build produced: {found}")

    if sim:
        print(f"\nok: {merged.relative_to(SIM_DIR)} "
              f"({merged.stat().st_size / 1024:.0f} KB)")
        print("next: python simctl.py test")
    return 0


# ── wokwi ────────────────────────────────────────────────────────────────────
def wokwi_cmd() -> list:
    """wokwi-cli, preferring a global install and falling back to npx."""
    local = shutil.which("wokwi-cli")
    if local:
        return [local]
    npx = need("npx", "Install Node.js from https://nodejs.org/")
    return [npx, "--yes", "wokwi-cli"]


def check_token():
    if os.environ.get("WOKWI_CLI_TOKEN"):
        return
    die(
        "WOKWI_CLI_TOKEN is not set.\n"
        "  The simulator itself is free; the CLI just needs a token to identify you.\n"
        "  1. Sign in at https://wokwi.com/  (GitHub/Google login is fine)\n"
        "  2. Open https://wokwi.com/dashboard/ci  and create a token\n"
        "  3. PowerShell:  $env:WOKWI_CLI_TOKEN = 'wok_...'\n"
        "     bash:        export WOKWI_CLI_TOKEN=wok_...\n"
        "  Or skip the CLI entirely and use the browser — see sim/README.md."
    )


def ensure_built():
    merged = BUILD / "fitness_watch.ino.merged.bin"
    if not merged.exists():
        die("no simulator build found. Run: python simctl.py build")


def cmd_lint(args) -> int:
    """Validate diagram.json against Wokwi's own part registry.

    Worth running after ANY wiring edit. Wokwi silently ignores a connection to
    a pin that does not exist, so a typo does not error — it just produces a
    board where, say, the OLED is never powered, and you debug firmware for an
    hour. This caught four such mistakes when the diagram was first written
    (wokwi-ssd1306 uses DATA/CLK, not SDA/SCL; the C3's 3.3 V pad is 3V3.1).
    """
    node = need("node", "Install Node.js from https://nodejs.org/")
    npm = need("npm", "Install Node.js from https://nodejs.org/")

    # Install into a local, gitignored prefix rather than relying on `npx`.
    # npx installs the package into its own temp cache, and an ESM `import
    # '@wokwi/diagram-lint'` from a script elsewhere on disk cannot resolve it
    # from there — the import has to be by a path Node can actually reach.
    tools = SIM_DIR / ".wokwi-tools"
    entry = tools / "node_modules" / "@wokwi" / "diagram-lint" / "dist" / "index.js"
    if not entry.exists():
        print("installing @wokwi/diagram-lint (one time)...")
        tools.mkdir(exist_ok=True)
        rc = run([npm, "install", "--prefix", str(tools), "--silent",
                  "--no-audit", "--no-fund", "@wokwi/diagram-lint"])
        if rc != 0 or not entry.exists():
            die("could not install @wokwi/diagram-lint (offline?)")

    script = tools / "lint-diagram.mjs"
    script.write_text(
        f"import {{ DiagramLinter }} from {json.dumps(entry.as_uri())};\n"
        "import { readFileSync } from 'node:fs';\n"
        "const d = JSON.parse(readFileSync(process.argv[2], 'utf8'));\n"
        "const r = new DiagramLinter().lint(d);\n"
        "for (const i of r.issues) console.log(`[${i.severity}] ${i.rule}: ${i.message}`);\n"
        "console.log(r.valid ? 'diagram.json: VALID' : 'diagram.json: INVALID');\n"
        "process.exit(r.valid ? 0 : 1);\n",
        encoding="utf-8",
    )
    return run([node, str(script), str(SIM_DIR / "diagram.json")])


def cmd_run(args) -> int:
    ensure_built()
    check_token()
    return run(wokwi_cmd() + [str(SIM_DIR), "--interactive",
                              "--timeout", str(args.timeout)])


def cmd_test(args) -> int:
    ensure_built()
    check_token()

    files = sorted(SCENARIOS.glob("*.test.yaml"))
    if args.name:
        files = [f for f in files if f.name.startswith(args.name)]
        if not files:
            die(f"no scenario matching '{args.name}' in {SCENARIOS}")

    results = []
    for f in files:
        print(f"\n{'=' * 60}\n  scenario: {f.stem}\n{'=' * 60}")
        rc = run(wokwi_cmd() + [str(SIM_DIR),
                                "--scenario", str(f.relative_to(SIM_DIR)),
                                "--timeout", str(args.timeout)])
        results.append((f.stem, rc))

    print(f"\n{'=' * 60}\n  RESULTS\n{'=' * 60}")
    for name, rc in results:
        print(f"  {'PASS' if rc == 0 else 'FAIL'}  {name}")
    failed = [n for n, rc in results if rc != 0]
    print(f"\n{len(results) - len(failed)}/{len(results)} passed")
    return 1 if failed else 0


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    b = sub.add_parser("build", help="compile the firmware")
    b.add_argument("--hw", action="store_true",
                   help="build the real hardware firmware instead of the sim")
    b.add_argument("--wifi", action="store_true",
                   help="sim build joins Wokwi's virtual network (SIM_WIFI=1)")
    b.add_argument("--bpm", type=int, help="override the synthetic pulse rate")
    b.set_defaults(func=cmd_build)

    ln = sub.add_parser("lint", help="validate diagram.json against Wokwi's registry")
    ln.set_defaults(func=cmd_lint)

    r = sub.add_parser("run", help="open the simulator interactively")
    r.add_argument("--timeout", type=int, default=600000, help="ms (default 10 min)")
    r.set_defaults(func=cmd_run)

    t = sub.add_parser("test", help="run the scenario suite")
    t.add_argument("name", nargs="?", help="run only scenarios starting with this")
    t.add_argument("--timeout", type=int, default=60000, help="ms per scenario")
    t.set_defaults(func=cmd_test)

    args = p.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
