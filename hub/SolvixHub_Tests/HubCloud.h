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

  DUPA ACEEA NU MAI FACE NIMIC: in Ready, tick() se intoarce imediat.
  De acolo preia HubHeartbeat, care isi ia ritmul din configul salvat de
  pasul 4 si bate cat timp hub-ul este alimentat. Telemetria in loturi
  ramane etapa urmatoare, si nu are inca niciun carlig aici.

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

    /*
     * Prea multe esecuri de provisioning la rand: se asteapta LUNG.
     *
     * NU este o fundatura, desi asa a fost la inceput. Cat timp exista
     * comanda `provision`, `Blocked` insemna "stop definitiv, reia omul";
     * de cand consola s-a redus la trei comenzi si aceea nu mai exista,
     * un hub blocat ar fi ramas blocat pana la reprogramare - exact in
     * cazul in care se ajunge aici cel mai des, adica atunci cand
     * serverul ne-a limitat (429).
     *
     * Acum este doar treapta cea mai lunga de asteptare:
     * CLOUD_BLOCKED_RETRY_MS, apoi se reintra singur in Health cu
     * contoarele pe zero. Castigul din F-043 ramane intreg - nu se mai
     * hamareste serverul la 11 secunde - dar hub-ul isi revine fara nicio
     * interventie.
     */
    Blocked
  };

  void begin();
  void tick();

  State       state();
  const char* stateName();

  // Ultima eroare, pentru comanda `status`. Sir gol daca nu a fost
  // niciuna sau daca ultima incercare a reusit.
  const char* lastError();
}

#endif // HUB_CLOUD_H
