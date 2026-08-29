#include "DeviceRegistry.h"
#include <Preferences.h>

// Lista de provisioning. VALORILE stau in Config.h
// (PROVISIONED_DEVICES_INIT); aici se face doar instantierea, fiindca
// Config.h nu are un .cpp propriu.
static const ProvisionedDevice PROVISIONED_DEVICES[] = PROVISIONED_DEVICES_INIT;
static const uint8_t PROVISIONED_COUNT =
    sizeof(PROVISIONED_DEVICES) / sizeof(PROVISIONED_DEVICES[0]);

// Pozitia din tabel devine DevAddr, deci un tabel mai lung decat
// numerotarea ar produce senzori carora nu li se poate aloca niciun
// numar. Se prinde la compilare, nu la prima inrolare esuata.
static_assert(sizeof(PROVISIONED_DEVICES) / sizeof(PROVISIONED_DEVICES[0])
                  <= HUB_MAX_SENSORS,
              "PROVISIONED_DEVICES_INIT are mai multe randuri decat "
              "HUB_MAX_SENSORS: ultimele placi nu ar primi niciun numar.");

namespace DeviceRegistry {

  // Se schimba ori de cate ori se modifica structura DeviceRecord. Un
  // blob salvat cu alta versiune este ignorat si registrul porneste gol,
  // in loc sa fie interpretat gresit octet cu octet.
  // v2: DeviceRecord a primit resetAttempts si resetSentMs, pentru
  //     dezinrolarea confirmata din F-031.
  // v3: DeviceRecord a primit lostPackets, lastTempX100, lastRssi,
  //     hasReading si offlineReported, pentru vederea pe mai multi
  //     senzori (tabelul `sensors` si contorul de pachete pierdute).
  //     PRETUL, platit o singura data: la primul boot cu versiunea asta
  //     registrul porneste gol, iar senzorii deja inrolati continua sa
  //     emita cu sesiunile din HEF si apar ca "DevAddr ... nu este
  //     inrolat". Fiecare trebuie reinrolat o data, manual.
  static const uint8_t REGISTRY_BLOB_VERSION = 3;

  static const char* KEY_VERSION = "ver";
  static const char* KEY_COUNT   = "count";
  static const char* KEY_BLOB    = "devices";

  static Preferences   s_prefs;
  static bool          s_open = false;
  static DeviceRecord  s_devices[REGISTRY_MAX_DEVICES];
  static uint8_t       s_count = 0;

  static bool sameEui(const uint8_t* a, const uint8_t* b) {
    return memcmp(a, b, DEV_EUI_LEN) == 0;
  }

  bool begin() {
    // false = read/write. Numele spatiului are cel mult 15 caractere.
    s_open = s_prefs.begin(REGISTRY_NVS_NAMESPACE, false);
    if (!s_open) {
      Serial.println(F("EROARE: nu s-a putut deschide NVS pentru registru."));
      return false;
    }
    return load();
  }

  uint8_t count() { return s_count; }

  DeviceRecord* at(uint8_t index) {
    if (index >= s_count) return nullptr;
    return &s_devices[index];
  }

  DeviceRecord* findByEui(const uint8_t* devEui) {
    for (uint8_t i = 0; i < s_count; i++) {
      if (sameEui(s_devices[i].devEui, devEui)) return &s_devices[i];
    }
    return nullptr;
  }

  DeviceRecord* findByAddr(uint8_t devAddr) {
    for (uint8_t i = 0; i < s_count; i++) {
      if (s_devices[i].devAddr == devAddr) return &s_devices[i];
    }
    return nullptr;
  }

  const uint8_t* findAppKey(const uint8_t* devEui) {
    for (uint8_t i = 0; i < PROVISIONED_COUNT; i++) {
      if (sameEui(PROVISIONED_DEVICES[i].devEui, devEui)) {
        return PROVISIONED_DEVICES[i].appKey;
      }
    }
    return nullptr;
  }

  uint8_t provisionedCount() { return PROVISIONED_COUNT; }

