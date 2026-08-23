#include "TestPairing.h"

// esp_random() este generatorul hardware al ESP32. In nucleele mai noi
// (ESP-IDF 5) locuieste in esp_random.h; in cele vechi era expus prin
// esp_system.h. Verificarea de mai jos merge in ambele cazuri.
#if __has_include(<esp_random.h>)
  #include <esp_random.h>
#else
  #include <esp_system.h>
#endif

namespace TestPairing {

  // Cat timp de liniste inainte de un mesaj de semn de viata.
  static const unsigned long HEARTBEAT_MS = 15000;

  // Ceva mai mare decat cel mai lung pachet al protocolului (DATA_ENC,
  // 17 octeti), ca un pachet prea lung sa fie citit si aratat, nu taiat
  // in tacere.
  static const int RX_BUFFER_SIZE = 32;

  static bool s_ready = false;

  static bool          s_pairingMode = false;
  static unsigned long s_pairingUntil = 0;
  static unsigned long s_lastBlink = 0;
  static bool          s_blinkOn = false;

  static unsigned long s_lastActivity = 0;

  // Contoare, toate afisate de `stats`.
  static unsigned long s_joinsAccepted = 0;
  static unsigned long s_joinsRejected = 0;
  static unsigned long s_dataValid = 0;
  static unsigned long s_dataReplay = 0;
  static unsigned long s_dataBadMic = 0;
  static unsigned long s_dataUnknown = 0;
  static unsigned long s_foreign = 0;

  // Cate pachete de date au venit de la ultima salvare in NVS.
  static unsigned int s_sinceSave = 0;

  bool isRunning()     { return s_ready; }
  bool isPairingMode() { return s_pairingMode; }

  // -------------------------------------------------------------------
  // Modul pairing
  // -------------------------------------------------------------------

  void enterPairingMode() {
    s_pairingMode = true;
    s_pairingUntil = millis() + PAIRING_MODE_TIMEOUT_MS;
    s_lastBlink = millis();
    s_blinkOn = false;

    Serial.println();
    Serial.print(F(">> MOD PAIRING pornit pentru "));
    Serial.print(PAIRING_MODE_TIMEOUT_MS / 1000UL);
    Serial.println(F(" secunde."));
    Serial.println(F(">> Porneste senzorul ne-inrolat acum. LED 2 clipeste cat timp fereastra e deschisa."));
  }

  void exitPairingMode() {
    if (!s_pairingMode) return;

    s_pairingMode = false;
    Serial.println(F(">> MOD PAIRING inchis. JOIN_REQ-urile urmatoare vor fi refuzate."));

    // Inapoi la semnificatia obisnuita a LED-ului 2: "radioul asculta".
    if (s_ready) Leds::set(PIN_LED_2, true);
  }

  static void servicePairingWindow() {
    if (!s_pairingMode) return;

    if ((long)(millis() - s_pairingUntil) >= 0) {
      exitPairingMode();
      return;
    }

    if (millis() - s_lastBlink >= PAIRING_BLINK_MS) {
      s_lastBlink = millis();
      s_blinkOn = !s_blinkOn;
      Leds::set(PIN_LED_2, s_blinkOn);
    }
  }

  // -------------------------------------------------------------------
  // Trimiterea unui downlink
  // -------------------------------------------------------------------

  static void sendCommand(const DeviceRecord& device, uint8_t commandType) {
    uint8_t packet[CMD_DOWN_LEN];
    SensorPacketCodec::buildCommand(packet, device.sessKey, device.devAddr,
                                    device.downCounter, commandType);

    LoRaRadio::sendRaw(packet, CMD_DOWN_LEN);

    Serial.print(F("      -> CMD_DOWN "));
    Serial.print(commandType == CMD_TYPE_RESET ? F("RESET") : F("ACK"));
    Serial.print(F(" trimis (counter "));
    Serial.print(device.downCounter);
    Serial.println(F(")"));
  }

  // -------------------------------------------------------------------
  // JOIN_REQ
  // -------------------------------------------------------------------

