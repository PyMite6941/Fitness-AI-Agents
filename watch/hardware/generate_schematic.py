#!/usr/bin/env python3
"""
generate_schematic.py — build `fitness watch.kicad_sch` from the netlist below.

Run it, then open the project in KiCad (or run `kicad-cli sch erc`).

WHY GENERATED: the previous schematic had every symbol placed and *zero* wires,
so ERC reported ~40 unconnected pins and the file documented nothing. Hand-wiring
is fine once, but the pin assignment lives in
`watch/firmware/fitness_watch/config.h` and changes as the firmware does. Keeping
the netlist here as data means the schematic can be regenerated to match instead
of drifting from it.

STYLE: connections are made with **net labels** rather than long drawn wires.
Every pin gets a short stub and a label; identically-named labels are the same
net. This is standard practice for dense schematics and is what makes the file
safe to generate — there is no routing to get subtly wrong.

The parts are BREAKOUT MODULES, not bare ICs, because that is what actually gets
soldered. So the symbols carry module pinouts (VIN/GND/SCL/SDA...), not the
QFN pinout of a raw MAX30102.
"""

import os
import re
import sys
import uuid
from pathlib import Path

for _s in (sys.stdout, sys.stderr):
    try:
        _s.reconfigure(encoding="utf-8", errors="replace")
    except (AttributeError, ValueError):
        pass

HERE = Path(__file__).resolve().parent
OUT = HERE / "fitness watch.kicad_sch"
PROJECT = "fitness watch"

KICAD_SYMBOLS = Path(
    os.environ.get(
        "KICAD_SYMBOL_DIR",
        r"C:\Users\gresh\AppData\Local\Programs\KiCad\10.0\share\kicad\symbols",
    )
)

SHEET_UUID = "5c4eca4c-3067-4428-b9a9-e5f079aadd75"   # keep stable across regens
STUB = 5.08          # mm of wire between a pin and its label
GRID = 1.27


def uid(seed: str) -> str:
    """Deterministic UUIDs, so regenerating produces a clean git diff."""
    return str(uuid.uuid5(uuid.NAMESPACE_URL, "fitnesswatch/" + seed))


# ── custom module symbols ────────────────────────────────────────────────────
# Pin tuples are (name, number, side, type). Sides are 'L' or 'R'; the generator
# lays them out top-to-bottom and sizes the body to fit.

MODULES = {
    "ESP32-C3-SuperMini": dict(
        desc="ESP32-C3 SuperMini module",
        pins=[
            ("5V",     "1",  "L", "passive"),
            ("GND",    "3",  "L", "power_in"),
            ("3V3",    "5",  "L", "power_out"),
            ("GPIO4",  "7",  "L", "bidirectional"),
            ("GPIO3",  "9",  "L", "bidirectional"),
            ("GPIO2",  "11", "L", "bidirectional"),
            ("GPIO1",  "13", "L", "bidirectional"),
            ("GPIO0",  "15", "L", "bidirectional"),
            ("GPIO5",  "2",  "R", "bidirectional"),
            ("GPIO6",  "4",  "R", "bidirectional"),
            ("GPIO7",  "6",  "R", "bidirectional"),
            ("GPIO8",  "8",  "R", "bidirectional"),
            ("GPIO9",  "10", "R", "bidirectional"),
            ("GPIO10", "12", "R", "bidirectional"),
            ("GPIO20", "14", "R", "bidirectional"),
            ("GPIO21", "16", "R", "bidirectional"),
        ],
    ),
    "SSD1306-OLED-Module": dict(
        desc="SSD1306 128x64 I2C OLED module",
        pins=[("GND", "1", "L", "power_in"), ("VCC", "2", "L", "power_in"),
              ("SCL", "3", "L", "input"),    ("SDA", "4", "L", "bidirectional")],
    ),
    "MAX30102-Module": dict(
        desc="MAX30102 heart-rate / SpO2 breakout",
        pins=[("VIN", "1", "L", "power_in"), ("GND", "2", "L", "power_in"),
              ("SCL", "3", "L", "input"),    ("SDA", "4", "L", "bidirectional"),
              ("INT", "5", "R", "output"),   ("IRD", "6", "R", "passive"),
              ("RD",  "7", "R", "passive")],
    ),
    "MPU6050-GY521": dict(
        desc="MPU6050 GY-521 accelerometer / gyro breakout",
        pins=[("VCC", "1", "L", "power_in"), ("GND", "2", "L", "power_in"),
              ("SCL", "3", "L", "input"),    ("SDA", "4", "L", "bidirectional"),
              ("XDA", "5", "R", "passive"),  ("XCL", "6", "R", "passive"),
              ("AD0", "7", "R", "input"),    ("INT", "8", "R", "output")],
    ),
    # NOTE on the negative pins: OUT-/VOUT- are `passive`, not `power_out`.
    # They are ground RETURNS, not sources. Typing them as power outputs makes
    # ERC see several supplies driving the single GND net and report
    # "Power output and Power output are connected" for every pair.
    "TP4056-Protected": dict(
        desc="TP4056 Li-ion charger with DW01A+FS8205 protection",
        pins=[("IN+",  "1", "L", "passive"),   ("IN-",  "2", "L", "passive"),
              ("BAT+", "3", "L", "passive"),   ("BAT-", "4", "L", "passive"),
              ("OUT+", "5", "R", "power_out"), ("OUT-", "6", "R", "passive")],
    ),
    "MT3608-Boost": dict(
        desc="MT3608 adjustable step-up converter",
        pins=[("VIN+", "1", "L", "power_in"),   ("VIN-", "2", "L", "passive"),
              ("VOUT+", "3", "R", "power_out"), ("VOUT-", "4", "R", "passive")],
    ),
}


