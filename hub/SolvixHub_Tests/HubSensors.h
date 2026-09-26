/*
  HubSensors.h - tot ce vorbeste cu senzorii.
  ---------------------------------------------------------------------
  Module, in ordinea dependentelor, fiecare in namespace-ul lui:
    LoRaRadio       invelisul peste libraria LoRa (SX1276)
    DeviceRegistry  registrul senzorilor inrolati, in NVS (solvix-pair)
    SensorLink      runtime-ul permanent: pairing, date, ACK, dezinrolare

  Fiecare modul isi pastreaza mai jos descrierea completa.
*/

#ifndef HUB_SENSORS_H
#define HUB_SENSORS_H

#include <Arduino.h>
#include <LoRa.h>
#include "Config.h"
#include "HubBoard.h"
#include "SensorPacket.h"

// =====================================================================
//  LoRaRadio
// =====================================================================

/*
  LoRaRadio - invelis peste libraria LoRa (Sandeep Mistry) pentru SX1276.
  ---------------------------------------------------------------------
  Reguli respectate aici, toate legate de faptul ca modulul imparte
  magistrala SPI cu ENC28J60:

  - inainte de orice operatie se apeleaza SpiBus::claimLoRa(), care ridica
    CS-ul modulului Ethernet;
  - dupa terminarea operatiei, NSS-ul este ridicat inapoi, ca Ethernet-ul
    sa poata folosi bus-ul;
  - NU se apeleaza LoRa.end(). Acea functie inchide SPI-ul intregului
    ESP32, iar modulul Ethernet ar ramane fara ceas pana la un nou
    SPI.begin(). Pentru oprire se foloseste sleep();
  - receptia se face prin polling cu LoRa.parsePacket(), nu prin
    LoRa.onReceive(). Un callback ar accesa SPI dintr-o intrerupere,
    posibil in mijlocul unui transfer Ethernet aflat in desfasurare.
*/

namespace LoRaRadio {

  // Reset hardware + LoRa.begin() pe frecventa din Config.h, urmat de
  // aplicarea parametrilor de modulatie (SF, BW, CR, sync word, CRC).
  // Parametrii sunt tot din Config.h si trebuie sa ramana identici cu
  // cei ai nodului senzor - altfel nu se receptioneaza nimic, si fara
  // niciun mesaj de eroare.
  bool begin(long frequency = LORA_FREQUENCY);

  bool isReady();

  // Trimite un pachet, ca octeti bruti. Nu exista varianta cu String, si
  // nici nu trebuie sa existe: JOIN_ACCEPT si CMD_DOWN pot contine octeti
  // 0x00 (adresa, contoare), iar String i-ar trata drept terminator de
  // sir - exact problema care a dus la receiveRaw() (F-019).
  bool sendRaw(const uint8_t* data, uint8_t length);

  // Verifica daca a sosit un pachet. Daca da, il pune in out si
  // completeaza rssi/snr. Nu blocheaza.

  // Varianta binara a lui receive(). Obligatorie pentru pachetele
  // nodului senzor: acolo un octet poate fi 0x00, iar String l-ar trata
  // drept terminator de sir. Pune in "length" numarul de octeti cititi,
  // cel mult maxLength. Nu blocheaza.
  bool receiveRaw(uint8_t* buffer, int maxLength, int& length,
                  int& rssi, float& snr);

  // Trece radioul in consum minim, fara sa inchida magistrala SPI.
  void sleep();
}


// =====================================================================
//  DeviceRegistry
// =====================================================================

