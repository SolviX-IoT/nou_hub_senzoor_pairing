/*
  TestLoRaRx.h - receptie LoRa.
  ---------------------------------------------------------------------
  Pereche pentru TestLoRaTx: se incarca pe a doua placa si afiseaza
  pachetele primite impreuna cu RSSI si SNR.

  Receptia se face prin polling (LoRa.parsePacket) si nu prin intreruperea
  DIO0 cu callback. Motivul este partajarea magistralei SPI: un callback
  ar accesa bus-ul dintr-o intrerupere, care poate cadea exact in
  mijlocul unui transfer catre ENC28J60 si ar corupe ambele transferuri.
*/

#ifndef TEST_LORA_RX_H
#define TEST_LORA_RX_H

#include "TestBase.h"
#include "LoRaRadio.h"

namespace TestLoRaRx {
  bool begin();
  void tick();
  void stop();
}

#endif // TEST_LORA_RX_H