def build_module_symbol(name: str, spec: dict, prefixed: bool = True) -> str:
    """Emit a symbol block for one module.

    prefixed=True gives the sheet-local name "fitness-watch:Foo" used inside
    lib_symbols; False gives the bare "Foo" a standalone .kicad_sym needs.
    """
    left = [p for p in spec["pins"] if p[2] == "L"]
    right = [p for p in spec["pins"] if p[2] == "R"]
    rows = max(len(left), len(right))

    half_h = ((rows - 1) * 2.54) / 2 + 2.54
    half_w = 12.7 if name == "ESP32-C3-SuperMini" else 10.16
    pin_x = half_w + 5.08

    def col(pins, side):
        out = []
        top = ((len(pins) - 1) * 2.54) / 2
        for i, (pname, pnum, _s, ptype) in enumerate(pins):
            y = top - i * 2.54
            x = -pin_x if side == "L" else pin_x
            rot = 0 if side == "L" else 180
            out.append(
                f'\t\t\t(pin {ptype} line\n'
                f'\t\t\t\t(at {x} {y} {rot})\n'
                f'\t\t\t\t(length 5.08)\n'
                f'\t\t\t\t(name "{pname}"\n\t\t\t\t\t(effects\n\t\t\t\t\t\t(font\n'
                f'\t\t\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t\t\t)\n\t\t\t\t\t)\n\t\t\t\t)\n'
                f'\t\t\t\t(number "{pnum}"\n\t\t\t\t\t(effects\n\t\t\t\t\t\t(font\n'
                f'\t\t\t\t\t\t\t(size 1.27 1.27)\n\t\t\t\t\t\t)\n\t\t\t\t\t)\n\t\t\t\t)\n'
                f'\t\t\t)'
            )
        return out

    pins_txt = "\n".join(col(left, "L") + col(right, "R"))

    sym_name = ("fitness-watch:" + name) if prefixed else name
    return f'''\t\t(symbol "{sym_name}"
\t\t\t(pin_names
\t\t\t\t(offset 1.016)
\t\t\t)
\t\t\t(exclude_from_sim no)
\t\t\t(in_bom yes)
\t\t\t(on_board yes)
\t\t\t(property "Reference" "U"
\t\t\t\t(at 0 {half_h + 2.54} 0)
\t\t\t\t(effects
\t\t\t\t\t(font
\t\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t\t)
\t\t\t\t)
\t\t\t)
\t\t\t(property "Value" "{name}"
\t\t\t\t(at 0 {-half_h - 2.54} 0)
\t\t\t\t(effects
\t\t\t\t\t(font
\t\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t\t)
\t\t\t\t)
\t\t\t)
\t\t\t(property "Footprint" ""
\t\t\t\t(at 0 0 0)
\t\t\t\t(hide yes)
\t\t\t\t(effects
\t\t\t\t\t(font
\t\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t\t)
\t\t\t\t)
\t\t\t)
\t\t\t(property "Datasheet" ""
\t\t\t\t(at 0 0 0)
\t\t\t\t(hide yes)
\t\t\t\t(effects
\t\t\t\t\t(font
\t\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t\t)
\t\t\t\t)
\t\t\t)
\t\t\t(property "Description" "{spec['desc']}"
\t\t\t\t(at 0 0 0)
\t\t\t\t(hide yes)
\t\t\t\t(effects
\t\t\t\t\t(font
\t\t\t\t\t\t(size 1.27 1.27)
\t\t\t\t\t)
\t\t\t\t)
\t\t\t)
\t\t\t(symbol "{name}_0_1"
\t\t\t\t(rectangle
\t\t\t\t\t(start {-half_w} {half_h})
\t\t\t\t\t(end {half_w} {-half_h})
\t\t\t\t\t(stroke
\t\t\t\t\t\t(width 0.254)
\t\t\t\t\t\t(type default)
\t\t\t\t\t)
\t\t\t\t\t(fill
\t\t\t\t\t\t(type background)
\t\t\t\t\t)
\t\t\t\t)
\t\t\t)
\t\t\t(symbol "{name}_1_1"
{pins_txt}
\t\t\t)
\t\t)'''


