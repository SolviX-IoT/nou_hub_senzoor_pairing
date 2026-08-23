/*
  Leds.h - cele doua LED-uri ale hub-ului (D22 si D21).
  ---------------------------------------------------------------------
  Modulul exista ca sa nu apara digitalWrite pe un numar de pin prin
  teste. Pinii si polaritatea stau doar in Config.h.

  Doua feluri de a aprinde un LED:

  - set(...)   - nivel continuu. Se foloseste pentru stare: "radioul
                 asculta", "am adresa IP".
  - pulse(...) - aprinde acum si stinge singur dupa LED_PULSE_MS.
                 Se foloseste pentru evenimente: "a venit un pachet".

  pulse() NU foloseste delay(): stingerea se face din service(), pe care
  fiecare tick() de test il apeleaza. Asa receptia urmatorului pachet nu
  este intarziata de LED. Un test care foloseste pulse() TREBUIE sa
  cheme service() in tick()-ul lui, altfel LED-ul ramane aprins.
*/

#ifndef LEDS_H
#define LEDS_H

#include <Arduino.h>
#include "Config.h"

namespace Leds {

  // Configureaza ambii pini ca iesiri si stinge LED-urile. Apelata o
  // singura data, din setup(). Apelurile ulterioare nu fac nimic.
  void begin();

  // Nivel continuu pe un LED.
  void set(uint8_t pin, bool on);

  // Aprinde LED-ul si programeaza stingerea peste LED_PULSE_MS.
  void pulse(uint8_t pin);

  // Stinge pulsurile ajunse la scadenta. Se cheama din tick().
  void service();

  // Stinge ambele LED-uri si anuleaza pulsurile in curs. Se cheama la
  // oprirea unui test, ca urmatorul sa porneasca de la zero.
  void allOff();
}

#endif // LEDS_H
