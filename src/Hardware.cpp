#include "Hardware.h"
#include "Config.h"

TwoWire Wire1(PB9, PB6); // SDA, SCL สำหรับ SHT40 + EEPROM

void initBoard()
{
  pinMode(kStatusLedPin, OUTPUT);
  pinMode(kLatchTrigPin, OUTPUT);
  pinMode(kLatchUnlockPin, INPUT);
  digitalWrite(kStatusLedPin, LOW);

  Serial.setRx(kDebugSerialRxPin);
  Serial.setTx(kDebugSerialTxPin);
  Serial.begin(kDebugSerialBaud);
}
