#include "HubHeartbeat.h"
#include "NetLink.h"
#include "Http.h"
#include "HubIdentity.h"
#include "HubCloud.h"
#include "SensorLink.h"
#include "DeviceRegistry.h"
#include <ArduinoJson.h>
#include <esp_timer.h>

namespace HubHeartbeat {

  static const IPAddress CLOUD_IP(CLOUD_HOST_IP_0, CLOUD_HOST_IP_1,
                                  CLOUD_HOST_IP_2, CLOUD_HOST_IP_3);

  static const unsigned long RETRY_S[] = HEARTBEAT_RETRY_BACKOFF_S;
  static const uint8_t RETRY_COUNT = sizeof(RETRY_S) / sizeof(RETRY_S[0]);

  /*
   * Tamponul raspunsului, static in .bss din aceleasi doua motive ca cel
   * din HubCloud: ArduinoJson v7 parseaza un char* mutabil FARA sa copieze
   * sirurile, deci tamponul chiar este memoria documentului si trebuie sa
   * ii supravietuiasca; iar o jumatate de kilooctet pe stiva task-ului de
   * loop ar fi o cheltuiala inutila pentru ceva chemat o data pe minut.
   */
  static char s_body[HEARTBEAT_BODY_MAX];

  /*
   * "X-Solvix-ApiKey: " + cheia + terminator, compus inaintea fiecarei
   * cereri. Static fiindca sirul trebuie sa traiasca cat dureaza cererea:
   * Http primeste un tablou de pointeri si nu copiaza nimic.
   *
   * CONTINUTUL LUI NU AJUNGE NICIODATA PE Serial (regula 11).
   */
  static char s_apiKeyHeader[sizeof(CLOUD_API_KEY_HEADER) + 2
                             + sizeof(HubIdentityData::apiKey)];

  // --- Ritmul, citit din identitate la prima intrare in Ready ---------
  static bool     s_armed = false;
  static uint16_t s_intervalS = 0;   // deja marginit de podeaua din Config.h
  static uint16_t s_timeoutS = 0;    // toleranta serverului; 0 = necunoscuta

  // --- Starea batailor ------------------------------------------------
  static unsigned long s_nextBeat = 0;
  static unsigned long s_lastOkMs = 0;
  static bool          s_everOk = false;
  static uint8_t       s_failures = 0;

  /*
   * Ce se raporteaza in campul cloudReachable al URMATOAREI batai: cum a
   * mers cea dinainte. Pare circular si nu este - serverul afla astfel ca
   * hub-ul a avut o perioada in care nu ajungea la el, chiar daca acum
   * ajunge. Porneste true fiindca in Ready se intra doar dupa un
   * /api/health reusit.
   */
  static bool s_cloudReachable = true;

  static int      s_lastStatus = 0;
  static uint32_t s_lastRetryAfterS = 0;
  static char     s_lastError[48] = "";

  // --- Ce a spus serverul ultima data ---------------------------------
  static uint16_t s_serverConfigVersion = 0;
  static bool     s_configUpdateRequired = false;
  static uint16_t s_pendingCommands = 0;

  // Raportari care se fac O SINGURA DATA la schimbare, nu la fiecare
  // bataie: altfel aceeasi linie s-ar repeta la fiecare minut, luni in
  // sir, si ar ineca exact jurnalul senzorilor.
  static bool     s_declaredOffline = false;
  static bool     s_configNagged = false;
  static uint16_t s_commandsNagged = 0;
  static bool     s_floorWarned = false;
  static bool     s_ceilingWarned = false;

  // --- Actualizarea configului ----------------------------------------
  static bool          s_configFetchPending = false;
  static unsigned long s_configFetchNext = 0;
  static uint8_t       s_configFetchFailures = 0;

  /*
   * GARDA IMPOTRIVA CICLULUI FARA CAPAT, si singurul motiv pentru care
   * exista perechea asta de variabile.
   *
   * Preluarea configului este declansata de configUpdateRequired din
   * raspunsul heartbeat-ului. Serverul stinge steagul la citire, deci in
   * mod normal se cere o singura data - dar daca vreodata nu il stinge,
   * sau daca uita sa incrementeze configVersion odata cu schimbarea,
   * hub-ul ar cere acelasi config la fiecare bataie, pentru totdeauna.
   *
   * Aici se retine versiunea PENTRU CARE s-a facut preluarea, nu cea
   * primita inapoi: daca serverul anunta v3 dar raspunde cu un config
   * marcat v2, garda tot tine. Cu versiunea primita, exact cazul acela ar
   * fi produs ciclul de care ne aparam.
   */
  static bool     s_configEverFetched = false;
  static uint16_t s_configFetchedVersion = 0;

  // -------------------------------------------------------------------
  // Ajutoare
  // -------------------------------------------------------------------

  static void setError(const char* text) {
    strncpy(s_lastError, text, sizeof(s_lastError) - 1);
    s_lastError[sizeof(s_lastError) - 1] = '\0';
  }

  /*
   * millis() se rastoarna dupa 49,7 zile, iar hub-ul asta trebuie sa
   * mearga luni fara repornire - deci rasturnarea nu este teoretica, este
   * programata. esp_timer_get_time() intoarce microsecundele de la
   * pornire pe 64 de biti, adica se rastoarna peste ~292.000 de ani:
   * problema dispare in loc sa fie administrata cu un contor de
   * rasturnari, care ar trebui chemat destul de des cat sa nu piarda una.
   */
  static uint32_t uptimeSeconds() {
    return (uint32_t)(esp_timer_get_time() / 1000000LL);
  }