# ── stock symbol extraction ─────────────────────────────────────────────────
def stock_symbol(lib_id: str) -> str:
    lib, sym = lib_id.split(":", 1)
    path = KICAD_SYMBOLS / f"{lib}.kicad_sym"
    if not path.exists():
        sys.exit(f"error: symbol library not found: {path}\n"
                 f"  set KICAD_SYMBOL_DIR to your KiCad symbols directory")
    text = path.read_text(encoding="utf-8")
    m = re.search(r'\(symbol "' + re.escape(sym) + r'"[\s\(]', text)
    if not m:
        sys.exit(f"error: symbol {lib_id} not found in {path}")
    start = m.start()
    depth, i = 0, start
    while True:
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                break
        i += 1
    block = text[start:i + 1]
    # Re-key it as "Lib:Symbol" (the file-local name) and re-indent to depth 2.
    block = block.replace(f'(symbol "{sym}"', f'(symbol "{lib_id}"', 1)
    return "\n".join("\t\t" + ln for ln in block.splitlines())


# ── pin geometry ────────────────────────────────────────────────────────────
PIN_RE = re.compile(
    r'\(pin\s+(\w+)\s+\w+\s*\(at\s+([-\d.]+)\s+([-\d.]+)\s+(\d+)\)\s*'
    r'\(length\s+([\d.]+)\)[\s\S]*?\(name\s+"([^"]*)"[\s\S]*?\(number\s+"([^"]*)"',
    re.M,
)


def parse_pins(block: str) -> dict:
    """number -> {name, x, y, rot} in symbol coordinates (y is UP)."""
    pins = {}
    for m in PIN_RE.finditer(block):
        pins[m.group(7)] = dict(type=m.group(1), x=float(m.group(2)),
                                y=float(m.group(3)), rot=int(m.group(4)),
                                name=m.group(6))
    return pins


def pin_point(inst_xy, p):
    """Symbol-local pin -> sheet coordinates. Symbols are placed unrotated, and
    the schematic's Y axis points DOWN while the symbol's points UP."""
    return (round(inst_xy[0] + p["x"], 3), round(inst_xy[1] - p["y"], 3))


COORD_RE = re.compile(r'\((?:start|end|center|mid|xy)\s+(-?[\d.]+)\s+(-?[\d.]+)\)')


def symbol_bbox(block: str):
    """Bounding box of a symbol's GRAPHICS, in symbol coords (y up).

    Pins are deliberately excluded: reference/value text only needs to clear the
    body outline, and including the pin stubs would push it needlessly far out.
    Returns (min_x, min_y, max_x, max_y), or a small default for symbols drawn
    entirely from primitives this does not parse.
    """
    body = re.sub(r'\(pin\s[\s\S]*?\n\t+\)', '', block)
    xs, ys = [], []
    for m in COORD_RE.finditer(body):
        xs.append(float(m.group(1)))
        ys.append(float(m.group(2)))
    if not xs:
        return (-1.27, -1.27, 1.27, 1.27)
    return (min(xs), min(ys), max(xs), max(ys))


