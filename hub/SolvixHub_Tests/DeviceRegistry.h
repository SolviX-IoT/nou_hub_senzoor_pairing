/*
  DeviceRegistry.h - registrul senzorilor inrolati.
  ---------------------------------------------------------------------
  Tine minte, PESTE REPORNIRI ale hub-ului, ce senzori au trecut prin
  pairing si cu ce cheie de sesiune. Fara asta, orice pana de curent ar
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

  LISTA DE PROVISIONING (DevEUI -> AppKey) NU este acelasi lucru cu
  registrul: ea spune CINE ARE VOIE sa se inroleze si sta in Config.h,
  compilata in program. Registrul spune CINE S-A INROLAT DEJA si traieste
  in NVS.

  ---------------------------------------------------------------------
  NUMEROTAREA SENZORILOR
  ---------------------------------------------------------------------
  Reteaua are pana la HUB_MAX_SENSORS placi, iar fiecare are un NUMAR
  stabil, 1..HUB_MAX_SENSORS. Numarul NU este o eticheta pusa pe deasupra:
  este chiar DevAddr-ul din protocol, adica octetul [2] al fiecarui
  DATA_ENC si al fiecarui CMD_DOWN.

  Numarul vine din POZITIA senzorului in tabelul PROVISIONED_DEVICES_INIT
  din Config.h, nu din ordinea inrolarii (addressForEui). Asta inseamna:

    - senzorul de pe randul 3 este "Senzor #3" la prima inrolare, dupa o
      dezinrolare si o reinrolare, si dupa o golire completa a
      registrului;
    - doua placi nu pot primi niciodata acelasi numar;
    - operatorul poate scrie "3" pe cutie si numarul ramane adevarat.

  Acelasi numar il stie si placa: este SENSOR_NODE_ID din senzor/main.c,
  din care ies acolo DevEUI, AppKey si slotul de somn care o desincro-
  nizeaza de celelalte. Randul N din tabel <-> placa cu SENSOR_NODE_ID N.
*/

#ifndef DEVICE_REGISTRY_H
#define DEVICE_REGISTRY_H

#include <Arduino.h>
#include "Config.h"
#include "SensorPacket.h"

// O pereche din lista de provisioning. Valorile stau in Config.h
// (PROVISIONED_DEVICES_INIT); aici este doar forma lor.
struct ProvisionedDevice {
  uint8_t devEui[DEV_EUI_LEN];
  uint8_t appKey[CRYPTO_KEY_LEN];
};

// Un senzor inrolat. Structura este salvata ca atare in NVS, deci orice
// modificare a ei invalideaza registrul salvat (vezi REGISTRY_BLOB_VERSION
// in DeviceRegistry.cpp).
struct DeviceRecord {
  uint8_t  devEui[DEV_EUI_LEN];

  // Numarul senzorului, 1..HUB_MAX_SENSORS, si in acelasi timp adresa
  // lui din protocol. Vine din pozitia in tabelul de provisioning, deci
  // este stabil peste reinrolari si peste golirea registrului.
  uint8_t  devAddr;

  uint8_t  sessKey[CRYPTO_KEY_LEN];

  // Ultimul frame counter acceptat de la senzor. Un pachet cu o valoare
  // mai mica sau egala este un replay si se arunca.
  uint32_t lastFrameCounterUp;

  // Are sens doar impreuna cu campul de mai sus: imediat dupa inrolare
  // nu a venit inca niciun pachet, iar primul poate avea counter 0.
  bool     hasUplink;

  // Contorul pachetelor de downlink trimise catre senzor (ACK / RESET).
  uint32_t downCounter;

  // Ultimul DevNonce folosit intr-un JOIN_REQ acceptat. Un JOIN_REQ care
  // repeta acest nonce este un replay si se refuza.
  uint16_t lastDevNonce;

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

  // AppKey-ul unui DevEUI din lista de provisioning din Config.h, sau
  // nullptr daca senzorul nu are voie sa se inroleze deloc.
  const uint8_t* findAppKey(const uint8_t* devEui);

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
  DeviceRecord* add(const uint8_t* devEui, uint8_t devAddr,
                    const uint8_t* sessKey, uint16_t devNonce);

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
  void printAll();

  // Tabelul comenzii `sensors`: toate cele HUB_MAX_SENSORS locuri, si
  // cele ocupate, si cele libere, cu ultima masuratoare si cu starea
  // legaturii. Este vederea de zi cu zi asupra retelei; `list` ramane
  // vederea asupra registrului.
  void printSensorTable();

  // Afiseaza lista de provisioning din Config.h - cine ARE VOIE sa se
  // inroleze, indiferent daca s-a inrolat deja sau nu.
  void printProvisioned();
}

#endif // DEVICE_REGISTRY_H