  /*
   * Cati senzori s-au auzit de curand.
   *
   * Acelasi prag ca serviceOfflineWatch() din SensorLink, dar NU aceeasi
   * regula pentru senzorii nemaiauziti niciodata in sesiunea asta
   * (lastSeenMs == 0). Acolo sunt sariti dinadins, ca o repornire a
   * hub-ului sa nu declare toata reteaua cazuta; aici se numara ca
   * OFFLINE, din doua motive: catre server "nu l-am auzit" chiar inseamna
   * "nu pot confirma ca traieste", si numai asa online + offline da
   * sensorCount - trei numere care nu se aduna nu se pot afisa nicaieri.
   */
  static uint8_t countOnline() {
    uint8_t online = 0;

    for (uint8_t i = 0; i < DeviceRegistry::count(); i++) {
      DeviceRecord* device = DeviceRegistry::at(i);
      if (device == nullptr) continue;
      if (device->lastSeenMs == 0) continue;
      if (millis() - device->lastSeenMs < SENSOR_OFFLINE_MS) online++;
    }

    return online;
  }

  /*
   * Copie locala a lui printProblemDetail din HubCloud.cpp, care este
   * file-static acolo. Douazeci de linii duplicate sunt un pret mai mic
   * decat o functie publica noua pe HubCloud, adaugata doar ca sa fie
   * impartita.
   *
   * Merita duplicate: esecul de departe cel mai probabil aici este 401 cu
   * o cheie refuzata, iar campul "detail" al serverului spune exact de ce.
   * Fara el ar ramane un "401" gol, si diagnosticul ar trebui refacut cu
   * curl de pe alt calculator.
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
  // Programarea urmatoarei batai
  // -------------------------------------------------------------------

  static void noteFailure(const char* text) {
    setError(text);

    // Se raporteaza in bataia urmatoare, ca serverul sa stie ca a existat
    // o perioada in care nu ajungeam la el.
    s_cloudReachable = false;

    if (s_failures < 255) s_failures++;

    uint8_t index = (uint8_t)((s_failures - 1 < RETRY_COUNT)
                              ? (s_failures - 1) : (RETRY_COUNT - 1));
    unsigned long wait = RETRY_S[index];

    // Un 429 nu spune "mai incearca", spune "incetineste" - si de obicei
    // spune si cat. Se respecta, aceeasi lectie ca in HubCloud.
    if (s_lastStatus == 429 && s_lastRetryAfterS != 0) {
      wait = (unsigned long)s_lastRetryAfterS;
    }

    s_nextBeat = millis() + wait * 1000UL;

    Serial.print(F("[HB] Esuat ("));
    Serial.print(text);
    Serial.print(F("). Reincerc peste "));
    Serial.print(wait);
    Serial.println(F(" s."));
  }

  /*
   * Esec care nu a ajuns niciodata pana la o cerere HTTP: fara retea,
   * fara client, corp prea lung.
   *
   * Sterge intai statusul si Retry-After ale cererii DINAINTE. Fara asta,
   * un 429 vechi ar dicta pauza unui esec care nu are nicio legatura cu
   * el: un cablu de retea scos imediat dupa o limitare a serverului ar
   * pune hub-ul sa astepte cele cincisprezece minute ale lui 429 in loc
   * de zece secunde, iar la reconectare nimeni nu ar intelege de ce tace.
   */
  static void noteLocalFailure(const char* text) {
    s_lastStatus = 0;
    s_lastRetryAfterS = 0;
    noteFailure(text);
  }

  static void noteSuccess(uint32_t nextInS) {
    s_failures = 0;
    s_cloudReachable = true;
    s_lastOkMs = millis();
    s_everOk = true;
    setError("");

    unsigned long wait = s_intervalS;

    /*
     * nextHeartbeatInSeconds este singurul camp din raspuns pe care
     * hub-ul chiar il poate onora, deci se respecta - dar trece prin
     * aceeasi podea ca intervalul din config. Un server care ar cere
     * bataia peste o secunda nu ar obtine o raportare mai buna, ar obtine
     * un hub surd (vezi HEARTBEAT_MIN_INTERVAL_S in Config.h).
     */
    if (nextInS != 0) {
      wait = nextInS;

      if (wait < HEARTBEAT_MIN_INTERVAL_S) {
        if (!s_floorWarned) {
          s_floorWarned = true;
          Serial.print(F("[HB] Serverul cere bataia peste "));
          Serial.print(nextInS);
          Serial.print(F(" s; nu cobor sub "));
          Serial.print(HEARTBEAT_MIN_INTERVAL_S);
          Serial.println(F(" s."));
          Serial.println(F("     Cat dureaza o cerere hub-ul nu aude radioul, iar pachetele"));
          Serial.println(F("     pierdute atunci nu se recupereaza. Se spune o singura data."));
        }
        wait = HEARTBEAT_MIN_INTERVAL_S;
      }

      /*
       * Tavanul nu este simetrie de dragul simetriei: fara el, o valoare
       * mare inmultita cu 1000 se rastoarna, iar "pauza foarte lunga" ar
       * deveni "bataie la fiecare trecere prin loop()" - exact pe dos.
       */
      if (wait > HEARTBEAT_MAX_INTERVAL_S) {
        if (!s_ceilingWarned) {
          s_ceilingWarned = true;
          Serial.print(F("[HB] Serverul cere bataia abia peste "));
          Serial.print(nextInS);
          Serial.print(F(" s; nu urc peste "));
          Serial.print(HEARTBEAT_MAX_INTERVAL_S);
          Serial.println(F(" s. Se spune o singura data."));
        }
        wait = HEARTBEAT_MAX_INTERVAL_S;
      }
    }

    s_nextBeat = millis() + wait * 1000UL;
  }

