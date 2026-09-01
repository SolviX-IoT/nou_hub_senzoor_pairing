/*
  HubCloud.h - pornirea in cloud: sanatatea serverului si provisioning-ul.
  ---------------------------------------------------------------------
  Implementeaza pasii 2 si 4 din diagrama de pornire, ca masina de stari
  chemata din loop():

    PASUL 2  GET /api/health, cu antetul X-Solvix-AdminKey. Serverul este
             considerat bun doar daca "database" este "Reachable" -
             "status": "Healthy" singur NU este suficient, fiindca API-ul
             poate raspunde perfect cu baza de date cazuta.
             La esec se reincearca, cu backoff (Config.h).

    PASUL 4  Daca identitatea din HubIdentity este goala, adica hub-ul nu
             a fost inca provizionat: POST /api/device/provision cu
             parametrii de fabrica din Config.h. Raspunsul se salveaza in
             NVS si de atunci hub-ul porneste provizionat.

  DUPA ACEEA NU MAI FACE NIMIC. Heartbeat-ul si telemetria sunt etapa
  urmatoare; carligele lor sunt declarate mai jos, dar nu au corp.

  DE CE CERERILE SUNT BLOCANTE, SI DE CE ESTE ACCEPTABIL
  ---------------------------------------------------------------------
  O cerere ruleaza pana la capat intr-un singur tick(), cel mult
  HTTP_BUDGET_MS. Nu este o scapare, este o alegere:

    - EthernetENC nu are connect() neblocant, deci o masina de stari
      HTTP tot ar avea o gaura de peste o secunda: cea mai mare parte a
      castigului dispare oricum;
    - o sesiune intinsa pe mai multe tick()-uri ar lasa LoRa sa vorbeasca
      in mijlocul unui transfer Ethernet, pe magistrala partajata. Ar
      trebui rezervat bus-ul la fiecare pas, deci modulul de HTTP ar
      trebui sa stie pe ce transport merge - exact ce evita NetLink;
    - bootstrap-ul se face O DATA si apoi tace. Cand va veni telemetria,
      raspunsul corect nu este intreteserea, ci trimiterea in loturi: un
      POST la cloudSyncIntervalSeconds cu maxBatchSize masuratori. Chiar
      asa spune si config-ul primit de la server.

  Costul, spus pe fata: cat dureaza o cerere, hub-ul este SURD. Nu
  intarziat - surd: LoRa.parsePacket() lucreaza in RX_SINGLE, care expira
  dupa ~102 ms, deci pachetele din intervalul acela se pierd de tot. Cu
  buget de 2,5 s si o reincercare la 60 s inseamna ~4% din timp, si numai
  cat timp cloud-ul este cazut. Pierderile se vad in coloana `pierd.` din
  `sensors` - nu sunt tacute.

  Cele doua porti de mai jos taie aproape tot ce se putea taia.
*/

#ifndef HUB_CLOUD_H
#define HUB_CLOUD_H

#include <Arduino.h>
#include "Config.h"

namespace HubCloud {

  enum class State : uint8_t {
    NetWait,        // nu exista inca adresa IP
    Health,         // urmeaza un GET /api/health
    HealthBackoff,  // serverul nu a raspuns bine; se asteapta
    Provision,      // urmeaza un POST /api/device/provision
    Ready,          // gata: server sanatos si hub provizionat
    Blocked         // esec care nu se repara singur de la sine
  };

  void begin();
  void tick();

  State       state();
  const char* stateName();
  void        printStatus();

  // Forteaza o verificare de sanatate acum (comanda `health`).
  void forceHealth();

  // Forteaza o incercare de provisioning (comanda `provision`).
  // Refuza daca hub-ul este deja provizionat: un al doilea provisioning
  // pentru acelasi deviceUid poate insemna, pe un server neidempotent, un
  // hub nou si istoricul vechi orfan. Calea corecta este `forget yes`.
  void forceProvision();
}

#endif // HUB_CLOUD_H
