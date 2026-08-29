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

       CU MAI MULTI SENZORI, doua lucruri se schimba aici. Primul: fiecare
       linie afisata incepe cu NUMARUL senzorului, care este chiar
       DevAddr-ul din pachet, deci raspunsul la "de la cine vine data" nu
       cere nicio deducere - este scris in pachet si este acoperit de MIC,
       asa ca nu poate fi nici falsificat, nici confundat. Al doilea:
       golurile din frame counter sunt numarate ca pachete PIERDUTE.
       Senzorul isi incrementeaza contorul la fiecare transmisie, deci un
       salt de la 41 la 44 inseamna doua pachete care nu au ajuns - cel
       mai adesea, o coliziune cu alt senzor. Fara contorul asta, o
       coliziune nu lasa absolut nicio urma pe hub.

    3. DEZINROLARE CONFIRMATA. Un device marcat de comanda `remove`
       primeste un CMD_DOWN de tip RESET la FIECARE pachet al lui, si
       inregistrarea NU se sterge inca. Senzorul care inca emite este
       dovada ca nu a primit comanda; senzorul care tace este dovada ca
       a primit-o, fiindca dupa RESET isi sterge cheia din HEF si intra
       in repaus. Abia dupa REMOVE_CONFIRM_SILENCE_MS de tacere
       inregistrarea dispare din registru. Varianta veche stergea la
       prima trimitere si, daca acel unic downlink se pierdea, hub-ul
       ramanea fara cheie si nu mai putea opri senzorul niciodata
       (F-031).

    4. SUPRAVEGHEREA TACERII. Un senzor care nu s-a mai auzit de
       SENSOR_OFFLINE_MS este anuntat o data pe Serial, si tot o data la
       revenire. Cu o singura placa se vedea imediat ca nu mai vine
       nimic; cu cinci, jurnalul curge in continuare vesel si lipsa
       exact a uneia dintre ele trece neobservata.

  DIFERENTA FATA DE TESTUL 7: acolo se asculta pachetul de temperatura in
  CLAR, fara nicio identitate - orice emitator cu aceiasi parametri radio
  este crezut pe cuvant. Aici fiecare pachet este legat de un device
  inrolat si de o cheie, iar un pachet rejucat este respins.

  DE CE NU E NEVOIE DE ARBITRAJ INTRE SENZORI: hub-ul nu programeaza
  sloturi si nu cere nimanui sa astepte. Doua pachete suprapuse se pierd
  amandoua, dar senzorii au intervale de somn diferite si jitter propriu
  (senzor/main.c, sectiunea 1), deci nu raman ciocniti: se despart
  singuri dupa o perioada. Un protocol de rezervare a canalului ar fi
  costat pe senzor mai mult decat pierde astazi in coliziuni.

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
