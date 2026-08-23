#include "TestLoRaTx.h"

namespace TestLoRaTx {

  static const unsigned long INTERVAL_MS = 2000;

  static unsigned long s_lastSend = 0;
  static int s_counter = 0;
  static bool s_ready = false;

  bool begin() {
    printTitle("TEST LoRa - EMISIE");
    Serial.println(F("Modulul Ethernet ramane deselectat pe toata durata testului."));
    printSeparator();

    s_ready = LoRaRadio::begin();
    s_counter = 0;
    s_lastSend = 0;

    if (s_ready) {
      Serial.println(F("Modul LoRa initializat. Incep emisia..."));
    }
    return s_ready;
  }

  void tick() {
    if (!s_ready) return;

    if (s_lastSend != 0 && millis() - s_lastSend < INTERVAL_MS) {
      delay(10);
      return;
    }
    s_lastSend = millis();

    String payload = "Test pachet nr " + String(s_counter);
    bool ok = LoRaRadio::sendText(payload);

    Serial.print(F("Trimit pachet #"));
    Serial.print(s_counter);
    Serial.println(ok ? F("  -> OK") : F("  -> ESEC la emisie"));

    s_counter++;
  }

  void stop() {
    LoRaRadio::sleep();
    Serial.println(F("Emisie LoRa oprita (radio in sleep, SPI ramane pornit)."));
  }
}
