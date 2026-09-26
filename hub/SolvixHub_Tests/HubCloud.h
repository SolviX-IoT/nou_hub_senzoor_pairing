/*
  HubCloud.h - pornirea in cloud.
  ---------------------------------------------------------------------
  Module, in ordinea dependentelor, fiecare in namespace-ul lui:
    HubIdentity  identitatea primita de la server, in NVS (solvix-hub)
    HubCloud     bootstrap-ul: health -> provision -> Ready

  Fiecare modul isi pastreaza mai jos descrierea completa.
*/

#ifndef HUB_CLOUD_H
#define HUB_CLOUD_H

#include <Arduino.h>
#include "Config.h"

// =====================================================================
//  HubIdentity
// =====================================================================

/*
  HubIdentity - identitatea hub-ului, primita de la server si tinuta in
  flash.
  ---------------------------------------------------------------------
  PASUL 3 din diagrama de pornire. La prima pornire campurile de aici sunt
  GOALE, iar asta este chiar semnalul ca hub-ul nu a fost inca
  provizionat: HubCloud le vede goale si cere /api/device/provision.
  Dupa ce serverul raspunde, valorile se salveaza in NVS si supravietuiesc
  repornirii, deci al doilea boot nu mai cere nimic.

  CE NU STA AICI: parametrii de FABRICA (deviceUid, serialNumber,
  provisioningSecret) sunt compilati in firmware, in Config.h. Ei sunt
  identitatea placii, nu o stare a ei: nu se schimba niciodata la rulare,
  deci nu au ce cauta in NVS.

  SPATIUL NVS ESTE SEPARAT de cel al registrului de senzori
  ("solvix-pair"). Doua motive: cele doua structuri se versioneaza
  independent, iar o stergere a identitatii nu are voie sa dezinroleze
  senzorii.

  ATENTIE, O CAPCANA NVS: doua obiecte Preferences deschise read-write pe
  ACELASI namespace au fiecare propriul handle si propria vedere, si vor
  ajunge sa se contrazica. Modulul asta tine un singur Preferences, pe
  namespace-ul lui. Nu deschide "solvix-hub" si din alta parte.

  SCRIEREA NU ESTE ATOMICA. NVS confirma fiecare put in parte, deci o
  pana de curent in mijlocul unui store() lasa o identitate pe jumatate.
  Singura parghie disponibila: numarul de versiune se scrie ULTIMUL si se
  sterge PRIMUL, iar isPresent() cere si versiunea, si hubGuid, si apiKey.
  O identitate pe jumatate arata deci ca o identitate lipsa - adica se
  reia provisioning-ul, in loc sa se plece cu o cheie trunchiata.
*/

// Valorile de configurare trimise de server: 11 in obiectul "config" al
// provisioning-ului, 12 in raspunsul lui GET /api/device/config.
//
// DIN ELE SE FOLOSESC DOUA: heartbeatIntervalSeconds si
// heartbeatTimeoutSeconds, amandoua in HubHeartbeat - primul da ritmul
// batailor, al doilea spune dupa cat timp de tacere serverul ne considera
// cazuti. Niciunul nu este crezut pe cuvant: ritmul trece prin
// HEARTBEAT_MIN_INTERVAL_S / HEARTBEAT_MAX_INTERVAL_S din Config.h,
// fiindca un parametru primit prin retea nu are voie sa opreasca receptia
// radio.
//
// CELELALTE NOUA SE SALVEAZA SI NU SE FOLOSESC. Telemetria in loturi
// (cloudSyncIntervalSeconds, maxBatchSize) si actualizarea de firmware
// sunt etapa urmatoare; a implementa pe jumatate comportamente comandate
// de aici ar produce exact genul de purtare pe care nimeni nu o poate
// testa.
struct HubConfig {
  uint16_t heartbeatIntervalSeconds;
  uint16_t heartbeatTimeoutSeconds;
  uint16_t cloudSyncIntervalSeconds;
  uint16_t maxBatchSize;
  uint16_t offlineCleanupDays;
  uint16_t retryIntervalSeconds;
  uint16_t discoveryDurationSeconds;
  uint16_t configVersion;
  uint8_t  maxRetryAttempts;
  bool     offlineStorageEnabled;
  bool     autoFirmwareUpdate;

  /*
   * ADAUGAT LA COADA, SI ACOLO TREBUIE SA RAMANA.
   *
   * Campul asta nu vine din provisioning, ci doar din GET
   * /api/device/config, deci a aparut dupa ce existau deja hub-uri cu
   * identitatea salvata in NVS. Faptul ca sta ULTIMUL este ceea ce face
   * ca acele hub-uri sa NU trebuiasca sa se re-provizioneze:
   *
   *   Preferences::getBytes() citeste fara reproset un blob mai SCURT
   *   decat structura (esueaza doar cand e mai lung), iar loadFromNvs()
   *   face memset pe tot inainte. Blobul vechi de 20 de octeti intra deci
   *   peste primii 20, fiecare camp la offset-ul lui neschimbat, iar
   *   campul asta ramane 0 - exact ce inseamna "serverul nu mi-a spus
   *   inca". Prima actualizare de config ii da valoarea adevarata.
   *
   * De aceea IDENTITY_BLOB_VERSION a ramas 1. Un camp INSERAT la mijloc,
   * sau o reordonare, ar deplasa toate offset-urile de dupa el: blobul
   * vechi s-ar citi strambat, in tacere, si ar trebui crescuta versiunea
   * - adica exact re-provisioning-ul pe care Config.h avertizeaza sa nu
   * il declansezi cat timp idempotenta lui /api/device/provision nu e
   * confirmata. Camp nou = la coada.
   */
  uint16_t maxOfflineMessages;
};