  const uint8_t* provisionedEui(uint8_t index) {
    if (index >= PROVISIONED_COUNT) return nullptr;
    return PROVISIONED_DEVICES[index].devEui;
  }

  uint8_t addressForEui(const uint8_t* devEui) {
    for (uint8_t i = 0; i < PROVISIONED_COUNT; i++) {
      if (!sameEui(PROVISIONED_DEVICES[i].devEui, devEui)) continue;

      // Pozitia in tabel, plus unu. Verificarea de mai jos este
      // redundanta cu static_assert-ul de sus, dar costa un octet si
      // acopera cazul in care cineva schimba doar HUB_MAX_SENSORS.
      if (i >= HUB_MAX_SENSORS) return 0;
      return (uint8_t)(i + 1);
    }
    return 0;
  }

  DeviceRecord* add(const uint8_t* devEui, uint8_t devAddr,
                    const uint8_t* sessKey, uint16_t devNonce) {
    DeviceRecord* record = findByEui(devEui);

    if (record == nullptr) {
      if (s_count >= REGISTRY_MAX_DEVICES) return nullptr;
      record = &s_devices[s_count];
      s_count++;
      memset(record, 0, sizeof(DeviceRecord));
      memcpy(record->devEui, devEui, DEV_EUI_LEN);
    }

    // O re-inrolare inseamna sesiune noua: contoarele o iau de la capat,
    // exact ca pe senzor, care isi pune frameCounter pe 0 dupa join.
    record->devAddr = devAddr;
    memcpy(record->sessKey, sessKey, CRYPTO_KEY_LEN);
    record->lastFrameCounterUp = 0;
    record->hasUplink = false;
    record->downCounter = 0;
    record->lastDevNonce = devNonce;
    record->packets = 0;
    record->lostPackets = 0;
    record->pendingReset = false;
    record->resetAttempts = 0;
    record->resetSentMs = 0;
    record->lastSeenMs = millis();
    record->lastTempX100 = 0;
    record->lastRssi = 0;
    record->hasReading = false;
    record->offlineReported = false;

    save();
    return record;
  }

  bool removeByEui(const uint8_t* devEui) {
    for (uint8_t i = 0; i < s_count; i++) {
      if (!sameEui(s_devices[i].devEui, devEui)) continue;

      // Golul se umple mutand ultimul element pe pozitia eliberata:
      // ordinea din registru nu inseamna nimic. Numarul senzorului NU
      // depinde de ea - vine din tabelul de provisioning, nu de aici.
      if (i != (uint8_t)(s_count - 1)) {
        s_devices[i] = s_devices[s_count - 1];
      }
      s_count--;
      save();
      return true;
    }
    return false;
  }

  bool save() {
    if (!s_open) return false;

    s_prefs.putUChar(KEY_VERSION, REGISTRY_BLOB_VERSION);
    s_prefs.putUChar(KEY_COUNT, s_count);

    size_t bytes = (size_t)s_count * sizeof(DeviceRecord);
    size_t written = s_prefs.putBytes(KEY_BLOB, s_devices, bytes);

    return written == bytes;
  }

  bool load() {
    if (!s_open) return false;

    s_count = 0;

    uint8_t version = s_prefs.getUChar(KEY_VERSION, 0);
    if (version != REGISTRY_BLOB_VERSION) {
      // Registru absent sau salvat de o versiune veche a structurii.
      return true;
    }

    uint8_t stored = s_prefs.getUChar(KEY_COUNT, 0);
    if (stored > REGISTRY_MAX_DEVICES) stored = REGISTRY_MAX_DEVICES;

    size_t expected = (size_t)stored * sizeof(DeviceRecord);
    if (expected == 0) return true;

    size_t read = s_prefs.getBytes(KEY_BLOB, s_devices, expected);
    if (read != expected) {
      Serial.println(F("ATENTIE: registrul din NVS nu se potriveste ca marime; il ignor."));
      return false;
    }

    s_count = stored;

    // Campurile relative la millis() nu mai inseamna nimic dupa
    // repornire. Pentru resetSentMs asta nu este doar curatenie: 0
    // inseamna "niciun RESET trimis in sesiunea asta", iar verificarea de
    // tacere refuza sa confirme o dezinrolare pe baza unui RESET pe care
    // nu l-a trimis ea (F-031). Fara zeroizare, millis() mic minus o
    // valoare veche mare ar da o diferenta uriasa si device-ul ar
    // disparea din registru imediat dupa fiecare repornire a hub-ului.
    //
    // Ultima masuratoare se sterge din acelasi motiv de onestitate: o
    // temperatura salvata acum trei saptamani nu are ce cauta in
    // coloana "ultima citire" a tabelului `sensors`.
    for (uint8_t i = 0; i < s_count; i++) {
      s_devices[i].lastSeenMs = 0;
      s_devices[i].resetSentMs = 0;
      s_devices[i].hasReading = false;
      s_devices[i].lastTempX100 = 0;
      s_devices[i].lastRssi = 0;
      s_devices[i].offlineReported = false;
    }

    return true;
  }

