/*
 * I2C all-pin finder + LATCH. Reports a genuine OLED on any GPIO pair, and
 * remembers the winning pins so a brief/intermittent contact is still captured.
 * Flash with CDCOnBoot=cdc.
 */
#include <Wire.h>

const int PINS[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
const int N = sizeof(PINS) / sizeof(PINS[0]);

int lastSda = -1, lastScl = -1;
uint8_t lastAddr = 0;
uint32_t hitCount = 0;

bool ack(int sda, int scl, uint8_t addr) {
  Wire.end(); Wire.begin(sda, scl); delay(6);
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}
bool realHit(int sda, int scl, uint8_t addr) {
  return ack(sda, scl, addr) && !ack(sda, scl, 0x4F);   // reject phantom (bogus addr must NOT ack)
}

void setup() { Serial.begin(115200); delay(1200); Serial.println("\n=== OLED finder + latch ==="); }

void loop() {
  bool nowOk = false;
  for (int i = 0; i < N && !nowOk; i++)
    for (int j = 0; j < N && !nowOk; j++) {
      if (i == j) continue;
      for (uint8_t addr = 0x3C; addr <= 0x3D; addr++)
        if (realHit(PINS[i], PINS[j], addr)) {
          lastSda = PINS[i]; lastScl = PINS[j]; lastAddr = addr; hitCount++;
          nowOk = true; break;
        }
    }
  if (nowOk)
    Serial.printf("LIVE NOW: 0x%02X SDA=%d SCL=%d\n", lastAddr, lastSda, lastScl);
  else if (lastSda >= 0)
    Serial.printf("dropped (intermittent). last good: 0x%02X SDA=%d SCL=%d  hits=%lu\n",
                  lastAddr, lastSda, lastScl, hitCount);
  else
    Serial.println("nothing real yet...");
  delay(1200);
}
