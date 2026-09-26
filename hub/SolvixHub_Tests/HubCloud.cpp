/*
  HubCloud.cpp - pornirea in cloud.
  ---------------------------------------------------------------------
  Module, in ordinea dependentelor, fiecare in namespace-ul lui:
    HubIdentity  identitatea primita de la server, in NVS (solvix-hub)
    HubCloud     bootstrap-ul: health -> provision -> Ready
*/

#include "HubCloud.h"
#include "HubNet.h"
#include "HubSensors.h"
#include <Preferences.h>
#include <ArduinoJson.h>

// =====================================================================
//  HubIdentity
// =====================================================================

namespace HubIdentity {

  // Cheile NVS. Maximum 15 caractere fiecare, asa cere NVS.
  //
  // Cele sase siruri raman SIRURI, nu un blob: au lungimi variabile, se
  // pot citi la depanare cu nvs_get_str, iar lungimile lor pe server se
  // pot schimba (lifecycleStatus este un nume de stare care poate creste).
  // Cele 11 numere merg intr-un singur blob: sunt fixe, sosesc mereu
  // impreuna si costa astfel o scriere in loc de unsprezece.
  static const char* KEY_VERSION   = "ver";
  static const char* KEY_GUID      = "guid";
  static const char* KEY_SERIAL    = "serial";
  static const char* KEY_APIKEY    = "apikey";
  static const char* KEY_PAIRCODE  = "paircode";
  static const char* KEY_LIFECYCLE = "lifecycle";
  static const char* KEY_PROVAT    = "provat";
  static const char* KEY_MAXSENS   = "maxsens";
  static const char* KEY_CONFIG    = "cfg";

  static Preferences     s_prefs;
  static bool            s_open = false;
  static HubIdentityData s_data;

  const HubIdentityData& get() { return s_data; }

  bool isProvisioned() {
    return s_data.hubGuid[0] != '\0' && s_data.apiKey[0] != '\0';
  }

  // -------------------------------------------------------------------
  // NVS
  // -------------------------------------------------------------------

  static void loadFromNvs() {
    memset(&s_data, 0, sizeof(s_data));

    uint8_t version = s_prefs.getUChar(KEY_VERSION, 0);

    if (version != IDENTITY_BLOB_VERSION) {
      if (version != 0) {
        Serial.print(F("[HUB] Identitate salvata cu versiunea "));
        Serial.print(version);
        Serial.print(F(", firmware-ul cere "));
        Serial.print(IDENTITY_BLOB_VERSION);
        Serial.println(F(". Se ignora - hub-ul se va proviziona din nou."));
      }
      return;
    }

    s_prefs.getString(KEY_GUID,      s_data.hubGuid,         sizeof(s_data.hubGuid));
    s_prefs.getString(KEY_SERIAL,    s_data.serialNumber,    sizeof(s_data.serialNumber));
    s_prefs.getString(KEY_APIKEY,    s_data.apiKey,          sizeof(s_data.apiKey));
    s_prefs.getString(KEY_PAIRCODE,  s_data.pairingCode,     sizeof(s_data.pairingCode));
    s_prefs.getString(KEY_LIFECYCLE, s_data.lifecycleStatus, sizeof(s_data.lifecycleStatus));
    s_prefs.getString(KEY_PROVAT,    s_data.provisionedAt,   sizeof(s_data.provisionedAt));

    s_data.maxSensors = s_prefs.getUChar(KEY_MAXSENS, 0);
    s_prefs.getBytes(KEY_CONFIG, &s_data.config, sizeof(s_data.config));
  }

  bool begin() {
    s_open = s_prefs.begin(IDENTITY_NVS_NAMESPACE, false);

    if (!s_open) {
      Serial.println(F("[HUB] EROARE: nu s-a putut deschide spatiul NVS al identitatii."));
      memset(&s_data, 0, sizeof(s_data));
      return false;
    }

    loadFromNvs();

    if (isProvisioned()) {
      Serial.print(F("[HUB] Provizionat. GUID "));
      Serial.print(s_data.hubGuid);
      Serial.print(F(", stare "));
      Serial.println(s_data.lifecycleStatus);

      // Serverul si Config.h nu au de ce sa fie de acord, si daca nu
      // sunt, vrem sa se vada. Cifra locala ramane cea buna: din ea ies
      // marimea registrului si ferestrele de temporizare.
      if (s_data.maxSensors != 0 && s_data.maxSensors != HUB_MAX_SENSORS) {
        Serial.print(F("[HUB] ATENTIE: serverul spune maxSensors="));
        Serial.print(s_data.maxSensors);
        Serial.print(F(", firmware-ul are HUB_MAX_SENSORS="));
        Serial.print(HUB_MAX_SENSORS);
        Serial.println(F("."));
        Serial.println(F("      Se foloseste valoarea din Config.h: ea dimensioneaza registrul"));
        Serial.println(F("      si din ea se deduce REMOVE_CONFIRM_SILENCE_MS."));
      }
      return true;
    }

    Serial.println(F("[HUB] Neprovizionat. Se va cere /api/device/provision."));
    return false;
  }

