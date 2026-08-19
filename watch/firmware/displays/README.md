# Display modules

One directory per physical panel the watch firmware has been run on. Each holds
that panel's `display_config.h` — geometry, I2C address, init quirks and the
wiring that module specifically needs.

| Directory | Panel | Bus / power | Address | Status |
|---|---|---|---|---|
| `ssd1306_oled/` | SSD1306 128×64 OLED | I2C, **3V3** | `0x3C` | Verified — the panel the firmware was developed against |
| `hd44780_1602/` | 16×2 character LCD + PCF8574 | I2C, **5V** | `0x27`/`0x3F` (probed) | Driver verified via the 2004A; geometry untested on hardware |
| `hd44780_2004a/` | 20×4 character LCD + PCF8574 | I2C, **5V** | `0x27` (found) | **Current build.** Verified on hardware, all four rows drawing |

## Choosing one

Exactly one line in `../fitness_watch/config.h`:

```c
#define DISPLAY_MODULE   DISPLAY_HD44780_2004A
```

That pulls in the matching `display_config.h`. Nothing else in `config.h` is
panel-specific, and no file in the sketch needs editing.

## Why the two HD44780 panels share a driver

They are the same controller behind the same PCF8574 backpack; only the row and
column count differ. Every LCD screen in the sketch is written with a two-row
layout and a four-row layout and picks between them on `LCD_ROWS`, so a 1602 and
a 2004A run identical code — `{16,2}` versus `{20,4}` is the whole difference.

They are still separate directories because what you need to *know* per module
is not the same: verified addresses, which rail it wants, and the gotchas that
cost real debugging time all differ. Duplicating the ~3,300 lines of shared
firmware per panel would mean three copies to keep in step, which is how display
bug fixes end up applied to one panel and not the others.

## Other display builds

- `../uno_display/` — Arduino Uno + LCD1602, a standalone fallback. Separate
  because it is a different *board*, not just a different panel: no radio, 32 KB
  of flash, and I2C fixed at A4/A5.
- `../screen_measure/` — panel identification sketch, for working out the driver
  and geometry of an unknown module.

## Adding a panel

1. `mkdir displays/<module>/` and write a `display_config.h` that defines
   `DISPLAY_TYPE` (which driver) plus that panel's constants.
2. Add a `DISPLAY_<MODULE>` id and an `#elif` branch in `config.h`.
3. If it needs a driver the sketch does not have, add the `#if DISPLAY_TYPE ==`
   branches alongside the existing OLED and HD44780 ones.