def property_anchors(bbox, at):
    """Where to put Reference and Value so they do not land on the symbol.

    Tall-and-thin parts (resistors, caps, the cell) get their text stacked to
    the RIGHT, which is what KiCad does by default and keeps it clear of the
    vertical wire stubs. Everything wider than it is tall gets the reference
    above the body and the value below."""
    min_x, min_y, max_x, max_y = bbox
    w, h = max_x - min_x, max_y - min_y
    cx = at[0]
    if h >= w:
        x = round(at[0] + max_x + 2.54, 3)
        return (x, round(at[1] - 1.27, 3)), (x, round(at[1] + 1.27, 3))
    # Symbol y is up, sheet y is down, so the top of the body is at at.y - max_y.
    return ((cx, round(at[1] - max_y - 2.54, 3)),
            (cx, round(at[1] - min_y + 3.81, 3)))


def outward(p):
    """Unit vector, in sheet coords, pointing AWAY from the symbol body.

    A pin's `rot` is the direction its body extends from the connection point,
    so the wire leaves in the opposite direction. The Y sign flips because the
    sheet's Y axis is inverted relative to the symbol's."""
    import math
    a = math.radians(p["rot"])
    return (-round(math.cos(a)), round(math.sin(a)))


# ── THE DESIGN ───────────────────────────────────────────────────────────────
# Nets are made by name: two pins carrying the same label are the same net.
#
# POWER ARCHITECTURE — "battery unless USB is plugged in":
#
#   cell -> TP4056(protected) -> MT3608 boost @5.1V -> D1 -> ESP32 5V pin
#                                                            ^
#                                   the module's own USB-C feeds the same pin
#
# The ESP32-C3 SuperMini's `5V` pad IS USB VBUS — the same copper. So when USB
# is plugged in that node sits at 5.0 V, while the battery branch can only reach
# 5.1 V minus D1's ~0.35 V = 4.75 V. D1 is therefore reverse-biased and the
# battery is cut out automatically; unplug USB and the node falls to 4.75 V and
# the battery takes over. No firmware, no relay, no switching glitch.
#
# D1 IS the switchover, and it is also what stops USB from back-feeding into the
# boost converter's output.
#
# The charger is fed from ITS OWN USB socket, never from the ESP32's 5V pin.
# Tying them together makes a loop where the battery charges from itself
# (boost -> 5V pin -> charger IN -> cell -> boost) which never terminates and
# flattens the cell. See watch/POWER.md.

