#include "HubIdentity.h"
#include "Console.h"
#include <Preferences.h>

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
  // Mascarea secretelor
  // -------------------------------------------------------------------

  /*
   * Un secret se arata ca "bli0...aWr (43 caractere)": destul ca sa
   * distingi doua hub-uri intr-un jurnal, inutil pentru cineva care vrea
   * sa il foloseasca. Sub 12 caractere nu se arata nimic din el - la
   * lungimi mici, prefixul plus sufixul ar fi aproape tot secretul.
   */
  static void printMasked(const char* secret) {
    size_t len = strlen(secret);

    if (len == 0) {
      Serial.println(F("(gol)"));
      return;
    }

    if (len < 12) {
      Serial.print(F("(ascuns, "));
      Serial.print(len);
      Serial.println(F(" caractere)"));
      return;
    }

    for (uint8_t i = 0; i < 4; i++) Serial.print(secret[i]);
    Serial.print(F("..."));
    for (size_t i = len - 3; i < len; i++) Serial.print(secret[i]);
    Serial.print(F(" ("));
    Serial.print(len);
    Serial.println(F(" caractere)"));
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

  void clear() {
    if (!s_open) return;

    // Versiunea PRIMA, din acelasi motiv pentru care se scrie ultima.
    s_prefs.remove(KEY_VERSION);
    s_prefs.remove(KEY_GUID);
    s_prefs.remove(KEY_SERIAL);
    s_prefs.remove(KEY_APIKEY);
    s_prefs.remove(KEY_PAIRCODE);
    s_prefs.remove(KEY_LIFECYCLE);
    s_prefs.remove(KEY_PROVAT);
    s_prefs.remove(KEY_MAXSENS);
    s_prefs.remove(KEY_CONFIG);

    memset(&s_data, 0, sizeof(s_data));
  }

  // -------------------------------------------------------------------
  // Afisare
  // -------------------------------------------------------------------

  void print() {
    Serial.println();
    printSeparator();
    Serial.println(F("  IDENTITATEA HUB-ULUI"));
    printSeparator();

    Serial.println(F("Din firmware (Config.h):"));
    Serial.print(F("  id                 : ")); Serial.println(HUB_ID);
    Serial.print(F("  deviceUid          : ")); Serial.println(F(HUB_DEVICE_UID));
    Serial.print(F("  serialNumber       : ")); Serial.println(F(HUB_SERIAL_NUMBER));
    Serial.print(F("  firmwareVersion    : ")); Serial.println(F(HUB_FIRMWARE_VERSION));
    // provisioningSecret NU se afiseaza, nici macar lungimea: si lungimea
    // este un indiciu, iar secretul este oricum compilat si neschimbabil.
    Serial.println(F("  provisioningSecret : (compilat in firmware, nu se afiseaza)"));

    Serial.println();

    if (!isProvisioned()) {
      Serial.println(F("De la server: NIMIC - hub-ul nu este provizionat."));
      Serial.println(F("Se cere singur cand reteaua si serverul sunt disponibile."));
      Serial.println(F("Comanda 'cloud' arata la ce pas este."));
      Serial.println();
      return;
    }

    Serial.println(F("De la server (NVS):"));
    Serial.print(F("  hubGuid            : ")); Serial.println(s_data.hubGuid);
    Serial.print(F("  serialNumber       : ")); Serial.println(s_data.serialNumber);
    Serial.print(F("  apiKey             : ")); printMasked(s_data.apiKey);

    /*
     * pairingCode se afiseaza INTREG, spre deosebire de apiKey.
     *
     * Rostul lui este sa fie citit de un om de pe ecran si tastat in
     * aplicatie ca sa revendice hub-ul - de aceea are noua caractere si
     * o cratima la mijloc, nu patruzeci si trei. Mascat, nu si-ar mai
     * putea face treaba, si prima consecinta ar fi ca cineva l-ar citi
     * oricum, direct din NVS. apiKey este altceva: acela nu trebuie citit
     * de nimeni, niciodata.
     */
    Serial.print(F("  pairingCode        : ")); Serial.println(s_data.pairingCode);
    Serial.print(F("  lifecycleStatus    : ")); Serial.println(s_data.lifecycleStatus);
    Serial.print(F("  provisionedAt      : ")); Serial.println(s_data.provisionedAt);
    Serial.print(F("  maxSensors         : ")); Serial.print(s_data.maxSensors);
    Serial.print(F("   (local: "));
    Serial.print(HUB_MAX_SENSORS);
    Serial.println(F(")"));

    Serial.println();
    Serial.println(F("Config primit (salvat, dar inca nefolosit - etapa urmatoare):"));
    Serial.print(F("  heartbeatInterval  : ")); Serial.print(s_data.config.heartbeatIntervalSeconds); Serial.println(F(" s"));
    Serial.print(F("  heartbeatTimeout   : ")); Serial.print(s_data.config.heartbeatTimeoutSeconds);  Serial.println(F(" s"));
    Serial.print(F("  cloudSyncInterval  : ")); Serial.print(s_data.config.cloudSyncIntervalSeconds); Serial.println(F(" s"));
    Serial.print(F("  maxBatchSize       : ")); Serial.println(s_data.config.maxBatchSize);
    Serial.print(F("  offlineStorage     : ")); Serial.println(s_data.config.offlineStorageEnabled ? F("da") : F("nu"));
    Serial.print(F("  offlineCleanupDays : ")); Serial.println(s_data.config.offlineCleanupDays);
    Serial.print(F("  retryInterval      : ")); Serial.print(s_data.config.retryIntervalSeconds);     Serial.println(F(" s"));
    Serial.print(F("  maxRetryAttempts   : ")); Serial.println(s_data.config.maxRetryAttempts);
    Serial.print(F("  discoveryDuration  : ")); Serial.print(s_data.config.discoveryDurationSeconds); Serial.println(F(" s"));
    Serial.print(F("  configVersion      : ")); Serial.println(s_data.config.configVersion);
    Serial.print(F("  autoFirmwareUpdate : ")); Serial.println(s_data.config.autoFirmwareUpdate ? F("da") : F("nu"));
    Serial.println();
  }
}
