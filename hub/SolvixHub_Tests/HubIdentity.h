/*
  HubIdentity.h - identitatea hub-ului, primita de la server si tinuta in
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

#ifndef HUB_IDENTITY_H
#define HUB_IDENTITY_H

#include <Arduino.h>
#include "Config.h"

// Cele 11 valori de configurare trimise de server, in obiectul "config".
//
// SE SALVEAZA, DAR NU SE FOLOSESC INCA. Heartbeat-ul si telemetria sunt
// etapa urmatoare; a implementa pe jumatate comportamente comandate de
// aici ar produce exact genul de purtare pe care nimeni nu o poate testa.
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

  // Sterge identitatea din NVS. Versiunea se sterge prima.
  void clear();

  // Afiseaza identitatea pe Serial, CU SECRETELE MASCATE (regula 12 din
  // CLAUDE.md). apiKey si pairingCode apar ca prefix + sufix + lungime;
  // provisioningSecret nu apare deloc, nici macar ca lungime.
  void print();
}

#endif // HUB_IDENTITY_H