/*
  DeviceRegistry - registrul senzorilor inrolati.
  ---------------------------------------------------------------------
  Tine minte, PESTE REPORNIRI ale hub-ului, ce senzori au trecut prin
  pairing si ce numar are fiecare. Fara asta, orice pana de curent ar
  obliga toate placile din teren sa se re-inroleze - iar ele nu pot,
  fiindca hub-ul accepta JOIN_REQ doar in modul pairing.

  UNDE SE SALVEAZA: in NVS, prin biblioteca Preferences din nucleul
  ESP32. NVS este o partitie separata de flash, cu wear-leveling propriu;
  nu are legatura cu sketch-ul si supravietuieste unei reprogramari
  obisnuite.

  CAND SE SALVEAZA: la fiecare inrolare, la fiecare stergere, si o data
  la REGISTRY_SAVE_EVERY pachete de date. Contorul de pachete se tine in
  RAM intre salvari, ca sa nu se scrie in flash la fiecare pachet primit;
  verificarea anti-replay cere doar ca frame counter-ul sa fie STRICT
  CRESCATOR, deci o valoare salvata "in urma" nu deschide nicio bresa
  atat timp cat senzorul nu isi reia niciodata contorul de la zero (si nu
  si-l reia: la cold boot sare inainte, vezi senzor/main.c, sectiunea 16).

  LISTA DE PROVISIONING (DevEUI-urile admise) NU este acelasi lucru cu
  registrul: ea spune CINE ARE VOIE sa se inroleze si sta in Config.h,
  compilata in program. Registrul spune CINE S-A INROLAT DEJA si traieste
  in NVS.

  ---------------------------------------------------------------------
  NUMEROTAREA SENZORILOR
  ---------------------------------------------------------------------
  Reteaua are pana la HUB_MAX_SENSORS placi, iar fiecare are un NUMAR
  stabil, 1..HUB_MAX_SENSORS. Numarul NU este o eticheta pusa pe deasupra:
  este chiar DevAddr-ul din protocol, adica octetul [2] al fiecarui
  DATA_UP si al fiecarui CMD_DOWN.

  Numarul vine din POZITIA senzorului in tabelul PROVISIONED_DEVICES_INIT
  din Config.h, nu din ordinea inrolarii (addressForEui). Asta inseamna:

    - senzorul de pe randul 3 este "Senzor #3" la prima inrolare, dupa o
      dezinrolare si o reinrolare, si dupa o golire completa a
      registrului;
    - doua placi nu pot primi niciodata acelasi numar;
    - operatorul poate scrie "3" pe cutie si numarul ramane adevarat.

  Acelasi numar il stie si placa: este SENSOR_NODE_ID din senzor/main.c,
  din care ies acolo DevEUI si slotul de somn care o desincronizeaza de
  celelalte. Randul N din tabel <-> placa cu SENSOR_NODE_ID N.
*/

// O pereche din lista de provisioning. Valorile stau in Config.h
// (PROVISIONED_DEVICES_INIT); aici este doar forma lor.
struct ProvisionedDevice {
  uint8_t devEui[DEV_EUI_LEN];
};

// Un senzor inrolat. Structura este salvata ca atare in NVS, deci orice
// modificare a ei invalideaza registrul salvat (vezi REGISTRY_BLOB_VERSION
// in HubSensors.cpp).
struct DeviceRecord {
  uint8_t  devEui[DEV_EUI_LEN];

  // Numarul senzorului, 1..HUB_MAX_SENSORS, si in acelasi timp adresa
  // lui din protocol. Vine din pozitia in tabelul de provisioning, deci
  // este stabil peste reinrolari si peste golirea registrului.
  uint8_t  devAddr;

  // Ultimul frame counter acceptat de la senzor. Un pachet cu o valoare
  // mai mica sau egala este un replay si se arunca.
  uint32_t lastFrameCounterUp;

  // Are sens doar impreuna cu campul de mai sus: imediat dupa inrolare
  // nu a venit inca niciun pachet, iar primul poate avea counter 0.
  bool     hasUplink;

  // Contorul pachetelor de downlink trimise catre senzor (ACK / RESET).
  uint32_t downCounter;

  // Cate pachete de date valide au venit de la acest senzor.
  uint32_t packets;

  // Cate pachete ale acestui senzor NU au ajuns niciodata la hub.
  // Se deduc din GOLURILE din frame counter: senzorul il incrementeaza
  // la fiecare transmisie, inclusiv la cele esuate, deci un salt de la
  // 41 la 44 inseamna doua pachete pierdute pe drum. Este singurul
  // indicator direct de coliziune sau de acoperire proasta pe care il
  // are hub-ul - un pachet pierdut nu lasa nicio alta urma.
  uint32_t lostPackets;

  // Marcat de comanda `remove`. Inregistrarea NU se sterge la trimiterea
  // primului RESET: se pastreaza, cu cheia intacta, cat timp senzorul
  // inca se aude, ca sa i se poata retrimite comanda (F-031).
  bool     pendingReset;

  // Cate CMD_DOWN(RESET) i-au fost trimise de la comanda `remove`.
  // Fiecare pachet primit de la un device marcat inseamna ca nu a primit
  // comanda precedenta, deci se mai incearca o data.
  uint16_t resetAttempts;

  // millis() la ultimul RESET trimis; 0 = niciunul in ACEASTA sesiune.
  // Ca si lastSeenMs, este relativ la pornirea hub-ului si se pune pe 0
  // la incarcarea din NVS: o valoare veche ar face ca dezinrolarea sa
  // para confirmata imediat dupa repornire, fara ca vreun RESET sa fi
  // plecat efectiv.
  uint32_t resetSentMs;

  // millis() la ultimul pachet valid. NU se pastreaza peste repornire -
  // este relativ la pornirea hub-ului si se pune pe 0 la incarcare.
  uint32_t lastSeenMs;

