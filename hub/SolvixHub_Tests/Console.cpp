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