PLACEMENTS = [
    dict(ref="U1", lib="fitness-watch:ESP32-C3-SuperMini", value="ESP32-C3 SuperMini",
         at=(106.68, 96.52), nets={
             "5V": "+5V", "GND": "GND", "3V3": "+3V3",
             "GPIO7": "SDA", "GPIO8": "SCL",
             "GPIO4": "BTN_A", "GPIO5": "BTN_B",
             "GPIO3": "VBAT_SENSE",
         },
         nc=["GPIO0", "GPIO1", "GPIO2", "GPIO6", "GPIO9", "GPIO10", "GPIO20", "GPIO21"]),

    dict(ref="U2", lib="fitness-watch:SSD1306-OLED-Module", value="SSD1306 OLED 128x64 (0x3C)",
         at=(203.2, 55.88), nets={"VCC": "+3V3", "GND": "GND", "SCL": "SCL", "SDA": "SDA"}),

    dict(ref="U3", lib="fitness-watch:MAX30102-Module", value="MAX30102 HR/SpO2 (0x57)",
         at=(203.2, 99.06), nets={"VIN": "+3V3", "GND": "GND", "SCL": "SCL", "SDA": "SDA"},
         nc=["INT", "IRD", "RD"]),

    dict(ref="U4", lib="fitness-watch:MPU6050-GY521", value="MPU6050 GY-521 (0x68)",
         at=(203.2, 142.24), nets={"VCC": "+3V3", "GND": "GND", "SCL": "SCL", "SDA": "SDA",
                                   "AD0": "GND"},
         nc=["XDA", "XCL", "INT"]),

    # ── Buttons: GPIO -> 330R -> switch -> 3V3, pin read with INPUT_PULLDOWN ──
    # Active-high, to match BTN_ACTIVE_HIGH=1 in config.h. The 330R is not a
    # pull-up; it limits current if the pin is ever driven LOW while the button
    # is held, which would otherwise short 3V3 straight into the pin.
    dict(ref="R1", lib="Device:R", value="330", at=(45.72, 132.08),
         nets={"1": "BTN_A", "2": "BTN_A_SW"}),
    dict(ref="SW1", lib="Switch:SW_Push", value="Button A",
         at=(45.72, 152.4), nets={"1": "BTN_A_SW", "2": "+3V3"}),

    dict(ref="R2", lib="Device:R", value="330", at=(76.2, 132.08),
         nets={"1": "BTN_B", "2": "BTN_B_SW"}),
    dict(ref="SW2", lib="Switch:SW_Push", value="Button B",
         at=(76.2, 152.4), nets={"1": "BTN_B_SW", "2": "+3V3"}),

    # ── Decoupling, one per module, mounted as close to its VCC pin as possible
    dict(ref="C1", lib="Device:C", value="100nF", at=(254, 55.88),
         nets={"1": "+3V3", "2": "GND"}),
    dict(ref="C2", lib="Device:C", value="100nF", at=(254, 99.06),
         nets={"1": "+3V3", "2": "GND"}),
    dict(ref="C3", lib="Device:C", value="100nF", at=(254, 142.24),
         nets={"1": "+3V3", "2": "GND"}),

    # ── Power chain ──────────────────────────────────────────────────────────
    dict(ref="BT1", lib="Device:Battery_Cell", value="Li-ion 3.7V 500-1200mAh",
         at=(40.64, 205.74), nets={"1": "BAT+", "2": "GND"}),

    dict(ref="U5", lib="fitness-watch:TP4056-Protected", value="TP4056 + DW01A/FS8205",
         at=(93.98, 203.2), nets={"BAT+": "BAT+", "BAT-": "GND",
                                  "OUT+": "VCELL", "OUT-": "GND"},
         nc=["IN+", "IN-"]),

    dict(ref="U6", lib="fitness-watch:MT3608-Boost", value="MT3608 boost - set to 5.1V",
         at=(162.56, 203.2), nets={"VIN+": "VCELL", "VIN-": "GND",
                                   "VOUT+": "VBOOST", "VOUT-": "GND"}),

    # Cathode (pin 1) is on the left = the +5V side; anode (pin 2) on the right.
    dict(ref="D1", lib="Device:D_Schottky", value="1N5819", at=(215.9, 203.2),
         nets={"1": "+5V", "2": "VBOOST"}),

    # ── Battery sense divider: halves the cell into ADC1 ─────────────────────
    # 100k/100k puts a full 4.2 V cell at 2.1 V, inside the 11 dB range, and
    # draws only 21 uA so it can stay connected permanently. Tapped from the
    # PROTECTED rail (OUT+), not BAT+, so it stops draining if protection trips.
    # The mid-node is drawn as real wire rather than three separate labels: a
    # divider is the one place where label-style connection hides the topology,
    # and this is the node a builder is most likely to get wrong. `None` means
    # "no stub, no label — wired explicitly below".
    dict(ref="R3", lib="Device:R", value="100k", at=(287.02, 187.96),
         nets={"1": "VCELL", "2": None}),
    dict(ref="R4", lib="Device:R", value="100k", at=(287.02, 213.36),
         nets={"1": None, "2": "GND"}),
    dict(ref="C4", lib="Device:C", value="100nF", at=(320.04, 213.36),
         nets={"1": None, "2": "GND"}),
]

# Explicit geometry for the sense node. Coordinates are pin points: R3 pin 2 and
# R4 pin 1 are 3.81 mm from their symbol centres, C4 pin 1 likewise.
SENSE_NODE = (287.02, 200.66)
EXTRA_WIRES = [
    ((287.02, 191.77), (287.02, 209.55)),   # R3 bottom straight down to R4 top
    ((320.04, 209.55), (320.04, 200.66)),   # C4 up to the node's height
    ((320.04, 200.66), (287.02, 200.66)),   # and across into the node
]
EXTRA_JUNCTIONS = [SENSE_NODE]
# Rotation 180 runs the text left, into empty sheet, instead of along the wire.
EXTRA_LABELS = [("VBAT_SENSE", SENSE_NODE, 180)]

# One power symbol per rail, plus a PWR_FLAG on the rails that have no power
# OUTPUT pin to drive them. +3V3 is already driven by U1's 3V3 pin (the
# SuperMini's on-board regulator), so flagging it too would be a second source
# on the same net and ERC reports the conflict.
#   flag=True  -> the rail's only sources are passive pins
RAILS = [("GND",  355.6, 76.2,   True),    # returns are passive, so nothing drives it
         ("+3V3", 355.6, 106.68, False),   # driven by U1.3V3 (power_out)
         ("+5V",  355.6, 137.16, True)]    # fed through D1, which is passive