  // -------------------------------------------------------------------
  // Pornirea, la prima intrare in HubCloud::State::Ready
  // -------------------------------------------------------------------

  /*
   * Preia ritmul dintr-un config si il margineste.
   *
   * Scoasa din arm() cand a aparut actualizarea de config, si SCOASA
   * DINADINS: cele doua cai - pornirea si un config nou sosit prin
   * GET /api/device/config - trebuie sa margineasca la fel si sa se
   * planga la fel. Duplicata, una dintre ele ar fi ramas in urma la prima
   * modificare, si tocmai calea rara ar fi fost cea nereparata.
   *
   * Nu programeaza nicio bataie: cine cheama decide cand urmeaza
   * urmatoarea.
   */
  static void applyRhythm(const HubConfig& config) {
    unsigned long interval = config.heartbeatIntervalSeconds;

    if (interval == 0) {
      interval = HEARTBEAT_DEFAULT_INTERVAL_S;
      Serial.print(F("[HB] Serverul nu a trimis heartbeatIntervalSeconds. Folosesc "));
      Serial.print(interval);
      Serial.println(F(" s."));
    }

    if (interval < HEARTBEAT_MIN_INTERVAL_S) {
      Serial.print(F("[HB] heartbeatIntervalSeconds = "));
      Serial.print(interval);
      Serial.print(F(" s este sub podeaua de "));
      Serial.print(HEARTBEAT_MIN_INTERVAL_S);
      Serial.println(F(" s. Bat mai rar."));
      Serial.println(F("     Un parametru primit prin retea nu are voie sa opreasca receptia."));
      interval = HEARTBEAT_MIN_INTERVAL_S;
    }

    s_intervalS = (uint16_t)interval;
    s_timeoutS  = config.heartbeatTimeoutSeconds;

    /*
     * Ritmul trebuie sa incapa in toleranta serverului. Daca nu incape,
     * hub-ul ar fi declarat offline INTRE doua batai perfect reusite, iar
     * din teren cauza ar fi de negasit: totul merge, si totusi aparatul
     * apare mort in aplicatie.
     *
     * NU SE CORECTEAZA SINGUR. Este exact cazul lui maxSensors din
     * HubIdentity.h: cand ce spune serverul nu se potriveste cu ce se
     * poate face aici, se spune ZGOMOTOS si se merge mai departe. O
     * valoare inventata local ar ascunde o neintelegere intre cele doua
     * capete, care trebuie reparata pe server, nu mascata pe placa.
     */
    if (s_timeoutS != 0 && s_intervalS >= s_timeoutS) {
      Serial.println();
      Serial.println(F("[HB] ATENTIE: intervalul de heartbeat NU incape in toleranta serverului."));
      Serial.print(F("     Bat la "));
      Serial.print(s_intervalS);
      Serial.print(F(" s, dar serverul ne considera cazuti dupa "));
      Serial.print(s_timeoutS);
      Serial.println(F(" s."));
      Serial.println(F("     Hub-ul va aparea offline INTRE doua batai reusite. Se repara in"));
      Serial.println(F("     configul de pe server - aici nu se inventeaza nimic."));
      Serial.println();
    }
  }

  static void arm() {
    applyRhythm(HubIdentity::get().config);

    Serial.print(F("[HB] Heartbeat pornit: la "));
    Serial.print(s_intervalS);
    Serial.print(F(" s"));
    if (s_timeoutS != 0) {
      Serial.print(F(", serverul ne asteapta in cel mult "));
      Serial.print(s_timeoutS);
      Serial.print(F(" s"));
    }
    Serial.println(F("."));

    // Punctul din care se masoara tacerea. Fara el, prima verificare ar
    // raporta hub-ul offline pentru timpul scurs cu bootstrap-ul.
    s_lastOkMs = millis();
    s_nextBeat = millis();
    s_armed = true;
  }

  // -------------------------------------------------------------------
  // "Serverul ne considera offline"
  // -------------------------------------------------------------------

  /*
   * A doua intrebuintare a lui heartbeatTimeoutSeconds. Nu face nicio
   * cerere: doar compara si spune, o data la cadere si o data la revenire,
   * ca serviceOfflineWatch() din SensorLink.
   *
   * Merita spusa fiindca este singura defectiune din tot lantul care este
   * complet INVIZIBILA de pe hub: senzorii se aud, LED-urile clipesc,
   * consola raspunde, si totusi in aplicatie aparatul apare mort.
   */
  static void serviceOfflineSelfCheck() {
    if (s_timeoutS == 0) return;

    bool late = (millis() - s_lastOkMs) >= (unsigned long)s_timeoutS * 1000UL;

    if (late && !s_declaredOffline) {
      s_declaredOffline = true;
      Serial.println();
      Serial.print(F(">> SERVERUL NE CONSIDERA OFFLINE: niciun heartbeat reusit de "));
      Serial.print((millis() - s_lastOkMs) / 1000UL);
      Serial.println(F(" s,"));
      Serial.print(F(">> iar toleranta lui este de "));
      Serial.print(s_timeoutS);
      Serial.println(F(" s. Continui sa incerc."));
      Serial.println(F(">> Senzorii merg mai departe normal - se pierde doar raportarea."));
      Serial.println();
    }
    else if (!late && s_declaredOffline) {
      s_declaredOffline = false;
      Serial.println();
      Serial.println(F(">> HEARTBEAT REVENIT: serverul ne vede din nou."));
      Serial.println();
    }
  }

