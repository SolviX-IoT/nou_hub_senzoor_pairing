#include "Leds.h"

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