NOTES = [
    (25.4, 30.48, "FitnessAI Watch - ESP32-C3 SuperMini + SSD1306 + MAX30102 + MPU6050"),
    (25.4, 35.56, "Connections are made by NET LABEL: identically-named labels are one net."),
    (25.4, 40.64, "Pin assignment is the source of truth in watch/firmware/fitness_watch/config.h."),
    (25.4, 45.72, "Generated by generate_schematic.py - edit the netlist there, not the sheet."),

    (144.78, 165.1, "I2C runs at 100 kHz. Pull-ups are already on the breakout modules"),
    (144.78, 170.18, "(~1.1k combined). Do not add more - see watch/README.md > Resistors."),

    (25.4, 175.26, "POWER: the battery runs the watch unless USB-C is plugged in."),
    (25.4, 180.34, "The SuperMini's 5V pad IS USB VBUS. USB present = 5.0V on that node; the"),
    (25.4, 185.42, "battery branch can only reach 5.1V - 0.35V (D1) = 4.75V, so D1 reverse-"),
    (25.4, 190.5,  "biases and the battery drops out. Unplug USB and it conducts again."),

    (25.4, 233.68, "TP4056 IN+/IN- = the charger module's OWN USB socket. Do NOT wire them to"),
    (25.4, 238.76, "the ESP32 5V pin: that makes a loop that charges the battery from itself"),
    (25.4, 243.84, "(boost -> 5V -> charger -> cell -> boost). It never terminates. See POWER.md."),
    (25.4, 254, "SET THE MT3608 TO 5.1V, WITH NOTHING ON ITS OUTPUT, BEFORE WIRING IT UP."),
    (25.4, 259.08, "These boards ship at an arbitrary setting and can put out up to 28V."),
]

DIR_TO_LABEL_ROT = {(1, 0): 0, (-1, 0): 180, (0, -1): 90, (0, 1): 270}


# ── emitters ─────────────────────────────────────────────────────────────────
def emit_wire(a, b, key):
    return ('\t(wire\n\t\t(pts\n\t\t\t(xy %s %s) (xy %s %s)\n\t\t)\n'
            '\t\t(stroke\n\t\t\t(width 0)\n\t\t\t(type default)\n\t\t)\n'
            '\t\t(uuid "%s")\n\t)' % (a[0], a[1], b[0], b[1], uid("w" + key)))


def emit_label(net, pt, rot, key):
    # No explicit justify: KiCad derives it from the rotation so the text always
    # runs AWAY from the wire's anchor. Forcing "left" makes labels on
    # right-to-left stubs (rotation 180) grow back over the symbol — which is
    # invisible for a short name like GND and unreadable for VBAT_SENSE.
    return ('\t(label "%s"\n\t\t(at %s %s %s)\n'
            '\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n'
            '\t\t)\n\t\t(uuid "%s")\n\t)'
            % (net, pt[0], pt[1], rot, uid("l" + key)))


def emit_junction(pt, key):
    return ('	(junction
		(at %s %s)
		(diameter 0)
'
            '		(color 0 0 0 0)
		(uuid "%s")
	)'
            % (pt[0], pt[1], uid("j" + key)))


def emit_nc(pt, key):
    return '\t(no_connect\n\t\t(at %s %s)\n\t\t(uuid "%s")\n\t)' % (pt[0], pt[1], uid("nc" + key))


def emit_text(pt, s, key):
    return ('\t(text "%s"\n\t\t(exclude_from_sim no)\n\t\t(at %s %s 0)\n'
            '\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n'
            '\t\t\t(justify left bottom)\n\t\t)\n\t\t(uuid "%s")\n\t)'
            % (s, pt[0], pt[1], uid("t" + key)))


