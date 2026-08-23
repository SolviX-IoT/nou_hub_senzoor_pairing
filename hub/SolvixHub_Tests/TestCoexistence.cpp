#include "TestCoexistence.h"

namespace TestCoexistence {

  static const char* HOST = "google.com";
  static const unsigned long LORA_INTERVAL_MS = 3000;
  static const unsigned long HTTP_INTERVAL_MS = 15000;

  static unsigned long s_lastLoRa = 0;
  static unsigned long s_lastHttp = 0;

  static int  s_loraCounter = 0;
  static unsigned long s_loraOk = 0, s_loraFail = 0;
  static unsigned long s_httpOk = 0, s_httpFail = 0;

  static bool s_loraReady = false;
  static bool s_ethReady = false;

  bool begin() {
    printTitle("TEST COEXISTENTA - LoRa + Ethernet pe acelasi SPI");
    Serial.println(F("Ambele module folosesc SCK 18 / MISO 19 / MOSI 23."));
    Serial.println(F("Se despart doar prin chip select: CS_ETH = 4, NSS_LoRa = 5."));
    printSeparator();

    // Pasul 1: LoRa. Radioul are nevoie de o secventa de reset curata,
    // deci il initializam inainte sa apara trafic Ethernet pe bus.
    Serial.println(F("[1/2] Initializez LoRa..."));
    s_loraReady = LoRaRadio::begin();
    Serial.println(s_loraReady ? F("      LoRa: OK") : F("      LoRa: ESEC"));

    // Pasul 2: Ethernet. LoRa este deja deselectat de LoRaRadio::begin().
    Serial.println(F("[2/2] Initializez Ethernet (DHCP)..."));
    s_ethReady = EthernetLink::begin();
    Serial.println(s_ethReady ? F("      Ethernet: OK") : F("      Ethernet: ESEC"));

    printSeparator();
    if (!s_loraReady && !s_ethReady) {
      Serial.println(F("Niciun modul nu a pornit. Ruleaza intai testele individuale."));
      return false;
    }
    if (!s_loraReady || !s_ethReady) {
      Serial.println(F("Doar un modul a pornit; testul continua cu el, dar"));
      Serial.println(F("coexistenta nu poate fi verificata cu adevarat."));
    }

    s_lastLoRa = 0;
    s_lastHttp = millis();   // prima cerere HTTP abia peste 15 s
    s_loraCounter = 0;
    s_loraOk = s_loraFail = s_httpOk = s_httpFail = 0;
    Serial.println();
    return true;
  }

  static void printCounters() {
    Serial.print(F("   [contoare] LoRa OK/ESEC: "));
    Serial.print(s_loraOk); Serial.print('/'); Serial.print(s_loraFail);
    Serial.print(F("   HTTP OK/ESEC: "));
    Serial.print(s_httpOk); Serial.print('/'); Serial.println(s_httpFail);
  }

  void tick() {
    // --- receptie LoRa, prin polling (niciodata din intrerupere) ---
    if (s_loraReady) {
      String payload;
      int rssi = 0;
      float snr = 0.0f;
      if (LoRaRadio::receive(payload, rssi, snr)) {
        Serial.print(F("<< LoRa RX: \""));
        Serial.print(payload);
        Serial.print(F("\"  RSSI "));
        Serial.print(rssi);
        Serial.println(F(" dBm"));
      }
    }

    // --- emisie LoRa ---
    if (s_loraReady && (s_lastLoRa == 0 || millis() - s_lastLoRa >= LORA_INTERVAL_MS)) {
      s_lastLoRa = millis();
      String payload = "hub coexist #" + String(s_loraCounter++);
      if (LoRaRadio::sendText(payload)) {
        s_loraOk++;
        Serial.print(F(">> LoRa TX: "));
        Serial.println(payload);
      } else {
        s_loraFail++;
        Serial.println(F(">> LoRa TX: ESEC"));
      }
    }

    // --- trafic Ethernet ---
    if (s_ethReady) {
      EthernetLink::maintain();

      if (millis() - s_lastHttp >= HTTP_INTERVAL_MS) {
        s_lastHttp = millis();
        Serial.println();
        Serial.println(F("== Cerere HTTP in timp ce LoRa este activ =="));
        if (EthernetLink::httpPing(HOST)) {
          s_httpOk++;
        } else {
          s_httpFail++;
        }
        printCounters();
        Serial.println();

        // Dupa un transfer Ethernet lung, ne asiguram ca bus-ul e liber.
        SpiBus::deselectAll();
      }
    }

    delay(5);
  }

  void stop() {
    LoRaRadio::sleep();
    SpiBus::deselectAll();
    Serial.println(F("Test coexistenta oprit."));
    printCounters();
  }
}
