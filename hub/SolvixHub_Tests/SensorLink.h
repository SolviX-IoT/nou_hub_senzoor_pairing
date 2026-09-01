/*
  SensorLink.h - legatura radio cu senzorii. RUNTIME PERMANENT.
  ---------------------------------------------------------------------
  Acest fisier se numea TestPairing.h si era "testul 8" din meniul de pe
  Serial: nu rula decat daca operatorul tasta o cifra. Era insa singurul
  loc din tot sketch-ul in care se intampla ceva util. Acum porneste din
  setup() si ruleaza cat timp hub-ul este alimentat - de aici si numele
  nou, in oglinda cu NetLink (legatura cu reteaua).

  Perechea de pe hub pentru masina de stari din senzor/main.c. Face patru
  lucruri, in acelasi tick():

    1. INROLARE. Doar cat timp hub-ul este in "mod pairing" (comanda
       `pair` pe Serial sau apasarea butonului 1), un JOIN_REQ este luat
       in seama: se verifica DevEUI-ul in lista de provisioning din
       Config.h, se ia numarul senzorului din POZITIA lui in acea lista
       si se raspunde cu JOIN_ACCEPT. In afara modului pairing, orice
       JOIN_REQ este refuzat si raportat.

       ATENTIE: de cand nu mai exista criptografie, apartenenta la lista
       nu mai este DOVEDITA, ci doar declarata. Oricine poate emite un
       JOIN_REQ cu un DevEUI din lista. Inrolarea a ramas o comisionare,
       nu un control de acces - vezi antetul lui SensorPacket.h.

    2. DATE. Un DATA_UP este acceptat oricand, daca vine de la o adresa
       inrolata si are frame counter STRICT mai mare decat ultimul
       valid - singura aparare ramasa pe calea de date. Payload-ul, care
       circula in clar, este dat lui SensorPacketCodec::decode().

       Fiecare linie afisata incepe cu NUMARUL senzorului, care este chiar
       DevAddr-ul din pachet, deci raspunsul la "de la cine vine data" nu
       cere nicio deducere - este scris in pachet. Este insa DECLARATIV:
       fara MIC, orice emitator poate pretinde orice numar. Golurile din
       frame counter sunt numarate ca pachete PIERDUTE: senzorul isi
       incrementeaza contorul la fiecare transmisie, deci un salt de la 41
       la 44 inseamna doua pachete care nu au ajuns - cel mai adesea, o
       coliziune cu alt senzor. Fara contorul asta, o coliziune nu lasa
       absolut nicio urma pe hub.

    3. DEZINROLARE CONFIRMATA. Un device marcat de comanda `remove`
       primeste un CMD_DOWN de tip RESET la FIECARE pachet al lui, si
       inregistrarea NU se sterge inca. Senzorul care inca emite este
       dovada ca nu a primit comanda; senzorul care tace este dovada ca
       a primit-o, fiindca dupa RESET isi sterge inrolarea din HEF si
       intra in repaus. Abia dupa REMOVE_CONFIRM_SILENCE_MS de tacere
       inregistrarea dispare din registru. Varianta veche stergea la
       prima trimitere si, daca acel unic downlink se pierdea, senzorul
       ramanea in retea fara ca hub-ul sa il mai poata opri (F-031).

    4. SUPRAVEGHEREA TACERII. Un senzor care nu s-a mai auzit de
       SENSOR_OFFLINE_MS este anuntat o data pe Serial, si tot o data la
       revenire. Cu o singura placa se vedea imediat ca nu mai vine
       nimic; cu cinci, jurnalul curge in continuare vesel si lipsa
       exact a uneia dintre ele trece neobservata.

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

#ifndef SENSOR_LINK_H
#define SENSOR_LINK_H

#include "Console.h"
#include "LoRaRadio.h"
#include "SensorPacket.h"
#include "DeviceRegistry.h"
#include "Leds.h"

namespace SensorLink {
  bool begin();
  void tick();

  // Salveaza registrul si adoarme radioul. Nu se mai apeleaza la
  // schimbarea unui test - testele nu mai exista - ci inainte de o
  // repornire ceruta de operator (comanda `reboot`), ca frame counter-ele
  // tinute in RAM sa ajunga in NVS.
  void stop();

  // true daca radioul a fost initializat cu succes si runtime-ul merge.
  bool isRunning();

  // Deschide fereastra de inrolare pentru PAIRING_MODE_TIMEOUT_MS.
  // Un al doilea apel reporneste numaratoarea.
  void enterPairingMode();

  // Inchide fereastra de inrolare inainte de expirare.
  void exitPairingMode();

  bool isPairingMode();

  // Contoarele runtime-ului, pentru comanda `stats`.
  void printStats();

  // ------------------------------------------------------------------
  // Ferestre de liniste pentru restul sistemului
  // ------------------------------------------------------------------
  // Momentul (millis) ultimului pachet primit pe radio, valid sau nu.
  //
  // La ce foloseste: senzorul isi deschide fereastra de downlink IMEDIAT
  // dupa ce a emis si o tine deschisa doar DOWNLINK_WINDOW_MS = 600 ms,
  // iar hub-ul ii raspunde in ~55 ms. Orice alta parte a programului care
  // vrea sa faca ceva lung - o cerere HTTP, o reinnoire DHCP - trebuie sa
  // se uite intai aici si sa nu porneasca peste fereastra abia deschisa.
  //
  // Nu este o precautie teoretica: LoRa.parsePacket() pune modemul in
  // RX_SINGLE, care expira dupa ~102 ms. O blocare mai lunga nu INTARZIE
  // receptia, o DISTRUGE - pachetele nu se acumuleaza nicaieri.
  unsigned long lastRxMs();

  // true daca exista cel putin un device marcat cu `remove` care inca
  // asteapta confirmarea prin tacere. Cat timp este adevarat, downlink-ul
  // este singurul lucru care conteaza pe radio si nimic lung nu are voie
  // sa se interpuna (F-031).
  bool hasPendingRemoval();

  // ------------------------------------------------------------------
  // Carligul pentru telemetrie (etapa urmatoare)
  // ------------------------------------------------------------------
  // Chemat pentru fiecare masuratoare VALIDA, din interiorul tick(),
  // inainte de blocul de afisare. Handler-ul ruleaza pe calea fierbinte a
  // receptiei: are voie sa puna intr-o coada si sa se intoarca, NIMIC
  // altceva. O cerere de retea pornita de aici ar cadea fix peste
  // fereastra de downlink a senzorului care tocmai a vorbit.
  typedef void (*ReadingHandler)(uint8_t devAddr, int16_t tempX100,
                                 uint32_t frameCounter, int16_t rssi,
                                 uint8_t reason);
  void onReading(ReadingHandler handler);
}

#endif // SENSOR_LINK_H