  // -------------------------------------------------------------------
  // Ce a cerut serverul in raspuns
  // -------------------------------------------------------------------

  /*
   * Ce a cerut serverul in raspunsul la heartbeat.
   *
   * Configul SE IA: se programeaza un GET /api/device/config. Comenzile
   * doar se raporteaza - pentru ele inca nu exista endpoint, iar un
   * comportament implementat pe jumatate ar fi unul pe care nimeni nu il
   * poate testa.
   */
  static void reportServerRequests() {
    if (s_configUpdateRequired) {

      // Prima oara cand se anunta versiunea asta: se cere. Vezi garda de
      // la declararea lui s_configFetchedVersion.
      if (!s_configEverFetched || s_configFetchedVersion != s_serverConfigVersion) {
        if (!s_configFetchPending) {
          s_configFetchPending = true;
          s_configFetchFailures = 0;

          /*
           * NU imediat, ci peste o fereastra de liniste.
           *
           * Functia asta este chemata la sfarsitul unei batai, adica la
           * cateva milisecunde dupa o cerere HTTP care tocmai s-a
           * terminat. Programata pe `millis()`, preluarea configului ar
           * porni la urmatoarea trecere prin loop() - deci doua cereri
           * blocante practic lipite, si pana la cinci secunde de surzenie
           * la rand. Regula "cel mult o cerere per tick" nu apara de asta:
           * tick-urile sunt la milisecunde distanta.
           *
           * Portile se reevalueaza oricum inaintea celei de-a doua cereri,
           * dar ele se uita in TRECUT, la ultimul pachet auzit. Pauza asta
           * lasa si un senzor care tocmai urmeaza sa emita sa fie auzit
           * intre cele doua.
           */
          s_configFetchNext = millis() + HTTP_QUIET_AFTER_RX_MS;

          Serial.print(F("[HB] Serverul are config nou (v"));
          Serial.print(s_serverConfigVersion);
          Serial.print(F("), noi avem v"));
          Serial.print(HubIdentity::get().config.configVersion);
          Serial.println(F("). Il cer."));
        }
      }
      else if (!s_configNagged) {
        s_configNagged = true;
        Serial.println();
        Serial.print(F("[HB] Serverul cere in continuare config nou pentru v"));
        Serial.print(s_serverConfigVersion);
        Serial.println(F(", pe care am luat-o deja."));
        Serial.println(F("     NU o mai cer inca o data: ar fi un ciclu fara capat. Ori serverul"));
        Serial.println(F("     nu stinge configUpdateRequired la citire, ori nu a incrementat"));
        Serial.println(F("     configVersion odata cu schimbarea. Se spune o singura data."));
        Serial.println();
      }
    }
    else {
      s_configNagged = false;
    }

    if (s_pendingCommands != 0 && s_pendingCommands != s_commandsNagged) {
      s_commandsNagged = s_pendingCommands;
      Serial.print(F("[HB] Serverul are "));
      Serial.print(s_pendingCommands);
      Serial.println(F(" comanda/comenzi in asteptare pentru hub."));
      Serial.println(F("     Nu exista inca endpoint din care sa fie luate."));
    }
    else if (s_pendingCommands == 0) {
      s_commandsNagged = 0;
    }
  }

  // -------------------------------------------------------------------
  // O bataie
  // -------------------------------------------------------------------