struct HubIdentityData {
  /*
   * DIMENSIUNILE INCLUD TERMINATORUL. Un camp de [17] tine 16 caractere,
   * nu 17 - iar `strlcpy` din `HubCloud::doProvision()` verifica exact
   * asta si refuza sa salveze ceva ce nu incape, in loc sa trunchieze in
   * tacere. Un `apiKey` taiat nu autentifica nimic, iar simptomul ar
   * aparea abia peste saptamani.
   *
   * Verificarea aceea si-a facut treaba imediat: `lifecycleStatus` era
   * [17], adica 16 caractere utile, iar prima valoare reala trimisa de
   * server - "PendingActivation" - are fix 17. Provisioning-ul a fost
   * refuzat cu un mesaj care spunea exact ce si de ce.
   *
   * Cifrele de mai jos au deci marja scrisa alaturi, ca urmatoarea
   * valoare ceva mai lunga sa nu mai coste o rulare.
   */
  char hubGuid[37];          // UUID canonic = fix 36. Marja 0, dar formatul e fix
  char serialNumber[33];     // "PrimaV1HUB2026" = 14. Marja 18
  char apiKey[97];           // "svx_" + 32 hex + "_" + 43 = 80. Marja 16
  char pairingCode[17];      // "2SAP-87W7" = 9. Marja 7

  // Numele starii din ciclul de viata. Cel mai lung cunoscut,
  // "PendingActivation", are 17 caractere; 30 lasa loc pentru nume mai
  // descriptive fara sa mai fie nevoie de o recompilare.
  char lifecycleStatus[31];

  /*
   * ISO-8601 cu fractiuni de secunda.
   *
   * "2026-09-01T17:05:45.6069005Z" are 28 de caractere, deci in [33]
   * incapea - dar cu numai 4 de rezerva. Este prea putin: aceeasi data cu
   * un decalaj numeric in loc de "Z", adica
   * "2026-09-01T17:05:45.6069005+03:00", are 33 si NU ar mai fi incaput.
   * Serverul foloseste azi UTC, dar asta nu este o garantie scrisa
   * nicaieri, iar simptomul ar fi fost identic cu cel de la
   * lifecycleStatus: un provisioning refuzat, cu server si placa in
   * regula amandoua.
   */
  char provisionedAt[41];

  // Cati senzori spune SERVERUL ca poate tine hub-ul.
  //
  // NU inlocuieste HUB_MAX_SENSORS si nu dimensioneaza nimic. Marimea
  // registrului si ordinea din PROVISIONED_DEVICES_INIT traiesc in
  // Config.h, care este sursa unica de adevar pentru ele; de acolo se
  // deduce si REMOVE_CONFIRM_SILENCE_MS. O valoare venita prin retea nu
  // are voie sa devina o limita de tablou. Cand cele doua nu se
  // potrivesc, se spune zgomotos si se merge mai departe cu cea locala.
  uint8_t maxSensors;

  HubConfig config;
};

namespace HubIdentity {

  // Deschide NVS si incarca ce este salvat. Intoarce true daca a gasit o
  // identitate valida.
  bool begin();

  // Este hub-ul provizionat? Cere versiune corecta SI hubGuid SI apiKey.
  bool isProvisioned();

  const HubIdentityData& get();

  // Scrie identitatea in NVS. Versiunea se pune ultima, dinadins.
  bool store(const HubIdentityData& identity);

  /*
   * Inlocuieste DOAR blocul de configurare, pastrand identitatea neatinsa.
   * Pentru GET /api/device/config, care aduce configul nou fara sa spuna
   * nimic despre hubGuid, apiKey sau restul.
   *
   * Nu atinge KEY_VERSION: identitatea era deja completa si valida
   * inainte de apel si ramane asa. Disciplina "versiunea se scrie ultima"
   * apara scrierea unei identitati NOI de o pana de curent; aici nu se
   * scrie o identitate, ci se schimba un camp al uneia existente, iar
   * stergerea versiunii ar face exact raul de care ne temem - un hub
   * perfect provizionat care se trezeste crezandu-se gol.
   *
   * Un singur nvs_set_blob, deci cel mai rau caz al unei pene de curent
   * este configul vechi, nu unul pe jumatate.
   */
  bool storeConfig(const HubConfig& config);

}


// =====================================================================
//  HubCloud
// =====================================================================

/*
  HubCloud - pornirea in cloud: sanatatea serverului si provisioning-ul.
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