  bool store(const HubIdentityData& identity) {
    if (!s_open) return false;

    /*
     * Versiunea se scrie ULTIMA. NVS confirma fiecare put in parte, deci
     * o pana de curent la mijloc lasa campuri pe jumatate scrise; daca
     * versiunea nu a apucat sa ajunga acolo, tot blocul este citit ca
     * inexistent la urmatoarea pornire si provisioning-ul se reia. Este
     * singura forma de atomicitate disponibila aici, si este suficienta.
     */
    s_prefs.remove(KEY_VERSION);

    s_prefs.putString(KEY_GUID,      identity.hubGuid);
    s_prefs.putString(KEY_SERIAL,    identity.serialNumber);
    s_prefs.putString(KEY_APIKEY,    identity.apiKey);
    s_prefs.putString(KEY_PAIRCODE,  identity.pairingCode);
    s_prefs.putString(KEY_LIFECYCLE, identity.lifecycleStatus);
    s_prefs.putString(KEY_PROVAT,    identity.provisionedAt);
    s_prefs.putUChar (KEY_MAXSENS,   identity.maxSensors);
    s_prefs.putBytes (KEY_CONFIG,    &identity.config, sizeof(identity.config));

    size_t written = s_prefs.putUChar(KEY_VERSION, IDENTITY_BLOB_VERSION);

    if (written == 0) {
      Serial.println(F("[HUB] EROARE: identitatea nu s-a putut scrie in NVS."));
      return false;
    }

    s_data = identity;
    return true;
  }

  bool storeConfig(const HubConfig& config) {
    if (!s_open) return false;

    /*
     * O identitate care nu exista nu are ce config sa primeasca. Fara
     * verificarea asta, un raspuns venit intr-un moment nefericit ar
     * scrie un bloc de configurare langa un KEY_VERSION inexistent, iar
     * la urmatoarea pornire ar fi oricum ignorat - dar in NVS ar ramane
     * un blob orfan care nu spune nimanui nimic.
     */
    if (!isProvisioned()) return false;

    size_t written = s_prefs.putBytes(KEY_CONFIG, &config, sizeof(config));

    if (written != sizeof(config)) {
      Serial.println(F("[HUB] EROARE: configul nou nu s-a putut scrie in NVS."));
      return false;
    }

    s_data.config = config;
    return true;
  }
}


// =====================================================================
//  HubCloud
// =====================================================================

namespace HubCloud {

  static const IPAddress CLOUD_IP(CLOUD_HOST_IP_0, CLOUD_HOST_IP_1,
                                  CLOUD_HOST_IP_2, CLOUD_HOST_IP_3);

  static const unsigned long BACKOFF_S[] = CLOUD_RETRY_BACKOFF_S;
  static const uint8_t BACKOFF_COUNT = sizeof(BACKOFF_S) / sizeof(BACKOFF_S[0]);

  /*
   * Tamponul raspunsului. Static, in .bss, DINADINS:
   *   - ArduinoJson v7 parseaza un char* mutabil fara sa copieze
   *     sirurile, deci tamponul asta chiar este memoria documentului si
   *     trebuie sa il supravietuiasca;
   *   - 1,5 kB pe stiva task-ului de loop (8 kB) ar fi o cheltuiala
   *     nesabuita pentru ceva folosit o data la 60 de secunde.
   */
  static char s_body[CLOUD_BODY_MAX];

  static State         s_state = State::NetWait;
  static unsigned long s_nextAttempt = 0;
  static unsigned long s_lastNag = 0;