  static void handleJoinRequest(const uint8_t* buffer, int length,
                                int rssi, float snr) {
    JoinRequest request;
    if (!SensorPacketCodec::parseJoinRequest(buffer, length, request)) {
      s_joinsRejected++;
      return;
    }

    Serial.print(F("[JOIN_REQ] DevEUI "));
    SensorPacketCodec::printEui(request.devEui);
    Serial.print(F("  DevNonce 0x"));
    Serial.print(request.devNonce, HEX);
    Serial.print(F("  RSSI "));
    Serial.print(rssi);
    Serial.print(F(" dBm  SNR "));
    Serial.print(snr, 1);
    Serial.println(F(" dB"));

    if (!s_pairingMode) {
      s_joinsRejected++;
      Serial.println(F("      REFUZAT: hub-ul nu este in mod pairing (comanda 'pair')."));
      return;
    }

    // 1. Are voie senzorul asta sa se inroleze?
    const uint8_t* appKey = DeviceRegistry::findAppKey(request.devEui);
    if (appKey == nullptr) {
      s_joinsRejected++;
      Serial.println(F("      REFUZAT: DevEUI-ul nu este in lista de provisioning din Config.h."));
      return;
    }

    // 2. Este cererea autentica? MIC-ul acopera primii 12 octeti.
    if (!HubCrypto::macVerify(appKey, buffer, JOIN_REQ_MIC_INPUT_LEN, request.mic)) {
      s_joinsRejected++;
      Serial.println(F("      REFUZAT: MIC gresit - AppKey-ul de pe senzor nu se potriveste."));
      return;
    }

    // 3. Nu este un JOIN_REQ rejucat?
    DeviceRecord* existing = DeviceRegistry::findByEui(request.devEui);
    if (existing != nullptr && existing->lastDevNonce == request.devNonce) {
      s_joinsRejected++;
      Serial.println(F("      REFUZAT: DevNonce deja folosit (posibil replay)."));
      return;
    }

    // 4. Adresa: la o re-inrolare se pastreaza cea veche, ca sa nu se
    //    umple spatiul de adrese cu aceeasi placa.
    uint8_t devAddr = (existing != nullptr) ? existing->devAddr
                                            : DeviceRegistry::allocateAddress();
    if (devAddr == 0) {
      s_joinsRejected++;
      Serial.println(F("      REFUZAT: nu mai sunt adrese libere (registrul este plin)."));
      return;
    }

    // 5. JoinNonce nou, din generatorul hardware al ESP32.
    uint8_t joinNonce[JOIN_NONCE_LEN];
    uint32_t entropy = esp_random();
    joinNonce[0] = (uint8_t)((entropy >> 16) & 0xFF);
    joinNonce[1] = (uint8_t)((entropy >> 8) & 0xFF);
    joinNonce[2] = (uint8_t)(entropy & 0xFF);

    // 6. Cheia de sesiune, derivata identic pe ambele capete.
    uint8_t sessKey[CRYPTO_KEY_LEN];
    HubCrypto::deriveSessionKey(appKey, request.devNonce, joinNonce,
                                devAddr, sessKey);

    DeviceRecord* record = DeviceRegistry::add(request.devEui, devAddr,
                                               sessKey, request.devNonce);
    if (record == nullptr) {
      s_joinsRejected++;
      Serial.println(F("      REFUZAT: registrul este plin."));
      return;
    }

    // 7. Raspunsul. Senzorul asculta imediat dupa ce a emis, deci nu
    //    trebuie sa intarziem.
    uint8_t accept[JOIN_ACCEPT_LEN];
    SensorPacketCodec::buildJoinAccept(accept, appKey, request.devEui,
                                       request.devNonce, devAddr, joinNonce);
    LoRaRadio::sendRaw(accept, JOIN_ACCEPT_LEN);

    s_joinsAccepted++;
    Leds::pulse(PIN_LED_1);

    Serial.print(F("      ACCEPTAT. DevAddr alocat: 0x"));
    if (devAddr < 0x10) Serial.print('0');
    Serial.print(devAddr, HEX);
    Serial.println(F("  - JOIN_ACCEPT trimis, cheie de sesiune salvata."));
  }

  // -------------------------------------------------------------------
  // DATA_ENC
  // -------------------------------------------------------------------