def emit_symbol(ref, lib, value, at, rot, pin_numbers, key, bbox=None):
    hidden = ref.startswith("#")
    ref_at, val_at = property_anchors(bbox or (-1.27, -1.27, 1.27, 1.27), at)
    props = [("Reference", ref, ref_at[0], ref_at[1], hidden),
             ("Value", value, val_at[0], val_at[1], hidden),
             ("Footprint", "", at[0], at[1], True),
             ("Datasheet", "", at[0], at[1], True),
             ("Description", "", at[0], at[1], True)]
    out = ['\t(symbol\n\t\t(lib_id "%s")\n\t\t(at %s %s %s)\n'
           '\t\t(unit 1)\n\t\t(exclude_from_sim no)\n\t\t(in_bom yes)\n'
           '\t\t(on_board yes)\n\t\t(dnp no)\n\t\t(uuid "%s")'
           % (lib, at[0], at[1], rot, uid("s" + key))]
    for name, val, px, py, hide in props:
        h = '\n\t\t\t(hide yes)' if hide else ''
        out.append('\t\t(property "%s" "%s"\n\t\t\t(at %s %s 0)%s\n'
                   '\t\t\t(effects\n\t\t\t\t(font\n\t\t\t\t\t(size 1.27 1.27)\n'
                   '\t\t\t\t)\n\t\t\t\t(justify left)\n\t\t\t)\n\t\t)'
                   % (name, val, px, py, h))
    for num in pin_numbers:
        out.append('\t\t(pin "%s"\n\t\t\t(uuid "%s")\n\t\t)' % (num, uid("p" + key + num)))
    out.append('\t\t(instances\n\t\t\t(project "%s"\n'
               '\t\t\t\t(path "/%s"\n\t\t\t\t\t(reference "%s")\n'
               '\t\t\t\t\t(unit 1)\n\t\t\t\t)\n\t\t\t)\n\t\t)' % (PROJECT, SHEET_UUID, ref))
    out.append("\t)")
    return "\n".join(out)


def write_symbol_library() -> None:
    """Write fitness-watch.kicad_sym and register it in the project.

    The sheet already embeds a copy of every symbol, so the schematic opens and
    ERCs fine without this. But KiCad then warns that the 'fitness-watch'
    library is not in the configuration, and the symbols cannot be edited or
    reused — they exist only as a frozen copy inside the sheet. Shipping a real
    library plus a sym-lib-table makes them first-class parts.
    """
    lib_path = HERE / "fitness-watch.kicad_sym"
    syms = []
    for name, spec in MODULES.items():
        block = build_module_symbol(name, spec, prefixed=False)
        # The sheet nests symbols two levels deep (inside lib_symbols); a
        # standalone library nests them one.
        syms.append("\n".join(ln[1:] if ln.startswith("\t") else ln
                              for ln in block.splitlines()))
    # A symbol library carries its OWN format version, which is not the
    # schematic's. Using the sheet's 20260306 here makes KiCad reject the whole
    # file with a bare "Unable to load library" and fall back to the copies
    # embedded in the sheet — which still ERCs clean, so the breakage is easy
    # to miss. This value matches the stock KiCad 10 libraries.
    lib_path.write_text(
        '(kicad_symbol_lib\n\t(version 20251024)\n'
        '\t(generator "kicad_symbol_editor")\n'
        '\t(generator_version "10.0")\n' + "\n".join(syms) + "\n)\n",
        encoding="utf-8")

    table = HERE / "sym-lib-table"
    if not table.exists():
        table.write_text(
            '(sym_lib_table\n\t(version 7)\n'
            '\t(lib (name "fitness-watch")(type "KiCad")'
            '(uri "${KIPRJMOD}/fitness-watch.kicad_sym")(options "")'
            '(descr "FitnessAI Watch breakout modules"))\n)\n',
            encoding="utf-8")
    print("wrote %s and sym-lib-table" % lib_path.name)


