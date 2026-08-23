/*
  TestPairing.h - testul 8: inrolarea si receptia criptata.
  ---------------------------------------------------------------------
  Perechea de pe hub pentru masina de stari din senzor/main.c. Face trei
  lucruri, in acelasi tick():

    1. INROLARE. Doar cat timp hub-ul este in "mod pairing" (comanda
       `pair` pe Serial sau apasarea butonului 1), un JOIN_REQ este luat
       in seama: se cauta DevEUI in lista de provisioning din Config.h,
       se verifica MIC-ul cu AppKey, se aloca un DevAddr, se deriva
       cheia de sesiune si se raspunde cu JOIN_ACCEPT. In afara modului
       pairing, orice JOIN_REQ este refuzat si raportat.

    2. DATE. Un DATA_ENC este acceptat oricand, daca vine de la o adresa
       inrolata, are frame counter STRICT mai mare decat ultimul si MIC
       valid. Payload-ul decriptat este dat lui SensorPacketCodec::decode(),
       adica exact aceluiasi cod care serveste testul 7.

    3. DEZINROLARE. Un device marcat de comanda `remove` primeste, la
       primul contact, un CMD_DOWN de tip RESET, dupa care este sters din
       registru. Senzorul isi sterge cheia din HEF si revine la pairing.

  DIFERENTA FATA DE TESTUL 7: acolo se asculta pachetul de temperatura in
  CLAR, fara nicio identitate - orice emitator cu aceiasi parametri radio
  este crezut pe cuvant. Aici fiecare pachet este legat de un device
  inrolat si de o cheie, iar un pachet rejucat este respins.

  LED-uri:
    - LED 1 (D22) pulseaza la fiecare pachet de date VALID;
    - LED 2 (D21) sta aprins cat timp radioul asculta si CLIPESTE cat
      timp hub-ul este in mod pairing.
*/

#ifndef TEST_PAIRING_H
#define TEST_PAIRING_H

#include "TestBase.h"
#include "LoRaRadio.h"
#include "SensorPacket.h"
#include "HubCrypto.h"
#include "DeviceRegistry.h"
#include "Leds.h"

namespace TestPairing {
  bool begin();
  void tick();
  void stop();

  // true daca testul a pornit si ruleaza (radioul este initializat).
  bool isRunning();

  // Deschide fereastra de inrolare pentru PAIRING_MODE_TIMEOUT_MS.
  // Un al doilea apel reporneste numaratoarea.
  void enterPairingMode();

  // Inchide fereastra de inrolare inainte de expirare.
  void exitPairingMode();

  bool isPairingMode();

  // Contoarele testului, pentru comanda `stats`.
  void printStats();
}

#endif // TEST_PAIRING_H
