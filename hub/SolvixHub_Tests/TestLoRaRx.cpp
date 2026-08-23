#include "TestLoRaRx.h"

namespace TestLoRaRx {

  static bool s_ready = false;
  static unsigned long s_received = 0;
  static unsigned long s_lastHeartbeat = 0;

  bool begin() {
    printTitle("TEST LoRa - RECEPTIE");
    Serial.println(F("Astept pachete. Ruleaza testul de emisie pe cealalta placa."));
    printSeparator();

    s_ready = LoRaRadio::begin();
    s_received = 0;
    s_lastHeartbeat = millis();
    return s_ready;
  }

  void tick() {
    if (!s_ready) return;

    String payload;
    int rssi = 0;
    float snr = 0.0f;

    if (LoRaRadio::receive(payload, rssi, snr)) {
      s_received++;
      Serial.print(F("Pachet primit: \""));
      Serial.print(payload);
      Serial.print(F("\"  RSSI: "));
      Serial.print(rssi);
      Serial.print(F(" dBm  SNR: "));
      Serial.print(snr, 1);
      Serial.print(F(" dB  (total: "));
      Serial.print(s_received);
      Serial.println(F(")"));
      s_lastHeartbeat = millis();
    } else if (millis() - s_lastHeartbeat >= 5000) {
      s_lastHeartbeat = millis();
      Serial.println(F("...niciun pachet in ultimele 5 secunde."));
    }

    delay(5);
  }

  void stop() {
    LoRaRadio::sleep();
    Serial.println(F("Receptie LoRa oprita."));
  }
}