  static void sendBeat() {
    Client* client = NetLink::acquireClient();
    if (client == NULL) {
      noteLocalFailure("fara adresa IP");
      return;
    }

    const HubIdentityData& identity = HubIdentity::get();
    uint8_t enrolled = DeviceRegistry::count();
    uint8_t online   = countOnline();

    // Corpul, construit cu ArduinoJson ca sa nu existe un al doilea loc in
    // proiect in care se scapa o ghilimea.
    JsonDocument request;

    // --- Ce se masoara cu adevarat -----------------------------------
    request["firmwareVersion"] = F(HUB_FIRMWARE_VERSION);
    request["uptimeSeconds"]   = uptimeSeconds();

    /*
     * PRESUPUNERE: "avem o adresa IP utilizabila" se raporteaza ca
     * internetAvailable. Nu este acelasi lucru - un DHCP reusit intr-o
     * retea fara iesire ar da tot true - dar hub-ul nu poate distinge
     * fara inca o cerere catre exterior, care l-ar face surd degeaba.
     * cloudReachable de mai jos este masura onesta.
     */
    request["internetAvailable"] = NetLink::isUp();
    request["cloudReachable"]    = s_cloudReachable;

    request["connectionType"]     = F(HEARTBEAT_CONNECTION_TYPE);
    request["sensorCount"]        = enrolled;
    request["onlineSensorCount"]  = online;
    request["offlineSensorCount"] = (uint8_t)(enrolled - online);
    request["freeMemoryBytes"]    = ESP.getFreeHeap();
    request["configVersion"]      = identity.config.configVersion;

    /*
     * --- Ce NU exista pe placa asta ----------------------------------
     *
     * Campurile de mai jos sunt in schema serverului, dar hub-ul nu are
     * hardware pentru ele. Se trimit cu valori fixe, si fiecare isi poarta
     * presupunerea aici (regula 6.2.5 din CLAUDE.md). O cifra plauzibila
     * si inventata - o tensiune de baterie, o temperatura de cip - este
     * mai rea decat un zero: zero se vede ca zero, o cifra frumoasa ajunge
     * intr-un grafic si e crezuta.
     *
     * PRESUPUNERE (cablaj): hub-ul este alimentat DOAR din sursa
     * principala. Nu are baterie de rezerva si nu are cum sa masoare
     * caderea alimentarii - la o cadere se opreste, deci nu ar avea nici
     * cine sa o raporteze. mainPowerAvailable este deci true prin
     * constructie: daca acest pachet a plecat, alimentarea exista. Cand se
     * adauga bateria pe placa, aici se schimba patru linii si in Config.h
     * apare pinul de masura.
     */
    request["mainPowerAvailable"]   = true;
    request["backupBatteryActive"]  = false;
    request["backupBatteryLevel"]   = 0;
    request["backupBatteryVoltage"] = 0;

    /*
     * PRESUPUNERE (masurabil, dar nu de incredere): ESP32 clasic are un
     * senzor intern de temperatura, insa temperatureRead() intoarce pe
     * multe exemplare aceeasi valoare fixa (~53 C) indiferent de mediu. O
     * cifra constanta care arata ca o masuratoare este mai rea decat
     * niciuna, deci se trimite 0 pana cand exista un senzor adevarat.
     */
    request["cpuTemperature"] = 0;

    // Flash-ul liber nu se poate exprima onest intr-un singur numar:
    // partitia de sketch, NVS si SPIFFS sunt spatii diferite, iar niciunul
    // nu creste si nu scade in rulare pe hub-ul asta.
    request["freeStorageBytes"] = 0;

    // Nu exista coada de mesaje: masuratorile se trimit cand vin, sau se
    // pierd. Cand va exista - cloudSyncIntervalSeconds si maxBatchSize
    // sunt deja in config, tot nefolosite - aici intra lungimea ei.
    request["pendingMessageCount"] = 0;

    // Ethernet nu are RSSI. Ramane 0 pana la ramura de WiFi din NetLink.
    request["wifiSignalStrength"] = 0;

    // Hub-ul isi stie doar adresa din LAN, data de DHCP. Adresa publica o
    // stie doar cineva din afara - de exemplu chiar serverul, care vede
    // sursa conexiunii. Un IP privat trimis ca "public" ar fi o minciuna
    // curata, deci se trimite gol.
    request["publicIp"] = "";

    // Camp liber, fara continut agreat. Ramane gol cat timp nu exista
    // nimic de spus in el: un JSON inghesuit intr-un sir nu se citeste de
    // nicaieri si nu se valideaza nicaieri.
    request["payloadJson"] = "";

    char requestBody[HEARTBEAT_REQUEST_MAX];
    size_t requestLen = serializeJson(request, requestBody, sizeof(requestBody));

    if (requestLen == 0 || requestLen >= sizeof(requestBody) - 1) {
      NetLink::releaseClient(client);
      noteLocalFailure("cerere prea lunga");
      Serial.println(F("     Creste HEARTBEAT_REQUEST_MAX in Config.h."));
      return;
    }

    /*
     * Antetul de autentificare, si singurul lucru din toata cererea care
     * spune serverului CINE bate - corpul nu contine niciun identificator
     * de hub.
     *
     * snprintf, nu strcat: cheia vine din NVS, deci este date, nu un
     * literal, si nu are voie sa curga peste tampon daca serverul incepe
     * vreodata sa emita chei mai lungi.
     */
    snprintf(s_apiKeyHeader, sizeof(s_apiKeyHeader),
             "%s: %s", CLOUD_API_KEY_HEADER, identity.apiKey);

    const char* headers[] = { s_apiKeyHeader };

    Http::Result result;

    bool ok = Http::post(*client, CLOUD_IP, CLOUD_PORT, CLOUD_PATH_HEARTBEAT,
                         headers, 1, requestBody, requestLen,
                         s_body, sizeof(s_body), result);

    /*
     * stop() OBLIGATORIU pe orice cale: EthernetENC are patru conexiuni
     * (UIP_CONNS) si nu le elibereaza singur. O scapare aici, la o bataie
     * pe minut, ar lasa hub-ul definitiv fara retea in patru minute - si
     * tacut.
     */
    NetLink::releaseClient(client);

    s_lastStatus      = result.status;
    s_lastRetryAfterS = result.retryAfterS;

    if (!ok) {
      Http::printResult(result);
      printProblemDetail(result);

      if (result.status == 401 || result.status == 403) {
        Serial.println(F("        Cheia hub-ului a fost refuzata. Ori identitatea din flash nu mai"));
        Serial.println(F("        este valida pe server, ori endpoint-ul asteapta alt antet."));
      }

      noteFailure(Http::resultText(result.status));
      return;
    }

    /*
     * De aici incolo cererea A REUSIT: statusul este 2xx, deci serverul
     * ne-a inregistrat bataia. Ce urmeaza este doar citirea a ce ne-a
     * raspuns, iar un raspuns pe care nu il putem citi NU transforma o
     * bataie reusita intr-una esuata - ar insemna sa ne declaram singuri
     * offline cu serverul multumit.
     */
    uint32_t nextInS = 0;

    if (result.truncated) {
      Serial.println(F("[HB] Raspuns trunchiat; bataia a fost inregistrata, dar nu il pot citi."));
      Serial.println(F("     Creste HEARTBEAT_BODY_MAX in Config.h."));
    }
    else if (result.bodyLen == 0) {
      // Corp gol la 2xx: serverul nu are nimic de cerut. Perfect valid.
    }
    else {
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, s_body, result.bodyLen);

      if (error) {
        Serial.print(F("[HB] Raspuns cu JSON invalid ("));
        Serial.print(error.c_str());
        Serial.println(F("); bataia a fost totusi inregistrata."));
        Serial.print(F("     Primii octeti: "));
        for (uint16_t i = 0; i < result.bodyLen && i < 40; i++) Serial.print(s_body[i]);
        Serial.println();
      }
      else {
        s_serverConfigVersion  = doc["configVersion"]          | (uint16_t)0;
        s_configUpdateRequired = doc["configUpdateRequired"]   | false;
        s_pendingCommands      = doc["pendingCommandCount"]    | (uint16_t)0;
        nextInS                = doc["nextHeartbeatInSeconds"] | (uint32_t)0;
      }
    }

