#include "Console.h"

void printSeparator() {
  Serial.println(F("--------------------------------------------------"));
}

void printTitle(const char* title) {
  Serial.println();
  Serial.println(F("=================================================="));
  Serial.print(F(" "));
  Serial.println(title);
  Serial.println(F("=================================================="));
}

void printHexByte(const char* label, uint8_t value) {
  Serial.print(label);
  Serial.print(F(" = 0x"));
  if (value < 0x10) Serial.print('0');
  Serial.print(value, HEX);
  Serial.print(F("  (bin: "));
  for (int i = 7; i >= 0; i--) Serial.print((value >> i) & 1);
  Serial.println(')');
}