  void clear() {
    s_count = 0;
    if (s_open) {
      s_prefs.clear();
    }
  }

  // -------------------------------------------------------------------
  // Afisare
  // -------------------------------------------------------------------

  // "#3" - numarul senzorului, adica DevAddr.
  static void printNumber(uint8_t devAddr) {
    Serial.print('#');
    Serial.print(devAddr);
  }

  static void printAgeOrDash(uint32_t lastSeenMs) {
    if (lastSeenMs == 0) {
      Serial.print(F("   -"));
      return;
    }
    uint32_t seconds = (millis() - lastSeenMs) / 1000UL;
    if (seconds < 10)   Serial.print(F("  "));
    else if (seconds < 100) Serial.print(' ');
    Serial.print(seconds);
    Serial.print('s');
  }

  void printAll() {
    Serial.println();
    Serial.print(F("Senzori inrolati: "));
    Serial.print(s_count);
    Serial.print(F(" din "));
    Serial.println(HUB_MAX_SENSORS);

    if (s_count == 0) {
      Serial.println(F("  (registrul este gol - foloseste 'pair' ca sa inrolezi unul)"));
      return;
    }

    for (uint8_t i = 0; i < s_count; i++) {
      const DeviceRecord& d = s_devices[i];

      Serial.print(F("  Senzor "));
      printNumber(d.devAddr);

      Serial.print(F("  DevEUI "));
      SensorPacketCodec::printEui(d.devEui);

      Serial.print(F("  DevAddr 0x"));
      if (d.devAddr < 0x10) Serial.print('0');
      Serial.print(d.devAddr, HEX);

      Serial.print(F("  pachete "));
      Serial.print(d.packets);

      Serial.print(F("  pierdute "));
      Serial.print(d.lostPackets);

      Serial.print(F("  ultimul counter "));
      if (d.hasUplink) Serial.print(d.lastFrameCounterUp);
      else             Serial.print(F("-"));

      if (d.lastSeenMs != 0) {
        Serial.print(F("  vazut acum "));
        Serial.print((millis() - d.lastSeenMs) / 1000UL);
        Serial.print(F(" s"));
      } else {
        Serial.print(F("  nevazut de la pornire"));
      }

      if (d.pendingReset) {
        Serial.print(F("  [DEZINROLARE IN CURS, RESET-uri trimise: "));
        Serial.print(d.resetAttempts);
        if (d.resetSentMs == 0) {
          Serial.print(F(", niciunul in sesiunea asta - astept un pachet"));
        } else {
          Serial.print(F(", ultimul acum "));
          Serial.print((millis() - d.resetSentMs) / 1000UL);
          Serial.print(F(" s"));
        }
        Serial.print(F("]"));
      }

      Serial.println();
    }
  }

