/*
  TestEncSpi.h - diagnostic SPI de nivel jos pentru ENC28J60.
  ---------------------------------------------------------------------
  Nu foloseste nicio librarie de Ethernet. Vorbeste direct cu chipul si
  arata byte cu byte ce se trimite si ce se primeste inapoi. Este primul
  test de rulat cand Ethernet-ul nu merge: separa problemele de cablaj
  de cele de retea.

  PARTEA A - test de cablaj, cu ambele CS-uri pe HIGH.
    Se trimit tipare cunoscute (0x00, 0xFF, 0xAA, 0x55, ...). Chipul nu
    asculta, deci nu ar trebui sa raspunda nimic coerent.
      - daca MISO repeta exact ce s-a trimis pe MOSI, cele doua fire sunt
        probabil scurtcircuitate pe cablaj;
      - daca se primeste mereu 0x00 sau mereu 0xFF, este normal: linia
        MISO este in high-impedance. Se trece la partea B.

  PARTEA B - citire reala de registre, cu CS-ul Ethernet-ului activ.
    EREVID trebuie sa fie intre 0x01 si 0x07. Valorile 0x00 si 0xFF
    inseamna ca cipul nu raspunde deloc.
    ESTAT bit 0 (CLKRDY) trebuie sa fie 1 dupa stabilizarea oscilatorului.

  Testul ruleaza la ETH_SPI_HZ_DEBUG (1 MHz), viteza mica si tolerabila
  chiar si pe fire lungi de prototip.
*/

#ifndef TEST_ENC_SPI_H
#define TEST_ENC_SPI_H

#include "TestBase.h"

namespace TestEncSpi {
  bool begin();
  void tick();
  void stop();
}

#endif // TEST_ENC_SPI_H
