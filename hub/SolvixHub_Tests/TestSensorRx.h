/*
  TestSensorRx.h - receptia masuratorilor de la nodul senzor.
  ---------------------------------------------------------------------
  Perechea de pe hub pentru firmware-ul din senzor/main.c: asculta
  pachetele de temperatura trimise de PIC16LF1508 si le afiseaza pe
  Serial, cu RSSI si SNR.

  Diferenta fata de TestLoRaRx: acolo se afiseaza orice pachet, ca text
  brut, pentru un test de legatura intre doua placi identice. Aici
  pachetul este BINAR, are un format fix de 6 octeti descris in
  SensorPacket.h, si este validat prin magic + checksum inainte de a fi
  luat in seama. Pachetele respinse se afiseaza in hexazecimal, ca sa se
  vada daca vine ceva si nu se potriveste, sau nu vine nimic deloc.

  LED-uri:
    - LED 1 (D22) pulseaza la fiecare pachet VALID;
    - LED 2 (D21) sta aprins cat timp radioul asculta.
*/

#ifndef TEST_SENSOR_RX_H
#define TEST_SENSOR_RX_H

#include "TestBase.h"
#include "LoRaRadio.h"
#include "SensorPacket.h"
#include "Leds.h"

namespace TestSensorRx {
  bool begin();
  void tick();
  void stop();
}

#endif // TEST_SENSOR_RX_H