  // --- Ultima masuratoare, pentru tabelul comenzii `sensors` ---------
  // Toate trei sunt relative la sesiunea curenta si se zeroizeaza la
  // incarcarea din NVS, ca lastSeenMs: o temperatura de acum trei
  // saptamani afisata ca "ultima citire" ar induce in eroare.
  int16_t  lastTempX100;
  int16_t  lastRssi;
  bool     hasReading;

  // true cat timp senzorul este considerat "nu se mai aude"
  // (SENSOR_OFFLINE_MS fara niciun pachet valid). Serveste doar ca sa se
  // anunte O SINGURA DATA caderea si o singura data revenirea, in loc de
  // o linie pe Serial la fiecare trecere prin tick().
  bool     offlineReported;
};

namespace DeviceRegistry {

  // Deschide NVS si incarca registrul. Se cheama o singura data, din
  // setup(). Intoarce false daca NVS nu a putut fi deschis.
  bool begin();

  // Cate device-uri sunt inrolate acum.
  uint8_t count();

  // Inregistrarea de pe pozitia "index", sau nullptr daca nu exista.
  DeviceRecord* at(uint8_t index);

  DeviceRecord* findByEui(const uint8_t* devEui);
  DeviceRecord* findByAddr(uint8_t devAddr);

  // true daca DevEUI-ul apare in lista de provisioning din Config.h,
  // adica senzorul are voie sa se inroleze.
  //
  // NU se inlocuieste cu "addressForEui() != 0": acela intoarce 0 si
  // pentru "nu e in tabel", si pentru "pozitia depaseste
  // HUB_MAX_SENSORS", iar handleJoinRequest are mesaje diferite pentru
  // cele doua cazuri.
  bool isProvisioned(const uint8_t* devEui);

  // Cate randuri are lista de provisioning din Config.h.
  uint8_t provisionedCount();

  // DevEUI-ul randului "index" din lista de provisioning, sau nullptr.
  const uint8_t* provisionedEui(uint8_t index);

  // NUMARUL senzorului = adresa lui = pozitia in lista de provisioning
  // plus unu. Intoarce 0 daca DevEUI nu este in lista, sau daca pozitia
  // lui depaseste HUB_MAX_SENSORS.
  //
  // Aceasta functie a inlocuit vechiul allocateAddress(), care dadea
  // prima adresa libera. Diferenta se vede abia cu mai multi senzori:
  // acolo "prima libera" facea ca numarul unei placi sa depinda de
  // ordinea in care au fost pornite si sa se schimbe dupa fiecare
  // dezinrolare, deci sa nu poata fi scris pe cutie.
  uint8_t addressForEui(const uint8_t* devEui);

  // Adauga sau inlocuieste inregistrarea unui senzor. Intoarce pointerul
  // catre inregistrarea din registru, sau nullptr daca nu mai este loc.
  // Salveaza imediat in NVS.
  DeviceRecord* add(const uint8_t* devEui, uint8_t devAddr);

  // Scoate un device din registru si salveaza. Intoarce false daca nu a
  // fost gasit.
  bool removeByEui(const uint8_t* devEui);

  // Salveaza registrul in NVS. Se cheama automat la add/remove; explicit
  // doar cand se vrea fortarea unui checkpoint.
  bool save();

  // Reincarca registrul din NVS, aruncand ce este in RAM.
  bool load();

  // Sterge complet registrul (si din NVS).
  void clear();

  // Afiseaza registrul pe Serial, in forma ceruta de comanda `list`:
  // o linie pe senzor, cu identitatea si starea dezinrolarii.

  // Tabelul comenzii `sensors`: toate cele HUB_MAX_SENSORS locuri, si
  // cele ocupate, si cele libere, cu ultima masuratoare si cu starea
  // legaturii. Este vederea de zi cu zi asupra retelei; `list` ramane
  // vederea asupra registrului.
  void printSensorTable();

  // Afiseaza lista de provisioning din Config.h - cine ARE VOIE sa se
  // inroleze, indiferent daca s-a inrolat deja sau nu.
}


// =====================================================================
//  SensorLink
// =====================================================================

/*
  SensorLink - legatura radio cu senzorii. RUNTIME PERMANENT.
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

namespace SensorLink {
  bool begin();
  void tick();

  // Deschide fereastra de inrolare pentru PAIRING_MODE_TIMEOUT_MS.
  // Un al doilea apel reporneste numaratoarea.
  void enterPairingMode();

  // Inchide fereastra de inrolare inainte de expirare.
  void exitPairingMode();

  bool isPairingMode();

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

}

#endif // HUB_SENSORS_H
