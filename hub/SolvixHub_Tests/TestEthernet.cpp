#include "TestEthernet.h"

namespace TestEthernet {

  static const char* HOST = "google.com";
  static const unsigned long INTERVAL_MS = 10000;

  static unsigned long s_lastPing = 0;
  static bool s_linkUp = false;

  bool begin() {
    printTitle("TEST 2/3 - RETEA SI INTERNET (ENC28J60)");
    Serial.println(F("Modulul LoRa ramane deselectat pe toata durata testului."));
    printSeparator();

    s_linkUp = EthernetLink::begin();
    s_lastPing = 0;

    if (!s_linkUp) {
      Serial.println(F("Testul nu poate continua fara adresa IP."));
      return false;
    }

    Serial.println();
    return true;
  }

  void tick() {
    if (!s_linkUp) return;

    EthernetLink::maintain();

    if (s_lastPing != 0 && millis() - s_lastPing < INTERVAL_MS) {
      delay(10);
      return;
    }
    s_lastPing = millis();

    printSeparator();
    EthernetLink::httpPing(HOST);
    Serial.println(F("Urmatoarea incercare in 10 secunde."));
  }

  void stop() {
    SpiBus::deselectAll();
    Serial.println(F("Test Ethernet oprit, CS_ETH pe HIGH."));
  }
}