    noteSuccess(nextInS);

    /*
     * O SINGURA linie pe bataie reusita; Http::printResult() ramane pentru
     * esecuri. La o bataie pe minut, doua linii de fiecare data ar ineca
     * jurnalul senzorilor - si acela este cel pentru care se sta cu ochii
     * pe Serial.
     */
    Serial.print(F("[HB] ok in "));
    Serial.print(result.elapsedMs);
    Serial.print(F(" ms. Uptime "));
    Serial.print(uptimeSeconds());
    Serial.print(F(" s, senzori "));
    Serial.print(online);
    Serial.print(F("/"));
    Serial.print(enrolled);
    Serial.print(F(" online. Urmatorul peste "));
    Serial.print((s_nextBeat - millis()) / 1000UL);
    Serial.println(F(" s."));

    reportServerRequests();
  }

  // -------------------------------------------------------------------
  // GET /api/device/config
  // -------------------------------------------------------------------

  /*
   * Un config care nu s-a putut lua NU este o bataie esuata: hub-ul este
   * viu, serverul il vede, doar ca a ramas pe configul vechi - care este
   * complet functional, fiindca este chiar cel dupa care merge acum.
   *
   * De aceea contorul si programarea sunt separate de ale batailor, si de
   * aceea nu se atinge s_cloudReachable: ar fi o minciuna pe dos, un hub
   * care raporteaza ca nu ajunge la cloud in timp ce tocmai a batut cu
   * succes.
   */
  static void noteConfigFailure(const char* text) {
    if (s_configFetchFailures < 255) s_configFetchFailures++;

    uint8_t index = (uint8_t)((s_configFetchFailures - 1 < RETRY_COUNT)
                              ? (s_configFetchFailures - 1) : (RETRY_COUNT - 1));
    unsigned long wait = RETRY_S[index];

    s_configFetchNext = millis() + wait * 1000UL;

    Serial.print(F("[HB] Configul nu s-a putut lua ("));
    Serial.print(text);
    Serial.print(F("). Raman pe cel vechi, reincerc peste "));
    Serial.print(wait);
    Serial.println(F(" s."));
  }

  static void doFetchConfig() {
    Client* client = NetLink::acquireClient();
    if (client == NULL) {
      noteConfigFailure("fara adresa IP");
      return;
    }

    const HubIdentityData& identity = HubIdentity::get();

    // Aceeasi cheie ca la heartbeat, si tot ea este si identificatorul:
    // cererea nu are corp si nu poarta niciun hubGuid.
    snprintf(s_apiKeyHeader, sizeof(s_apiKeyHeader),
             "%s: %s", CLOUD_API_KEY_HEADER, identity.apiKey);

    const char* headers[] = { s_apiKeyHeader };

    Http::Result result;

    Serial.print(F("[HB] GET "));
    Serial.print(F(CLOUD_PATH_CONFIG));
    Serial.println(F(" ..."));

    bool ok = Http::get(*client, CLOUD_IP, CLOUD_PORT, CLOUD_PATH_CONFIG,
                        headers, 1, s_body, sizeof(s_body), result);

    NetLink::releaseClient(client);

    if (!ok) {
      Http::printResult(result);
      printProblemDetail(result);

      if (result.status == 401 || result.status == 403) {
        Serial.println(F("        Aceeasi cheie ca la heartbeat a fost refuzata aici. Daca"));
        Serial.println(F("        bataile trec si asta nu, endpoint-ul cere altceva."));
      }

      noteConfigFailure(Http::resultText(result.status));
      return;
    }

    // Un corp taiat nu se parseaza. Configul vechi merge; unul citit pe
    // jumatate ar putea sa nu mearga, si ar ajunge salvat in NVS.
    if (result.truncated) {
      Serial.println(F("        Raspunsul nu a incaput in tampon. Creste HEARTBEAT_BODY_MAX."));
      noteConfigFailure("raspuns trunchiat");
      return;
    }

    if (result.bodyLen == 0) {
      noteConfigFailure("raspuns gol");
      return;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, s_body, result.bodyLen);

    if (error) {
      Serial.print(F("        JSON invalid: "));
      Serial.println(error.c_str());
      Serial.print(F("        Primii octeti: "));
      for (uint16_t i = 0; i < result.bodyLen && i < 40; i++) Serial.print(s_body[i]);
      Serial.println();
      noteConfigFailure("JSON invalid");
      return;
    }

    /*
     * Se pleaca de la configul CURENT, nu de la zero: un camp care
     * lipseste din raspuns isi pastreaza astfel valoarea de acum, in loc
     * sa devina 0. Diferenta conteaza - un heartbeatIntervalSeconds
     * ajuns 0 fiindca serverul a omis campul ar arunca hub-ul pe ritmul
     * implicit fara ca nimeni sa fi cerut asta.
     */
    HubConfig fresh = identity.config;

    fresh.heartbeatIntervalSeconds = doc["heartbeatIntervalSeconds"] | fresh.heartbeatIntervalSeconds;
    fresh.heartbeatTimeoutSeconds  = doc["heartbeatTimeoutSeconds"]  | fresh.heartbeatTimeoutSeconds;
    fresh.cloudSyncIntervalSeconds = doc["cloudSyncIntervalSeconds"] | fresh.cloudSyncIntervalSeconds;
    fresh.maxBatchSize             = doc["maxBatchSize"]             | fresh.maxBatchSize;
    fresh.offlineStorageEnabled    = doc["offlineStorageEnabled"]    | fresh.offlineStorageEnabled;
    fresh.maxOfflineMessages       = doc["maxOfflineMessages"]       | fresh.maxOfflineMessages;
    fresh.offlineCleanupDays       = doc["offlineCleanupDays"]       | fresh.offlineCleanupDays;
    fresh.retryIntervalSeconds     = doc["retryIntervalSeconds"]     | fresh.retryIntervalSeconds;
    fresh.maxRetryAttempts         = doc["maxRetryAttempts"]         | fresh.maxRetryAttempts;
    fresh.discoveryDurationSeconds = doc["discoveryDurationSeconds"] | fresh.discoveryDurationSeconds;
    fresh.configVersion            = doc["configVersion"]            | fresh.configVersion;
    fresh.autoFirmwareUpdate       = doc["autoFirmwareUpdate"]       | fresh.autoFirmwareUpdate;

    uint16_t oldVersion  = identity.config.configVersion;
    uint16_t oldInterval = s_intervalS;
    uint16_t oldTimeout  = s_timeoutS;

    /*
     * SE SALVEAZA INAINTE SA SE APLICE. Daca scrierea in NVS pica, hub-ul
     * ramane in intregime pe configul vechi - si in RAM, si in flash. Un
     * ritm nou aplicat peste un config nesalvat ar disparea la prima
     * repornire, iar simptomul ar fi un hub care isi schimba singur
     * ritmul inapoi fara ca nimic sa se fi intamplat.
     */
    if (!HubIdentity::storeConfig(fresh)) {
      noteConfigFailure("scrierea in NVS a esuat");
      return;
    }

    s_configFetchPending = false;
    s_configFetchFailures = 0;
    s_configEverFetched = true;
    s_configFetchedVersion = s_serverConfigVersion;
    s_configUpdateRequired = false;
    s_configNagged = false;

    applyRhythm(fresh);

    /*
     * Urmatoarea bataie se reprogrameaza DE ACUM, cu ritmul nou. Nu se
     * bate imediat: tocmai s-a facut o cerere, iar a doua la rand ar
     * dubla degeaba fereastra in care hub-ul e surd. Bataia urmatoare
     * confirma oricum serverului ca s-a trecut pe configul nou.
     */
    s_nextBeat = millis() + (unsigned long)s_intervalS * 1000UL;

    Serial.print(F("[HB] Config nou aplicat: v"));
    Serial.print(oldVersion);
    Serial.print(F(" -> v"));
    Serial.print(fresh.configVersion);
    Serial.print(F(". Ritm "));
    Serial.print(oldInterval);
    Serial.print(F(" -> "));
    Serial.print(s_intervalS);
    Serial.print(F(" s, toleranta "));
    Serial.print(oldTimeout);
    Serial.print(F(" -> "));
    Serial.print(s_timeoutS);
    Serial.println(F(" s."));

    /*
     * Serverul a anuntat o versiune si a livrat alta. Nu este o eroare
     * care sa opreasca ceva - configul primit este cel bun, e singurul pe
     * care il avem - dar este o nepotrivire intre doua raspunsuri ale
     * aceluiasi server, si aia se spune.
     */
    if (fresh.configVersion != s_serverConfigVersion) {
      Serial.print(F("     ATENTIE: heartbeat-ul anuntase v"));
      Serial.print(s_serverConfigVersion);
      Serial.print(F(", dar /api/device/config a livrat v"));
      Serial.print(fresh.configVersion);
      Serial.println(F("."));
    }

    // Restul valorilor s-au salvat, dar nu comanda nimic pe hub. Se spune
    // ca sa nu para ca au fost aplicate.
    Serial.println(F("     Din config se folosesc doar cele doua de heartbeat; celelalte zece"));
    Serial.println(F("     s-au salvat in NVS si asteapta telemetria."));
  }

  // -------------------------------------------------------------------
  // Suprafata publica
  // -------------------------------------------------------------------

  void begin() {
    /*
     * Zero I/O si zero citiri din identitate: la momentul asta hub-ul
     * poate fi inca neprovizionat, deci configul nici nu exista. Ritmul se
     * afla in arm(), la prima intrare in Ready.
     */
    s_armed = false;
    s_everOk = false;
    s_failures = 0;
    s_cloudReachable = true;
    s_declaredOffline = false;
    s_configNagged = false;
    s_commandsNagged = 0;
    s_floorWarned = false;
    s_ceilingWarned = false;
    s_lastStatus = 0;
    s_lastRetryAfterS = 0;

    /*
     * Garda pe versiune se reia de la zero la fiecare pornire, dinadins:
     * ea apara de un ciclu de cereri INTR-O SESIUNE, iar dupa o repornire
     * configul din NVS poate fi oricum invechit. Prima bataie spune daca
     * este, si atunci se cere din nou - o data.
     */
    s_configFetchPending = false;
    s_configFetchFailures = 0;
    s_configEverFetched = false;
    s_configFetchedVersion = 0;

    setError("");
  }

  void tick() {
    /*
     * Heartbeat-ul incepe unde se termina bootstrap-ul. Inainte de Ready
     * nu exista nici identitate, nici config, nici certitudinea ca
     * serverul raspunde.
     */
    if (HubCloud::state() != HubCloud::State::Ready) return;
    if (!HubIdentity::isProvisioned()) return;

    if (!s_armed) arm();

    // Nu face I/O si nu are de ce sa astepte o fereastra linistita: doar
    // compara doua numere.
    serviceOfflineSelfCheck();

    bool configDue = s_configFetchPending &&
                     (long)(millis() - s_configFetchNext) >= 0;
    bool beatDue   = (long)(millis() - s_nextBeat) >= 0;

    if (!configDue && !beatDue) return;

    /*
     * ACELEASI DOUA PORTI CA IN HubCloud::tick(), si din aceleasi motive.
     *
     * PRIMA: senzorul isi tine fereastra de downlink deschisa 600 ms dupa
     * ce a emis, si aceea este singura ocazie in care hub-ul ii poate
     * raspunde. O cerere pornita fix atunci i-o mananca.
     *
     * A DOUA: o dezinrolare in curs. RESET-ul trebuie sa prinda fereastra
     * senzorului marcat, altfel se repeta fundatura din F-031.
     *
     * O bataie scadenta nu se pierde daca prinde un moment prost: se
     * incearca din nou la urmatoarea trecere prin loop(), peste cateva
     * milisecunde.
     */
    if (millis() - SensorLink::lastRxMs() < HTTP_QUIET_AFTER_RX_MS) return;
    if (SensorLink::hasPendingRemoval()) return;

    if (!NetLink::isUp()) {
      // Se pune pe seama a ceea ce era scadent. Daca erau amandoua, amandoua
      // au esuat - fiecare cu contorul si programarea ei.
      if (configDue) noteConfigFailure("fara adresa IP");
      if (beatDue)   noteLocalFailure("fara adresa IP");
      return;
    }

    /*
     * CEL MULT O CERERE PER TICK, chiar daca amandoua sunt scadente.
     *
     * Doua cereri blocante una dupa alta ar dubla fereastra in care
     * hub-ul este surd, si ar face-o exact in momentul cel mai prost:
     * imediat dupa ce s-a redeschis dupa portile de mai sus.
     *
     * Configul are prioritate. Este evenimentul rar - se intampla o data
     * la fiecare schimbare de configurare, nu o data pe minut - si o
     * bataie amanata cu o trecere prin loop() nu costa nimic, in timp ce
     * un config amanat tine hub-ul pe un ritm despre care serverul stie
     * deja ca nu mai e cel bun.
     */
    if (configDue) {
      doFetchConfig();
      return;
    }

    sendBeat();
  }

  // -------------------------------------------------------------------
  // Pentru comanda `status`
  // -------------------------------------------------------------------

  void printStatus() {
    Serial.print(F("Puls    : "));

    if (!s_armed) {
      Serial.println(F("inca nu bate (bootstrap-ul in cloud nu s-a incheiat)"));
      return;
    }

    Serial.print(F("la "));
    Serial.print(s_intervalS);
    Serial.print(F(" s"));

    if (!s_everOk) {
      Serial.print(F(", niciunul reusit inca"));
    } else {
      Serial.print(F(", ultimul acum "));
      Serial.print((millis() - s_lastOkMs) / 1000UL);
      Serial.print(F(" s"));
    }

    if (s_lastError[0] != '\0') {
      Serial.print(F("  ["));
      Serial.print(s_lastError);
      Serial.print(F("]"));
    }

    if (s_declaredOffline) Serial.print(F("  SERVERUL NE CONSIDERA OFFLINE"));

    Serial.println();

    // A doua linie: versiunea configului dupa care merge hub-ul, si ce
    // mai are serverul de spus.
    Serial.print(F("          config v"));
    Serial.print(HubIdentity::get().config.configVersion);

    if (s_configFetchPending) {
      Serial.print(F(", il iau pe v"));
      Serial.print(s_serverConfigVersion);
      if (s_configFetchFailures != 0) {
        Serial.print(F(" ("));
        Serial.print(s_configFetchFailures);
        Serial.print(F(" incercari esuate)"));
      }
    }
    else if (s_configUpdateRequired) {
      // Ramas aprins desi nu mai cerem nimic: garda anti-ciclu a taiat
      // preluarea fiindca versiunea asta a fost deja luata.
      Serial.print(F(", serverul cere v"));
      Serial.print(s_serverConfigVersion);
      Serial.print(F(" - deja luata, nu o mai cer"));
    }

    if (s_pendingCommands != 0) {
      Serial.print(F("; "));
      Serial.print(s_pendingCommands);
      Serial.print(F(" comenzi in asteptare"));
    }

    Serial.println();
  }
}