  /*
   * DOUA contoare, nu unul.
   *
   * Cat timp au fost unul singur, backoff-ul nu a crescut niciodata:
   * fiecare verificare de sanatate reusita il punea pe zero, apoi esecul
   * de provisioning care urma imediat il facea 1, deci se alegea vesnic
   * a doua treapta - 10 secunde. Hub-ul reincerca la fiecare ~11 s la
   * nesfarsit. Pe un server care limiteaza dupa prea multe incercari
   * esuate, asta nu este doar zgomot: fiecare incercare hraneste exact
   * contorul care tine usa inchisa, deci hub-ul isi intretinea singur
   * blocajul si nu putea iesi niciodata din el.
   *
   * Sanatatea serverului si acceptarea unui provisioning sunt doua
   * lucruri diferite si esueaza din motive diferite. Isi numara separat
   * esecurile.
   */
  static uint8_t s_healthFailures = 0;
  static uint8_t s_provisionFailures = 0;

  // In ce stare se intra dupa ce se scurge asteptarea.
  static State s_retryInto = State::Health;

  // Ce s-a intamplat ultima data, pentru comanda `cloud`.
  static int           s_lastStatus = 0;
  static uint32_t      s_lastRetryAfterS = 0;
  static char          s_lastError[48] = "";

  State state() { return s_state; }

  const char* stateName() {
    switch (s_state) {
      case State::NetWait:       return "asteapta reteaua";
      case State::Health:        return "verifica serverul";
      case State::HealthBackoff: return "asteapta reincercarea";
      case State::Provision:     return "cere provisioning";
      case State::Ready:         return "gata";
      case State::Blocked:       return "BLOCAT";
    }
    return "?";
  }

  static void setError(const char* text) {
    strncpy(s_lastError, text, sizeof(s_lastError) - 1);
    s_lastError[sizeof(s_lastError) - 1] = '\0';
  }

  // -------------------------------------------------------------------
  // Backoff
  // -------------------------------------------------------------------

  /*
   * 5 / 10 / 30 / 60 s, apoi 60 s la nesfarsit.
   *
   * Nu plat la 60 s: un hub care porneste cu trei secunde inaintea
   * switch-ului nu are de ce sa astepte un minut intreg pentru nimic, iar
   * un server chiar cazut nu merita interogat des.
   */
  static void scheduleRetry(uint8_t failures, State next) {
    uint8_t index = (failures < BACKOFF_COUNT) ? failures : (uint8_t)(BACKOFF_COUNT - 1);
    unsigned long wait = BACKOFF_S[index];

    s_nextAttempt = millis() + wait * 1000UL;
    s_retryInto = next;
    s_state = State::HealthBackoff;

    Serial.print(F("[CLOUD] Reincerc peste "));
    Serial.print(wait);
    Serial.println(F(" s."));
  }

  /*
   * Pauza lunga dupa un 429. Se respecta Retry-After daca serverul l-a
   * trimis; altfel CLOUD_RATELIMIT_COOLDOWN_MS.
   *
   * Se reintra DIRECT in Provision, nu prin Health: stim deja ca serverul
   * este sanatos - tocmai ne-a raspuns - iar un GET in plus la fiecare
   * ciclu doar ar adauga trafic si ar tipari inca o data "server sanatos"
   * intr-un moment in care asta nu este informatia relevanta.
   */
  static void scheduleCooldown(uint32_t retryAfterS) {
    unsigned long wait = (retryAfterS != 0)
                       ? (unsigned long)retryAfterS * 1000UL
                       : CLOUD_RATELIMIT_COOLDOWN_MS;

    s_nextAttempt = millis() + wait;
    s_retryInto = State::Provision;
    s_state = State::HealthBackoff;

    Serial.print(F("[CLOUD] Server-ul ne-a limitat. Astept "));
    Serial.print(wait / 1000UL);
    Serial.println(F(" s inainte de urmatoarea incercare."));
    Serial.println(F("        Reincercarea deasa NU ajuta: fiecare incercare esuata hraneste"));
    Serial.println(F("        exact contorul dupa care serverul ne tine usa inchisa."));
  }

  // -------------------------------------------------------------------
  // Antetele comune
  // -------------------------------------------------------------------

  static const char* adminHeader() {
    // Static: sirul trebuie sa traiasca cat dureaza cererea.
    static const char header[] = CLOUD_ADMIN_KEY_HEADER ": " CLOUD_ADMIN_KEY;
    return header;
  }

