#include "TestButtons.h"

namespace TestButtons {

  static int s_last1 = -1;
  static int s_last2 = -1;
  static unsigned long s_changes1 = 0;
  static unsigned long s_changes2 = 0;
  static unsigned long s_lastPrint = 0;

  bool begin() {
    printTitle("TEST BUTOANE (GPIO34 / GPIO35)");
    Serial.println(F("ATENTIE: GPIO34 si GPIO35 nu au pull-up/pull-down intern."));
    Serial.println(F("Daca numarul de schimbari creste continuu fara sa atingi"));
    Serial.println(F("butoanele, linia este flotanta: lipseste rezistorul extern."));
    printSeparator();

    pinMode(PIN_BUTTON_1, INPUT);
    pinMode(PIN_BUTTON_2, INPUT);

    s_last1 = -1;
    s_last2 = -1;
    s_changes1 = 0;
    s_changes2 = 0;
    s_lastPrint = 0;
    return true;
  }

  void tick() {
    int state1 = digitalRead(PIN_BUTTON_1);
    int state2 = digitalRead(PIN_BUTTON_2);

    if (s_last1 != -1 && state1 != s_last1) s_changes1++;
    if (s_last2 != -1 && state2 != s_last2) s_changes2++;
    s_last1 = state1;
    s_last2 = state2;

    // Citim des (ca sa prindem tranzitiile), dar afisam rar.
    if (millis() - s_lastPrint >= 250) {
      s_lastPrint = millis();
      Serial.print(F("Buton 1 (GPIO34): "));
      Serial.print(state1);
      Serial.print(F(" [schimbari: "));
      Serial.print(s_changes1);
      Serial.print(F("]   |   Buton 2 (GPIO35): "));
      Serial.print(state2);
      Serial.print(F(" [schimbari: "));
      Serial.print(s_changes2);
      Serial.println(F("]"));
    }

    delay(5);
  }

  void stop() {
    Serial.println(F("Test butoane oprit."));
  }
}
