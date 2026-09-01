#include "HubCloud.h"
#include "Console.h"
#include "NetLink.h"
#include "Http.h"
#include "HubIdentity.h"
#include "SensorLink.h"
#include <ArduinoJson.h>

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
  static unsigned long s_lastOkMs = 0;
  static bool          s_everHealthy = false;
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

    s_everHealthy = true;
    s_lastOkMs = millis();
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
    HubIdentity::print();
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
    if (s_state == State::Ready || s_state == State::Blocked) {
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
          Serial.println();
          Serial.print(F("[CLOUD] OPRIT dupa "));
          Serial.print(s_provisionFailures);
          Serial.println(F(" incercari de provisioning esuate la rand."));
          Serial.println(F("        Nu mai incerc singur: fiecare incercare in plus aduna un esec"));
          Serial.println(F("        in contul device-ului, fara sa apropie de nimic."));
          Serial.println(F("        Verifica, in ordine:"));
          Serial.println(F("          1. este deviceUid-ul deja provizionat pe server?"));
          Serial.println(F("          2. mai este valid provisioningSecret, sau a fost consumat?"));
          Serial.println(F("          3. s-a stins fereastra de rate limiting?"));
          Serial.println(F("        Cand stii raspunsul: comanda 'provision' reia de la capat."));
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
  // Comenzi
  // -------------------------------------------------------------------

  void forceHealth() {
    if (!NetLink::isUp()) {
      Serial.println(F("Nu exista legatura la retea. Incearca intai 'net'."));
      return;
    }

    if (millis() - SensorLink::lastRxMs() < HTTP_QUIET_AFTER_RX_MS) {
      Serial.println(F("Un senzor tocmai a emis si isi tine fereastra de downlink deschisa."));
      Serial.println(F("Astept sa se inchida si pornesc cererea imediat dupa."));
    }

    // Cererea propriu-zisa ramane in tick(), unde trec portile.
    s_healthFailures = 0;
    s_nextAttempt = millis();
    if (s_state != State::Provision) s_state = State::Health;
  }

  void forceProvision() {
    if (HubIdentity::isProvisioned()) {
      Serial.println(F("Hub-ul este DEJA provizionat. Nu se cere din nou."));
      Serial.println(F("Un al doilea provisioning pentru acelasi deviceUid poate crea un hub nou"));
      Serial.println(F("pe server si poate lasa istoricul vechi orfan - depinde daca endpoint-ul"));
      Serial.println(F("este idempotent, ceea ce nu este confirmat."));
      Serial.println(F("Daca chiar vrei asta: 'forget yes', apoi 'provision'."));
      return;
    }

    if (!NetLink::isUp()) {
      Serial.println(F("Nu exista legatura la retea. Incearca intai 'net'."));
      return;
    }

    // Comanda data de om sterge si oprirea din Blocked: el a vazut
    // motivul si a decis sa mai incerce o data.
    s_healthFailures = 0;
    s_provisionFailures = 0;
    s_nextAttempt = millis();
    s_retryInto = State::Health;
    s_state = State::Health;      // sanatatea intai, provisioning-ul dupa
    Serial.println(F("Pornesc secventa: sanatate -> provisioning."));
  }

  void printStatus() {
    Serial.println();
    printSeparator();
    Serial.println(F("  CLOUD"));
    printSeparator();

    Serial.print(F("Server      : "));
    Serial.print(CLOUD_IP);
    Serial.print(':');
    Serial.println(CLOUD_PORT);

    Serial.print(F("Stare       : "));
    Serial.println(stateName());

    Serial.print(F("Provizionat : "));
    Serial.println(HubIdentity::isProvisioned() ? F("da") : F("NU"));

    Serial.print(F("Ultimul HTTP: "));
    if (s_lastStatus == 0) {
      Serial.println(F("nicio cerere inca"));
    } else {
      Serial.print(s_lastStatus);
      Serial.print(F(" ("));
      Serial.print(Http::resultText(s_lastStatus));
      Serial.println(')');
    }

    Serial.print(F("Sanatate OK : "));
    if (!s_everHealthy) {
      Serial.println(F("niciodata in sesiunea asta"));
    } else {
      Serial.print(F("acum "));
      Serial.print((millis() - s_lastOkMs) / 1000UL);
      Serial.println(F(" s"));
    }

    Serial.print(F("Esecuri     : "));
    Serial.print(s_healthFailures);
    Serial.print(F(" la sanatate, "));
    Serial.print(s_provisionFailures);
    Serial.print(F(" la provisioning (din "));
    Serial.print(CLOUD_PROVISION_MAX_ATTEMPTS);
    Serial.println(F(" permise)"));

    if (s_lastRetryAfterS != 0) {
      Serial.print(F("Retry-After : "));
      Serial.print(s_lastRetryAfterS);
      Serial.println(F(" s (cerut de server)"));
    }

    if (s_lastError[0] != '\0') {
      Serial.print(F("Ultima eroare: "));
      Serial.println(s_lastError);
    }

    if (s_state == State::HealthBackoff) {
      long remaining = (long)(s_nextAttempt - millis());
      Serial.print(F("Reincercare : peste "));
      Serial.print(remaining > 0 ? (remaining / 1000L) : 0L);
      Serial.println(F(" s"));
    }

    Serial.println();
  }
}