  /*
   * Tabelul comenzii `sensors`.
   *
   * Se parcurge NUMEROTAREA, nu registrul: sunt afisate toate cele
   * HUB_MAX_SENSORS locuri, in ordinea numerelor, si cele goale sunt
   * aratate ca atare. Cu cinci placi in teren, intrebarea de zi cu zi nu
   * este "ce contine registrul", ci "care dintre cele cinci lipseste" -
   * iar la aceea o lista care sare peste locurile libere nu raspunde.
   */
  void printSensorTable() {
    Serial.println();
    Serial.println(F("=================================================================="));
    Serial.print(F("  SENZORI  ("));
    Serial.print(s_count);
    Serial.print(F(" inrolati din "));
    Serial.print(HUB_MAX_SENSORS);
    Serial.println(F(" locuri)"));
    Serial.println(F("=================================================================="));
    Serial.println(F("   #  DevEUI            temperatura   varsta  RSSI   pach.  pierd."));
    Serial.println(F("  ----------------------------------------------------------------"));

    for (uint8_t number = 1; number <= HUB_MAX_SENSORS; number++) {
      Serial.print(F("  "));
      if (number < 10) Serial.print(' ');
      printNumber(number);
      Serial.print(' ');

      DeviceRecord* d = findByAddr(number);

      if (d == nullptr) {
        const uint8_t* eui = provisionedEui((uint8_t)(number - 1));
        if (eui == nullptr) {
          Serial.println(F(" (niciun senzor provizionat pe acest numar)"));
        } else {
          Serial.print(' ');
          SensorPacketCodec::printEui(eui);
          Serial.println(F("  NEINROLAT - 'pair' pe hub + butonul 2 pe senzor"));
        }
        continue;
      }

      Serial.print(' ');
      SensorPacketCodec::printEui(d->devEui);
      Serial.print(F("  "));

      if (!d->hasReading) {
        Serial.print(F("       -  "));
      } else if (d->lastTempX100 == SENSOR_TEMP_INVALID) {
        Serial.print(F("  EROARE  "));
      } else {
        float celsius = d->lastTempX100 / 100.0f;
        if (celsius >= 0 && celsius < 10) Serial.print(' ');
        if (celsius > -10 && celsius < 100) Serial.print(' ');
        Serial.print(celsius, 2);
        Serial.print(F(" C  "));
      }

      printAgeOrDash(d->lastSeenMs);
      Serial.print(F("  "));

      if (d->hasReading) {
        Serial.print(d->lastRssi);
      } else {
        Serial.print(F("   -"));
      }

      Serial.print(F("  "));
      Serial.print(d->packets);
      Serial.print(F("  "));
      Serial.print(d->lostPackets);

      if (d->pendingReset) {
        Serial.print(F("   [DEZINROLARE IN CURS]"));
      } else if (d->offlineReported) {
        Serial.print(F("   [NU SE MAI AUDE]"));
      }

      Serial.println();
    }

    Serial.println(F("  ----------------------------------------------------------------"));
    Serial.println(F("  'pierd.' = goluri in frame counter, adica pachete care nu au ajuns:"));
    Serial.println(F("  coliziuni intre senzori sau acoperire slaba. Cateva la mii de"));
    Serial.println(F("  pachete sunt normale; o crestere continua pe UN singur senzor"));
    Serial.println(F("  inseamna semnal slab, iar pe DOI in acelasi timp inseamna ca se"));
    Serial.println(F("  ciocnesc intre ei."));
    Serial.println(F("=================================================================="));
  }

  void printProvisioned() {
    Serial.println();
    Serial.print(F("Senzori care AU VOIE sa se inroleze (Config.h): "));
    Serial.println(PROVISIONED_COUNT);
    Serial.println(F("Numarul senzorului este chiar pozitia din acest tabel."));

    for (uint8_t i = 0; i < PROVISIONED_COUNT; i++) {
      Serial.print(F("  Senzor "));
      printNumber((uint8_t)(i + 1));
      Serial.print(F("  DevEUI "));
      SensorPacketCodec::printEui(PROVISIONED_DEVICES[i].devEui);
      Serial.print(F("  (SENSOR_NODE_ID = "));
      Serial.print(i + 1);
      Serial.print(F(" pe placa)"));
      Serial.println(findByEui(PROVISIONED_DEVICES[i].devEui) != nullptr
                     ? F("  - inrolat") : F("  - neinrolat"));
    }
  }
}
