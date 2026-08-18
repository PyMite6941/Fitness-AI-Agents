/*
 * I2C DEFINITIVE finder for the FitnessAI Watch.
 *
 * Sweeps every ordered GPIO pair as (SDA, SCL) and reports EVERY device that
 * answers, with PHANTOM REJECTION: a bogus address (0x4F) must NOT ack on the
 * same pair, or the "hits" are floating-line artifacts rather than chips.
 *
 * Two things the first version got wrong, both of which produced a misleading
 * "the LCD is not on the bus" verdict:
 *   1. It stopped at the FIRST hit (`found = true; break;`), so on a bus with an
 *      MPU6050 it reported 0x68 and never scanned another pin pair — most of the
 *      pin space was silently never tested.
 *   2. It rejected phantoms per-address, so a pair where every address acks
 *      (SDA stuck LOW) looked like "nothing found" rather than "miswired".
 *
 * Flash with CDCOnBoot=cdc, then open the serial monitor at 115200.
 * Runs ONE full sweep, prints a summary, then idles — reset to sweep again.
 */
#include <Wire.h>

// Every GPIO broken out on the ESP32-C3 SuperMini. 11-17 are the internal SPI
// flash lines and are not on the header, so they are deliberately absent.
const int PINS[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 18, 19, 20, 21};
const int N = sizeof(PINS) / sizeof(PINS[0]);

const uint8_t PHANTOM_ADDR = 0x4F;   // nothing in this build lives here

bool ack(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

const char *describe(uint8_t addr) {
  switch (addr) {
    case 0x3C: case 0x3D: return "SSD1306 OLED";
    case 0x68: return "MPU6050";
    case 0x57: return "MAX30102/05";
    default:
      if ((addr >= 0x20 && addr <= 0x27) || (addr >= 0x38 && addr <= 0x3F))
        return "PCF8574 (LCD1602 backpack)";
      return "unknown";
  }
}

// Scan one pin pair. Returns the number of real devices reported.
int scanPair(int sda, int scl) {
  Wire.end();
  Wire.begin(sda, scl);
  Wire.setClock(100000);
  delay(6);

  // Count everything first. An ack from the phantom address, or a wildly high
  // count, means the lines are not being driven by real silicon.
  int hits = 0;
  uint8_t seen[16];
  int nseen = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
    if (!ack(addr)) continue;
    hits++;
    if (nseen < (int)sizeof(seen)) seen[nseen++] = addr;
  }
  if (hits == 0) return 0;

  if (ack(PHANTOM_ADDR) || hits > 8) {
    Serial.printf("  SDA=%-2d SCL=%-2d : %d phantom acks (line stuck/floating) - ignored\n",
                  sda, scl, hits);
    return 0;
  }

  for (int i = 0; i < nseen; i++)
    Serial.printf("  REAL: 0x%02X (%s)  SDA=%d SCL=%d\n",
                  seen[i], describe(seen[i]), sda, scl);
  return nseen;
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== I2C DEFINITIVE finder ===");
  Serial.printf("sweeping %d GPIOs = %d ordered (SDA,SCL) pairs, addr 0x08-0x77\n",
                N, N * (N - 1));

  int total = 0;
  for (int i = 0; i < N; i++)
    for (int j = 0; j < N; j++) {
      if (i == j) continue;
      total += scanPair(PINS[i], PINS[j]);
    }

  Serial.println("=== sweep complete ===");
  if (total == 0) {
    Serial.println("No real device answered on ANY pin pair.");
    Serial.println("That is a POWER or GROUND fault, not a pin-choice problem:");
    Serial.println("  - a module with no VCC is invisible on I2C;");
    Serial.println("  - a 5 V HD44780/PCF8574 backpack fed 3V3 behaves the same way.");
    Serial.println("Check VCC (5V for the LCD backpack), a shared GND, then re-run.");
  } else {
    Serial.printf("%d device(s) reported above. Put the winning pair into\n", total);
    Serial.println("config.h as PIN_I2C_SDA / PIN_I2C_SCL.");
  }
}

void loop() { delay(5000); }