# ── main ─────────────────────────────────────────────────────────────────────
def main() -> int:
    lib_blocks, pin_geom, bboxes = {}, {}, {}

    for name, spec in MODULES.items():
        lib_id = "fitness-watch:" + name
        block = build_module_symbol(name, spec)
        lib_blocks[lib_id] = block
        pin_geom[lib_id] = parse_pins(block)
        bboxes[lib_id] = symbol_bbox(block)

    stock = sorted({p["lib"] for p in PLACEMENTS if not p["lib"].startswith("fitness-watch:")})
    stock += ["power:GND", "power:+3V3", "power:+5V", "power:PWR_FLAG"]
    for lib_id in stock:
        if lib_id in lib_blocks:
            continue
        block = stock_symbol(lib_id)
        lib_blocks[lib_id] = block
        pin_geom[lib_id] = parse_pins(block)
        bboxes[lib_id] = symbol_bbox(block)

    body, nets_seen = [], {}

    for place in PLACEMENTS:
        ref, lib_id, at = place["ref"], place["lib"], place["at"]
        geom = pin_geom[lib_id]
        by_name = {p["name"]: num for num, p in geom.items()}

        body.append(emit_symbol(ref, lib_id, place["value"], at, 0,
                                sorted(geom, key=lambda n: int(n) if n.isdigit() else 0),
                                ref, bboxes[lib_id]))

        for key, net in place.get("nets", {}).items():
            if net is None:
                continue          # wired explicitly via EXTRA_WIRES
            num = key if key in geom else by_name.get(key)
            if num is None:
                sys.exit("error: %s has no pin %r (has %s)"
                         % (ref, key, sorted(by_name) or sorted(geom)))
            p = geom[num]
            start = pin_point(at, p)
            d = outward(p)
            end = (round(start[0] + d[0] * STUB, 3), round(start[1] + d[1] * STUB, 3))
            body.append(emit_wire(start, end, ref + num))
            body.append(emit_label(net, end, DIR_TO_LABEL_ROT[d], ref + num))
            nets_seen.setdefault(net, []).append("%s.%s" % (ref, p["name"] or num))

        for key in place.get("nc", []):
            num = key if key in geom else by_name.get(key)
            if num is None:
                sys.exit("error: %s has no pin %r for no-connect" % (ref, key))
            body.append(emit_nc(pin_point(at, geom[num]), ref + num))

    # Power rails: symbol at the top of a short wire, PWR_FLAG at the bottom.
    # GND draws downward and PWR_FLAG upward, so GND needs no rotation while the
    # positive rails need the flag turned 180 to point away from the wire.
    for i, (a, b) in enumerate(EXTRA_WIRES):
        body.append(emit_wire(a, b, "x%d" % i))
    for i, pt in enumerate(EXTRA_JUNCTIONS):
        body.append(emit_junction(pt, "x%d" % i))
    for i, (net, pt, rot) in enumerate(EXTRA_LABELS):
        body.append(emit_label(net, pt, rot, "x%d" % i))
        nets_seen.setdefault(net, []).append("wired-node")

    # References must end in a NUMBER or KiCad treats the symbol as unannotated
    # and `sch export netlist` warns. "#PWR_GND" looks readable but fails that
    # rule, so the rail symbols get conventional #PWR0n / #FLG0n names.
    for idx, (net, x, y, flag) in enumerate(RAILS, start=1):
        sym_at = (x, y)
        body.append(emit_symbol("#PWR%02d" % idx, "power:" + net, net, sym_at, 0,
                                ["1"], "pwr" + net, bboxes["power:" + net]))

        # Every rail symbol needs a wire on its pin. A power symbol standing on
        # its own is still an unconnected pin as far as ERC is concerned, even
        # though its name already defines the net.
        # GND's pin leaves upward, so its stub runs up; +3V3/+5V leave downward.
        end = (x, y - 7.62) if net == "GND" else (x, y + 7.62)
        body.append(emit_wire(sym_at, end, "rail" + net))

        if flag:
            # PWR_FLAG draws upward, GND downward, so on GND the two point away
            # from each other unrotated; on a positive rail the flag needs 180.
            body.append(emit_symbol("#FLG%02d" % idx, "power:PWR_FLAG", "PWR_FLAG",
                                    end, 0 if net == "GND" else 180,
                                    ["1"], "flg" + net, bboxes["power:PWR_FLAG"]))
        else:
            # No flag wanted (the rail already has a real power_out driving it),
            # so terminate the stub with a label instead.
            body.append(emit_label(net, end, 270, "rail" + net))

    for i, (x, y, s) in enumerate(NOTES):
        body.append(emit_text((x, y), s, "n%d" % i))

    doc = ['(kicad_sch',
           '\t(version 20260306)',
           '\t(generator "eeschema")',
           '\t(generator_version "10.0")',
           '\t(uuid "%s")' % SHEET_UUID,
           '\t(paper "A3")',
           '\t(lib_symbols']
    doc += [lib_blocks[k] for k in sorted(lib_blocks)]
    doc.append('\t)')
    doc += body
    doc.append('\t(sheet_instances\n\t\t(path "/"\n\t\t\t(page "1")\n\t\t)\n\t)')
    doc.append('\t(embedded_fonts no)')
    doc.append(')')

    OUT.write_text("\n".join(doc) + "\n", encoding="utf-8")
    write_symbol_library()

    print("wrote %s" % OUT.name)
    print("\nnets:")
    for net in sorted(nets_seen):
        pins = nets_seen[net]
        flag = "  <-- SINGLE PIN" if len(pins) < 2 else ""
        print("  %-12s %d  %s%s" % (net, len(pins), ", ".join(pins), flag))
    return 0


if __name__ == "__main__":
    sys.exit(main())