  /*
   * La esec, serverul raspunde in format RFC 7807 (problem+json), cu un
   * camp "detail" care spune exact ce nu i-a placut - de exemplu
   * "Identitatea trimisa nu este valida sau device-ul nu poate fi
   * provisionat". Fara linia asta ar ramane doar "401", si diagnosticul
   * ar trebui refacut cu curl de pe alt calculator.
   *
   * Se afiseaza DOAR "title" si "detail", campuri pe care serverul le
   * scrie ca sa fie citite. Corpul brut nu se arunca pe Serial.
   */
  static void printProblemDetail(const Http::Result& result) {
    if (result.bodyLen == 0 || result.truncated) return;

    JsonDocument doc;
    if (deserializeJson(doc, s_body, result.bodyLen)) return;

    const char* title  = doc["title"]  | "";
    const char* detail = doc["detail"] | "";

    if (title[0] != '\0') {
      Serial.print(F("        Serverul spune: "));
      Serial.println(title);
    }
    if (detail[0] != '\0') {
      Serial.print(F("        "));
      Serial.println(detail);
    }
  }

  // -------------------------------------------------------------------
  // PASUL 2: GET /api/health
  // -------------------------------------------------------------------

  static bool doHealth() {
    Client* client = NetLink::acquireClient();
    if (client == NULL) {
      setError("fara adresa IP");
      return false;
    }

    const char* headers[] = { adminHeader() };
    Http::Result result;

    Serial.print(F("[CLOUD] GET "));
    Serial.print(F(CLOUD_PATH_HEALTH));
    Serial.println(F(" ..."));

    bool ok = Http::get(*client, CLOUD_IP, CLOUD_PORT, CLOUD_PATH_HEALTH,
                        headers, 1, s_body, sizeof(s_body), result);

    // stop() OBLIGATORIU, pe orice cale: EthernetENC are patru conexiuni
    // si nu le elibereaza singur.
    NetLink::releaseClient(client);

    s_lastStatus = result.status;
    s_lastRetryAfterS = result.retryAfterS;
    Http::printResult(result);

    if (!ok) {
      setError(Http::resultText(result.status));
      printProblemDetail(result);
      return false;
    }

    // Un corp trunchiat nu se parseaza. Un JSON taiat da de obicei
    // eroare, dar nu intotdeauna, iar o identitate salvata pe jumatate
    // este mult mai rea decat o reincercare curata.
    if (result.truncated) {
      setError("raspuns trunchiat");
      Serial.println(F("        Raspunsul nu a incaput in tampon. Creste CLOUD_BODY_MAX."));
      return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, s_body, result.bodyLen);

    if (error) {
      setError("JSON invalid");
      Serial.print(F("        JSON invalid: "));
      Serial.println(error.c_str());
      // NU se arunca tot corpul pe Serial: nu poti reda un camp pe care
      // nu ai reusit sa il parsezi. Doar cat sa se recunoasca forma.
      Serial.print(F("        Primii octeti: "));
      for (uint16_t i = 0; i < result.bodyLen && i < 40; i++) Serial.print(s_body[i]);
      Serial.println();
      return false;
    }

    const char* status   = doc["status"]   | "";
    const char* database = doc["database"] | "";

    Serial.print(F("        status=\""));
    Serial.print(status);
    Serial.print(F("\"  database=\""));
    Serial.print(database);
    Serial.println('"');

    /*
     * Baza de date este criteriul, nu "status". API-ul poate raspunde
     * perfect, cu status Healthy, si sa aiba baza de date cazuta - caz in
     * care provisioning-ul ar esua oricum, dar mai tarziu si mai confuz.
     */
    if (strcmp(database, "Reachable") != 0) {
      setError("baza de date nu raspunde");
      Serial.println(F("        Serverul raspunde, dar baza de date NU este accesibila."));
      return false;
    }

    setError("");
    return true;
  }

  // -------------------------------------------------------------------
  // PASUL 4: POST /api/device/provision
  // -------------------------------------------------------------------