  static void handleEncryptedData(const uint8_t* buffer, int length,
                                  int rssi, float snr) {
    EncryptedData data;
    if (!SensorPacketCodec::parseEncryptedData(buffer, length, data)) {
      s_dataUnknown++;
      return;
    }

    DeviceRecord* device = DeviceRegistry::findByAddr(data.devAddr);
    if (device == nullptr) {
      s_dataUnknown++;
      Serial.print(F("[DATA] IGNORAT: DevAddr 0x"));
      if (data.devAddr < 0x10) Serial.print('0');
      Serial.print(data.devAddr, HEX);
      Serial.println(F(" nu este inrolat."));
      return;
    }

    // Anti-replay INAINTE de orice operatie criptografica: un pachet
    // rejucat nu merita nici macar o cifrare.
    if (device->hasUplink && data.frameCounter <= device->lastFrameCounterUp) {
      s_dataReplay++;
      Serial.print(F("[DATA] REPLAY: counter "));
      Serial.print(data.frameCounter);
      Serial.print(F(" <= ultimul acceptat "));
      Serial.println(device->lastFrameCounterUp);
      return;
    }

    // MIC-ul acopera primii 13 octeti: antet, adresa, counter si payload
    // cifrat. Orice bit schimbat pe drum cade aici.
    if (!HubCrypto::macVerify(device->sessKey, buffer, DATA_ENC_MIC_INPUT_LEN, data.mic)) {
      s_dataBadMic++;
      Serial.println(F("[DATA] RESPINS: MIC gresit (cheie de sesiune diferita sau pachet modificat)."));
      return;
    }

    // Decriptarea. Payload-ul redevine EXACT pachetul de 6 octeti al
    // senzorului, deci il poate prelua codul existent, neschimbat.
    uint8_t plain[SENSOR_PACKET_LEN];
    memcpy(plain, data.payload, SENSOR_PACKET_LEN);

#if PAIRING_ENCRYPT_PAYLOAD
    uint8_t iv[XTEA_BLOCK_LEN];
    HubCrypto::buildDataIv(iv, data.devAddr, data.frameCounter, 0x00);
    HubCrypto::ctr(device->sessKey, iv, plain, SENSOR_PACKET_LEN);
#endif

    SensorPacket packet;
    if (!SensorPacketCodec::decode(plain, SENSOR_PACKET_LEN, packet)) {
      // MIC-ul a trecut, deci pachetul chiar vine de la device-ul nostru,
      // dar continutul nu se valideaza: cel mai probabil comutatorul
      // PAIRING_ENCRYPT_PAYLOAD difera intre hub si senzor.
      s_dataBadMic++;
      Serial.println(F("[DATA] MIC bun, dar payload-ul decriptat nu trece de checksum."));
      Serial.println(F("       Verifica PAIRING_ENCRYPT_PAYLOAD: trebuie identic pe hub si pe senzor."));
      SensorPacketCodec::printRaw(plain, SENSOR_PACKET_LEN);
      Serial.println();
      return;
    }

    device->lastFrameCounterUp = data.frameCounter;
    device->hasUplink = true;
    device->packets++;
    device->lastSeenMs = millis();

    s_dataValid++;
    Leds::pulse(PIN_LED_1);

    Serial.print(F("[#"));
    Serial.print(s_dataValid);
    Serial.print(F("] DevAddr 0x"));
    if (data.devAddr < 0x10) Serial.print('0');
    Serial.print(data.devAddr, HEX);
    Serial.print(F("  counter "));
    Serial.print(data.frameCounter);
    Serial.print(F("  "));
    SensorPacketCodec::print(packet);
    Serial.print(F("  RSSI: "));
    Serial.print(rssi);
    Serial.print(F(" dBm  SNR: "));
    Serial.print(snr, 1);
    Serial.println(F(" dB"));

    // Downlink: RESET are prioritate fata de ACK.
    if (device->pendingReset) {
      device->downCounter++;
      sendCommand(*device, CMD_TYPE_RESET);

      // Copiem DevEUI inainte de stergere: removeByEui muta ultimul
      // element peste cel sters, deci "device" nu mai este valid dupa.
      uint8_t eui[DEV_EUI_LEN];
      memcpy(eui, device->devEui, DEV_EUI_LEN);

      DeviceRegistry::removeByEui(eui);
      s_sinceSave = 0;

      Serial.print(F("      Device-ul "));
      SensorPacketCodec::printEui(eui);
      Serial.println(F(" a fost scos din registru. Pachetele lui urmatoare vor fi ignorate."));
      return;
    }

#if PAIRING_SEND_ACK
    device->downCounter++;
    sendCommand(*device, CMD_TYPE_ACK);
#endif

    // Registrul se salveaza rar: NVS este flash, iar contorul din RAM
    // este oricum suficient pentru anti-replay intre doua salvari.
    s_sinceSave++;
    if (s_sinceSave >= REGISTRY_SAVE_EVERY) {
      s_sinceSave = 0;
      DeviceRegistry::save();
    }
  }

