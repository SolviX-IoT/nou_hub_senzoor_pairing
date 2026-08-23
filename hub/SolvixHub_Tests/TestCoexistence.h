/*
  TestCoexistence.h - Ethernet SI LoRa active in acelasi timp.
  ---------------------------------------------------------------------
  Acesta este testul care conteaza pentru hub: ENC28J60 si SX1276 sunt pe
  aceeasi magistrala SPI si trebuie sa functioneze alternativ, fara sa se
  incurce.

  Ordinea de initializare este importanta:
    1. mai intai LoRa. Libraria LoRa cere ca NSS-ul ei sa fie stabil in
       timpul secventei de reset a radioului, iar reset-ul se face inainte
       sa existe trafic Ethernet;
    2. apoi Ethernet cu DHCP. Modulul LoRa este deja deselectat, deci nu
       vede nimic din traficul DHCP.

  In bucla, cele doua module se folosesc pe rand, niciodata simultan:
    - la fiecare 3 secunde: un pachet LoRa;
    - la fiecare 15 secunde: o cerere HTTP;
    - in permanenta: verificarea pachetelor LoRa primite, prin polling.

  Testul raporteaza contoare pentru ambele module. Daca unul dintre ele
  incepe sa esueze exact atunci cand celalalt lucreaza, exista un
  conflict real pe bus (cel mai probabil un CS care ramane jos, sau un
  modul care nu isi elibereaza linia MISO).
*/

#ifndef TEST_COEXISTENCE_H
#define TEST_COEXISTENCE_H

#include "TestBase.h"
#include "EthernetLink.h"
#include "LoRaRadio.h"

namespace TestCoexistence {
  bool begin();
  void tick();
  void stop();
}

#endif // TEST_COEXISTENCE_H