  static bool doProvision() {
    Client* client = NetLink::acquireClient();
    if (client == NULL) {
      setError("fara adresa IP");
      return false;
    }

    // Corpul cererii. Construit cu ArduinoJson ca sa nu existe un al
    // doilea loc in care se scapa o ghilimea.
    JsonDocument request;
    request["deviceUid"]         = F(HUB_DEVICE_UID);
    request["serialNumber"]      = F(HUB_SERIAL_NUMBER);
    request["provisioningSecret"] = F(HUB_PROVISIONING_SECRET);
    request["firmwareVersion"]   = F(HUB_FIRMWARE_VERSION);

    char requestBody[256];
    size_t requestLen = serializeJson(request, requestBody, sizeof(requestBody));

    if (requestLen == 0 || requestLen >= sizeof(requestBody) - 1) {
      NetLink::releaseClient(client);
      setError("cerere prea lunga");
      return false;
    }

    const char* headers[1];
    uint8_t headerCount = 0;
#if CLOUD_PROVISION_SENDS_ADMIN_KEY
    headers[headerCount++] = adminHeader();
#endif

    Http::Result result;

    Serial.print(F("[CLOUD] POST "));
    Serial.print(F(CLOUD_PATH_PROVISION));
    Serial.println(F(" ..."));

    bool ok = Http::post(*client, CLOUD_IP, CLOUD_PORT, CLOUD_PATH_PROVISION,
                         headers, headerCount, requestBody, requestLen,
                         s_body, sizeof(s_body), result);

    NetLink::releaseClient(client);

    s_lastStatus = result.status;
    s_lastRetryAfterS = result.retryAfterS;
    Http::printResult(result);

    if (!ok) {
      setError(Http::resultText(result.status));
      printProblemDetail(result);

      if (result.status == 401 || result.status == 403) {
        Serial.println(F("        Autentificare respinsa. Verifica provisioningSecret si, daca"));
        Serial.println(F("        serverul chiar nu cere cheia de admin, CLOUD_PROVISION_SENDS_ADMIN_KEY."));
      }
      return false;
    }

    if (result.truncated) {
      setError("raspuns trunchiat");
      Serial.println(F("        Raspunsul nu a incaput in tampon. Creste CLOUD_BODY_MAX."));
      Serial.println(F("        NU se salveaza nimic: o identitate pe jumatate e mai rea decat una lipsa."));
      return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, s_body, result.bodyLen);

    if (error) {
      setError("JSON invalid");
      Serial.print(F("        JSON invalid: "));
      Serial.println(error.c_str());
      Serial.print(F("        Primii octeti: "));
      for (uint16_t i = 0; i < result.bodyLen && i < 40; i++) Serial.print(s_body[i]);
      Serial.println();
      return false;
    }

    HubIdentityData identity;
    memset(&identity, 0, sizeof(identity));

    /*
     * strlcpy si VERIFICAREA valorii intoarse. Un apiKey trunchiat in
     * tacere nu autentifica nimic, iar simptomul apare abia peste
     * saptamani, cand nimeni nu mai leaga cauza de provisioning.
     */
    struct { const char* key; char* dest; size_t size; bool required; } fields[] = {
      { "hubGuid",         identity.hubGuid,         sizeof(identity.hubGuid),         true  },
      { "serialNumber",    identity.serialNumber,    sizeof(identity.serialNumber),    false },
      { "apiKey",          identity.apiKey,          sizeof(identity.apiKey),          true  },
      { "pairingCode",     identity.pairingCode,     sizeof(identity.pairingCode),     false },
      { "lifecycleStatus", identity.lifecycleStatus, sizeof(identity.lifecycleStatus), false },
      { "provisionedAt",   identity.provisionedAt,   sizeof(identity.provisionedAt),   false },
    };

    for (uint8_t i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
      const char* value = doc[fields[i].key] | "";
      size_t needed = strlcpy(fields[i].dest, value, fields[i].size);

      if (needed >= fields[i].size) {
        setError("camp prea lung");
        Serial.print(F("        Campul '"));
        Serial.print(fields[i].key);
        Serial.print(F("' are "));
        Serial.print(needed);
        Serial.print(F(" caractere, dar incap "));
        Serial.print(fields[i].size - 1);
        Serial.println(F(". NU se salveaza nimic."));
        return false;
      }

      if (fields[i].required && fields[i].dest[0] == '\0') {
        setError("camp obligatoriu lipsa");
        Serial.print(F("        Raspunsul nu contine '"));
        Serial.print(fields[i].key);
        Serial.println(F("'. Serverul a schimbat formatul? NU se salveaza nimic."));
        return false;
      }
    }

    identity.maxSensors = doc["maxSensors"] | 0;

    JsonObject config = doc["config"];
    identity.config.heartbeatIntervalSeconds = config["heartbeatIntervalSeconds"] | 0;
    identity.config.heartbeatTimeoutSeconds  = config["heartbeatTimeoutSeconds"]  | 0;
    identity.config.cloudSyncIntervalSeconds = config["cloudSyncIntervalSeconds"] | 0;
    identity.config.maxBatchSize             = config["maxBatchSize"]             | 0;
    identity.config.offlineCleanupDays       = config["offlineCleanupDays"]       | 0;
    identity.config.retryIntervalSeconds     = config["retryIntervalSeconds"]     | 0;
    identity.config.discoveryDurationSeconds = config["discoveryDurationSeconds"] | 0;
    identity.config.configVersion            = config["configVersion"]            | 0;
    identity.config.maxRetryAttempts         = config["maxRetryAttempts"]         | 0;
    identity.config.offlineStorageEnabled    = config["offlineStorageEnabled"]    | false;
    identity.config.autoFirmwareUpdate       = config["autoFirmwareUpdate"]       | false;

    if (!HubIdentity::store(identity)) {
      setError("scrierea in NVS a esuat");
      return false;
    }

    setError("");
    Serial.println();
    Serial.println(F("[CLOUD] PROVISIONING REUSIT. Identitatea este salvata in flash;"));
    Serial.println(F("        la urmatoarea pornire hub-ul NU o va mai cere."));

    // Doar ce foloseste un om: identificatorul hub-ului si codul cu care
    // se revendica din aplicatie. apiKey nu se afiseaza niciodata, iar
    // restul se vede oricand cu `status`.
    Serial.print(F("        hub  "));
    Serial.print(identity.hubGuid);
    Serial.print(F("  ("));
    Serial.print(identity.lifecycleStatus);
    Serial.println(')');
    Serial.print(F("        cod de revendicare: "));
    Serial.println(identity.pairingCode);
    Serial.println();
    return true;
  }

