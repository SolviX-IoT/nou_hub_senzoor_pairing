#include "SpiBus.h"

namespace SpiBus {

  static bool  s_started = false;
  static Owner s_owner = Owner::None;

  Owner owner() { return s_owner; }

#if SPI_BUS_ASSERT
  /*
   * Se plange o singura data per tranzitie gresita: daca greseala este
   * intr-o bucla, un mesaj per iteratie ar ineca Serial-ul si ar deveni
   * el insusi o problema de temporizare.
   */
  static void assertFreeBus(Owner wanted, const __FlashStringHelper* who) {
    static Owner lastComplaint = Owner::None;

    if (s_owner != Owner::None && s_owner != wanted) {
      if (lastComplaint != s_owner) {
        lastComplaint = s_owner;
        Serial.print(F("[SPI] ATENTIE: "));
        Serial.print(who);
        Serial.print(F(" cere magistrala, dar "));
        Serial.print(s_owner == Owner::Ethernet ? F("Ethernet") : F("LoRa"));
        Serial.println(F(" nu a eliberat-o."));
        Serial.println(F("      Undeva lipseste un SpiBus::deselectAll() pe o cale de eroare."));
      }
    } else {
      lastComplaint = Owner::None;
    }
  }
#endif

  void deselectAll() {
    digitalWrite(PIN_ETH_CS,   HIGH);
    digitalWrite(PIN_LORA_NSS, HIGH);
    s_owner = Owner::None;
  }

  void begin() {
    if (s_started) return;

    // Pas 1: CS-urile devin iesiri si urca pe HIGH INAINTE de orice
    // trafic. Cat timp sunt flotante, un modul se poate crede selectat.
    pinMode(PIN_ETH_CS,   OUTPUT);
    pinMode(PIN_LORA_NSS, OUTPUT);
    deselectAll();

    // Pas 2: liniile de reset ale ambelor module, tinute inactive (HIGH).
    pinMode(PIN_ETH_RESET, OUTPUT);
    digitalWrite(PIN_ETH_RESET, HIGH);
    pinMode(PIN_LORA_RST, OUTPUT);
    digitalWrite(PIN_LORA_RST, HIGH);

    // Pas 3: magistrala propriu-zisa. Ultimul parametru este -1 intentionat:
    // nu lasam driverul ESP32 sa preia niciun pin drept CS hardware, fiindca
    // avem doua module si controlam ambele CS-uri manual.
    SPI.begin(PIN_SPI_SCK, PIN_SPI_MISO, PIN_SPI_MOSI, -1);

    s_started = true;
  }

  void claimEthernet() {
#if SPI_BUS_ASSERT
    assertFreeBus(Owner::Ethernet, F("Ethernet"));
#endif
    digitalWrite(PIN_LORA_NSS, HIGH);  // LoRa iese de pe MISO
    digitalWrite(PIN_ETH_CS,   HIGH);  // libraria coboara ea CS-ul cand vrea
    s_owner = Owner::Ethernet;
  }

  void claimLoRa() {
#if SPI_BUS_ASSERT
    assertFreeBus(Owner::LoRa, F("LoRa"));
#endif
    digitalWrite(PIN_ETH_CS,   HIGH);  // Ethernet iese de pe MISO
    digitalWrite(PIN_LORA_NSS, HIGH);
    s_owner = Owner::LoRa;
  }

  void resetEthernetModule() {
    deselectAll();
    digitalWrite(PIN_ETH_RESET, HIGH);
    delay(10);
    digitalWrite(PIN_ETH_RESET, LOW);   // reset activ pe LOW
    delay(10);
    digitalWrite(PIN_ETH_RESET, HIGH);
    delay(50);                          // asteptam stabilizarea oscilatorului
  }

  void resetLoRaModule() {
    deselectAll();
    digitalWrite(PIN_LORA_RST, HIGH);
    delay(10);
    digitalWrite(PIN_LORA_RST, LOW);
    delay(10);
    digitalWrite(PIN_LORA_RST, HIGH);
    delay(50);
  }
}
