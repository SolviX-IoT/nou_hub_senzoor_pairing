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

  // Marcat de comanda `remove`: device-ul mai este pastrat doar cat sa
  // primeasca un CMD_DOWN de tip RESET, apoi dispare din registru.
  bool     pendingReset;

  // millis() la ultimul pachet valid. NU se pastreaza peste repornire -
  // este relativ la pornirea hub-ului si se pune pe 0 la incarcare.
  uint32_t lastSeenMs;
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

  // Prima adresa libera din DEV_ADDR_MIN..DEV_ADDR_MAX, sau 0 daca nu
  // mai este niciuna (registrul plin).
  uint8_t allocateAddress();

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

  // Afiseaza registrul pe Serial, in forma ceruta de comanda `list`.
  void printAll();

  // Afiseaza lista de provisioning din Config.h - cine ARE VOIE sa se
  // inroleze, indiferent daca s-a inrolat deja sau nu.
  void printProvisioned();
}

#endif // DEVICE_REGISTRY_H
