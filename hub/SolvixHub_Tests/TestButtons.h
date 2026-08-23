/*
  TestButtons.h - citirea celor doua butoane de pe placa.

  GPIO34 si GPIO35 sunt pini exclusiv de intrare si nu au rezistente
  interne de pull-up sau pull-down. Fara un rezistor extern pe placa,
  starea citita este zgomot si va oscila aleator - de aceea testul
  raporteaza si de cate ori s-a schimbat starea, ca sa se vada imediat
  daca linia e flotanta.

  Butoanele nu ating magistrala SPI, deci acest test nu are nicio
  interactiune cu modulele Ethernet sau LoRa.
*/

#ifndef TEST_BUTTONS_H
#define TEST_BUTTONS_H

#include "TestBase.h"

namespace TestButtons {
  bool begin();
  void tick();
  void stop();
}

#endif // TEST_BUTTONS_H
