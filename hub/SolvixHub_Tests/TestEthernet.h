/*
  TestEthernet.h - test de retea si de internet prin ENC28J60.
  ---------------------------------------------------------------------
  De rulat DUPA ce testul 1 (diagnostic SPI) arata un EREVID valid.

  Pasii:
    1. reset hardware al modulului si obtinerea unui IP prin DHCP;
    2. rezolvarea numelui "google.com" (test de DNS);
    3. o cerere HTTP GET pe portul 80 catre acel IP;
    4. daca soseste o linie de raspuns, exista internet real, nu doar
       retea locala.

  Modulul LoRa este mentinut deselectat pe toata durata testului, prin
  EthernetLink care apeleaza SpiBus::claimEthernet() inainte de fiecare
  operatie.
*/

#ifndef TEST_ETHERNET_H
#define TEST_ETHERNET_H

#include "TestBase.h"
#include "EthernetLink.h"

namespace TestEthernet {
  bool begin();
  void tick();
  void stop();
}

#endif // TEST_ETHERNET_H
