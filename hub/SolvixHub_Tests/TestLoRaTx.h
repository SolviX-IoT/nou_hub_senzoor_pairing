/*
  TestLoRaTx.h - emisie LoRa periodica.
  ---------------------------------------------------------------------
  Trimite un pachet numerotat la fiecare 2 secunde pe frecventa din
  Config.h (868 MHz). Este echivalentul emitatorului din testul initial,
  dar cu CS-ul modulului Ethernet ridicat explicit inainte de fiecare
  transmisie, prin LoRaRadio.

  Un al doilea nod care ruleaza testul de receptie ar trebui sa vada
  contorul crescand fara salturi.
*/

#ifndef TEST_LORA_TX_H
#define TEST_LORA_TX_H

#include "TestBase.h"
#include "LoRaRadio.h"

namespace TestLoRaTx {
  bool begin();
  void tick();
  void stop();
}

#endif // TEST_LORA_TX_H
