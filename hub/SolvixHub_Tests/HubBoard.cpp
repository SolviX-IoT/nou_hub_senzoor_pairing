/*
  HubBoard.cpp - placa: magistrala SPI, LED-urile si afisarea pe Serial.
  ---------------------------------------------------------------------
  Module, in ordinea dependentelor, fiecare in namespace-ul lui:
    SpiBus   arbitrajul magistralei SPI partajate de ENC28J60 si LoRa
    Leds     cele doua LED-uri, fara delay()
    Console  printSeparator() / printTitle() - functii libere, fara namespace
*/

#include "HubBoard.h"

// =====================================================================
//  SpiBus
// =====================================================================

namespace SpiBus {

  static bool s_started = false;

  void deselectAll() {
    digitalWrite(PIN_ETH_CS,   HIGH);
    digitalWrite(PIN_LORA_NSS, HIGH);
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
    digitalWrite(PIN_LORA_NSS, HIGH);  // LoRa iese de pe MISO
    digitalWrite(PIN_ETH_CS,   HIGH);  // libraria coboara ea CS-ul cand vrea
  }

  void claimLoRa() {
    digitalWrite(PIN_ETH_CS,   HIGH);  // Ethernet iese de pe MISO
    digitalWrite(PIN_LORA_NSS, HIGH);
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


// =====================================================================
//  Leds
// =====================================================================

namespace Leds {

  static bool s_started = false;

  // Momentul la care trebuie stins fiecare LED; 0 = niciun puls in curs.
  static unsigned long s_offAt1 = 0;
  static unsigned long s_offAt2 = 0;

  // Scoate referinta la contorul potrivit pinului, ca set/pulse/service
  // sa nu repete un if peste tot. Intoarce nullptr pentru un pin strain.
  static unsigned long* deadlineFor(uint8_t pin) {
    if (pin == PIN_LED_1) return &s_offAt1;
    if (pin == PIN_LED_2) return &s_offAt2;
    return nullptr;
  }

  void begin() {
    if (s_started) return;

    pinMode(PIN_LED_1, OUTPUT);
    pinMode(PIN_LED_2, OUTPUT);
    allOff();

    s_started = true;
  }

  void set(uint8_t pin, bool on) {
    // LED_ON_LEVEL este HIGH sau LOW, dupa cum sunt cablate LED-urile.
    digitalWrite(pin, on ? LED_ON_LEVEL : !LED_ON_LEVEL);

    // Un nivel continuu are prioritate fata de un puls ramas in coada:
    // altfel service() ar stinge LED-ul imediat dupa un set(true).
    unsigned long* deadline = deadlineFor(pin);
    if (deadline != nullptr) *deadline = 0;
  }

  void pulse(uint8_t pin) {
    unsigned long* deadline = deadlineFor(pin);
    if (deadline == nullptr) return;

    digitalWrite(pin, LED_ON_LEVEL);

    // millis() + durata poate fi 0 exact la depasirea contorului, iar 0
    // este marcajul de "niciun puls". Valoarea 1 pierde o milisecunda o
    // data la 49 de zile si scapa de cazul special.
    unsigned long due = millis() + LED_PULSE_MS;
    *deadline = (due == 0) ? 1 : due;
  }

  void service() {
    // Scaderea in aritmetica fara semn si comparatia cu semn trec corect
    // peste depasirea lui millis(), la ~49 de zile de functionare.
    if (s_offAt1 != 0 && (long)(millis() - s_offAt1) >= 0) {
      digitalWrite(PIN_LED_1, !LED_ON_LEVEL);
      s_offAt1 = 0;
    }
    if (s_offAt2 != 0 && (long)(millis() - s_offAt2) >= 0) {
      digitalWrite(PIN_LED_2, !LED_ON_LEVEL);
      s_offAt2 = 0;
    }
  }

  void allOff() {
    digitalWrite(PIN_LED_1, !LED_ON_LEVEL);
    digitalWrite(PIN_LED_2, !LED_ON_LEVEL);
    s_offAt1 = 0;
    s_offAt2 = 0;
  }
}


// =====================================================================
//  Console
// =====================================================================

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