  // -------------------------------------------------------------------
  // Interfata de test
  // -------------------------------------------------------------------

  bool begin() {
    printTitle("PAIRING CRIPTAT - INROLARE SI DATE");
    Serial.println(F("Ascult JOIN_REQ (0x10) si DATA_ENC (0x12) de la nodurile senzor."));
    Serial.println(F("Comenzi in timpul testului: pair | list | remove <DevEUI> | stats"));
    printSeparator();

    s_ready = LoRaRadio::begin();

    s_lastActivity = millis();
    s_sinceSave = 0;

    if (s_ready) {
      Serial.println(F("LoRa OK: 868.0 MHz, SF7, BW 125 kHz, CR 4/5, sync 0x12, CRC on."));
      DeviceRegistry::printAll();
      Leds::set(PIN_LED_2, true);      // LED de stare: radioul asculta
    }

    return s_ready;
  }

  void tick() {
    if (!s_ready) return;

    Leds::service();                   // stinge pulsul precedent la scadenta
    servicePairingWindow();

    uint8_t buffer[RX_BUFFER_SIZE];
    int   length = 0;
    int   rssi = 0;
    float snr = 0.0f;

    // Polling, nu callback pe DIO0: un callback ar accesa SPI din
    // context de intrerupere, posibil in mijlocul unui transfer Ethernet.
    if (LoRaRadio::receiveRaw(buffer, RX_BUFFER_SIZE, length, rssi, snr)) {
      s_lastActivity = millis();

      switch (SensorPacketCodec::messageType(buffer, length)) {
        case SENSOR_MSG_JOIN_REQ:
          handleJoinRequest(buffer, length, rssi, snr);
          break;

        case SENSOR_MSG_DATA_ENC:
          handleEncryptedData(buffer, length, rssi, snr);
          break;

        case SENSOR_MSG_TEMPERATURE:
          // Un senzor care emite inca in clar: nu este o eroare, dar nu
          // are ce cauta aici. Testul 7 este cel pentru el.
          s_foreign++;
          Serial.println(F("[PLAIN] Pachet de temperatura NECRIPTAT - senzorul nu este inrolat."));
          Serial.println(F("        Pentru el foloseste testul 7, sau inroleaza-l cu 'pair'."));
          break;

        default:
          s_foreign++;
          SensorPacketCodec::printRaw(buffer, length);
          Serial.println();
          break;
      }
    }
    else if (millis() - s_lastActivity >= HEARTBEAT_MS) {
      s_lastActivity = millis();
      Serial.print(F("...niciun pachet in ultimele 15 s.  (date valide: "));
      Serial.print(s_dataValid);
      Serial.print(F(", inrolari: "));
      Serial.print(s_joinsAccepted);
      Serial.println(s_pairingMode ? F(", mod pairing ACTIV)") : F(")"));
    }

    delay(5);
  }

  void stop() {
    exitPairingMode();

    // Ce s-a acumulat in RAM de la ultima salvare merge acum pe disc.
    DeviceRegistry::save();

    LoRaRadio::sleep();
    Leds::allOff();
    s_ready = false;

    printStats();
  }

  void printStats() {
    printSeparator();
    Serial.println(F("Contoare pairing:"));
    Serial.print(F("  inrolari acceptate : ")); Serial.println(s_joinsAccepted);
    Serial.print(F("  inrolari refuzate  : ")); Serial.println(s_joinsRejected);
    Serial.print(F("  pachete de date OK : ")); Serial.println(s_dataValid);
    Serial.print(F("  replay respinse    : ")); Serial.println(s_dataReplay);
    Serial.print(F("  MIC gresit         : ")); Serial.println(s_dataBadMic);
    Serial.print(F("  adresa necunoscuta : ")); Serial.println(s_dataUnknown);
    Serial.print(F("  pachete straine    : ")); Serial.println(s_foreign);
    Serial.print(F("  mod pairing        : "));
    Serial.println(s_pairingMode ? F("ACTIV") : F("inchis"));
  }
}