  // -------------------------------------------------------------------
  // Masina de stari
  // -------------------------------------------------------------------

  void begin() {
    // Zero I/O aici. Tot ce inseamna retea se face in tick(), unde exista
    // portile care il tin departe de ferestrele de downlink.
    s_state = HubIdentity::isProvisioned() ? State::Ready : State::NetWait;
    s_nextAttempt = millis();
    s_retryInto = State::Health;
    s_healthFailures = 0;
    s_provisionFailures = 0;
    s_lastNag = millis();

    if (s_state == State::Ready) {
      Serial.println(F("[CLOUD] Hub provizionat din flash. Nu se cere nimic la pornire."));
    }
  }

  void tick() {
    if (s_state == State::Ready) return;

    /*
     * Blocked se trateaza aici, inaintea portilor: este doar o asteptare
     * lunga si nu costa decat o comparatie. Cand se scurge, se reintra in
     * Health cu contoarele pe zero - hub-ul isi revine singur, fara nicio
     * comanda, fiindca nu mai exista niciuna care sa il scoata de acolo.
     */
    if (s_state == State::Blocked) {
      if ((long)(millis() - s_nextAttempt) < 0) return;

      Serial.println(F("[CLOUD] A trecut pauza lunga. Incerc din nou."));
      s_healthFailures = 0;
      s_provisionFailures = 0;
      s_state = State::Health;
      return;
    }

    /*
     * PRIMA POARTA: nu se porneste nimic lung cat timp un senzor tocmai a
     * vorbit si isi tine fereastra de downlink deschisa. Aceea este
     * singura ocazie in care hub-ul ii poate raspunde, si se inchide
     * singura dupa 600 ms.
     */
    if (millis() - SensorLink::lastRxMs() < HTTP_QUIET_AFTER_RX_MS) return;

    /*
     * A DOUA POARTA: o dezinrolare in curs. RESET-ul trebuie sa prinda
     * fereastra senzorului marcat, altfel se repeta fundatura din F-031 -
     * senzor ramas in retea, registru blocat.
     */
    if (SensorLink::hasPendingRemoval()) return;

    // Reamintire, ca sa nu para ca hub-ul a uitat de el.
    if (!HubIdentity::isProvisioned() && millis() - s_lastNag >= CLOUD_NAG_EVERY_MS) {
      s_lastNag = millis();
      Serial.print(F("[CLOUD] Inca neprovizionat dupa "));
      Serial.print(millis() / 60000UL);
      Serial.print(F(" minute. Stare: "));
      Serial.print(stateName());
      if (s_lastError[0] != '\0') {
        Serial.print(F(" ("));
        Serial.print(s_lastError);
        Serial.print(')');
      }
      Serial.println();
    }

    switch (s_state) {

      case State::NetWait:
        if (!NetLink::isUp()) return;
        Serial.println(F("[CLOUD] Retea disponibila. Verific serverul."));
        s_state = State::Health;
        s_nextAttempt = millis();
        return;

      case State::HealthBackoff:
        if ((long)(millis() - s_nextAttempt) < 0) return;
        s_state = s_retryInto;
        return;

      case State::Health: {
        if (!NetLink::isUp()) {
          s_state = State::NetWait;
          return;
        }

        if (doHealth()) {
          // NUMAI contorul de sanatate. Cel de provisioning masoara alt
          // lucru si nu are voie sa fie sters de aici - vezi comentariul
          // de la declararea celor doua.
          s_healthFailures = 0;
          Serial.println(F("[CLOUD] Server sanatos, baza de date accesibila."));

          if (HubIdentity::isProvisioned()) {
            s_state = State::Ready;
            Serial.println(F("[CLOUD] Hub deja provizionat. Bootstrap incheiat."));
          } else {
            s_state = State::Provision;
          }
          return;
        }

        if (s_healthFailures < 255) s_healthFailures++;
        scheduleRetry(s_healthFailures, State::Health);
        return;
      }

      case State::Provision: {
        if (!NetLink::isUp()) {
          s_state = State::NetWait;
          return;
        }

        if (doProvision()) {
          s_healthFailures = 0;
          s_provisionFailures = 0;
          s_state = State::Ready;
          Serial.println(F("[CLOUD] Bootstrap incheiat."));
          return;
        }

        if (s_provisionFailures < 255) s_provisionFailures++;

        // 429 se trateaza separat de toate celelalte esecuri: nu spune
        // "mai incearca", spune "incetineste".
        if (s_lastStatus == 429) {
          scheduleCooldown(s_lastRetryAfterS);
          return;
        }

        /*
         * Dupa prea multe esecuri consecutive, hub-ul se opreste din
         * incercat. Un provisioning care pica de cinci ori la rand nu se
         * repara singur - secret gresit, device deja inregistrat, sau usa
         * inchisa de server - si toate trei cer un om. Pana atunci,
         * fiecare incercare in plus doar aduna esecuri in contul
         * device-ului.
         *
         * Radioul ramane pornit: senzorii se inroleaza si se citesc mai
         * departe. Se pierde raportarea in cloud, nu produsul.
         */
        if (s_provisionFailures >= CLOUD_PROVISION_MAX_ATTEMPTS) {
          s_state = State::Blocked;
          s_nextAttempt = millis() + CLOUD_BLOCKED_RETRY_MS;

          Serial.println();
          Serial.print(F("[CLOUD] Ma opresc dupa "));
          Serial.print(s_provisionFailures);
          Serial.println(F(" incercari de provisioning esuate la rand."));
          Serial.println(F("        Fiecare incercare in plus aduna un esec in contul device-ului,"));
          Serial.println(F("        fara sa apropie de nimic. Verifica, in ordine:"));
          Serial.println(F("          1. este deviceUid-ul deja provizionat pe server?"));
          Serial.println(F("          2. mai este valid provisioningSecret, sau a fost consumat?"));
          Serial.println(F("          3. s-a stins fereastra de rate limiting?"));
          Serial.print(F("        Reincerc singur peste "));
          Serial.print(CLOUD_BLOCKED_RETRY_MS / 60000UL);
          Serial.println(F(" minute."));
          Serial.println(F("        Senzorii merg mai departe normal - se pierde doar cloud-ul."));
          Serial.println();
          return;
        }

        // Se reia de la verificarea de sanatate, nu direct de la
        // provisioning: daca serverul a cazut intre timp, health o spune
        // mai clar si mai ieftin decat un POST esuat.
        scheduleRetry(s_provisionFailures, State::Health);
        return;
      }

      default:
        return;
    }
  }

  // -------------------------------------------------------------------
  // Pentru comanda `status`
  // -------------------------------------------------------------------

  const char* lastError() { return s_lastError; }
}
