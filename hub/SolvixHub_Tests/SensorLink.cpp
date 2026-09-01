#include "SensorLink.h"


namespace SensorLink {

  // Cat timp de liniste inainte de un mesaj de semn de viata.
  static const unsigned long HEARTBEAT_MS = 15000;

  // Ceva mai mare decat cel mai lung pachet al protocolului (DATA_UP,
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
  static unsigned long s_removalsConfirmed = 0;

  // Cate pachete au lipsit cu totul, insumat pe toti senzorii. Se deduc
  // din golurile de frame counter; per senzor, cifra sta in registru.
  static unsigned long s_lostTotal = 0;

  // Cate pachete de la adrese neinrolate au trecut de la ultimul sfat de
  // recuperare afisat.
  static unsigned long s_unknownSeen = 0;

  // Cate pachete de date au venit de la ultima salvare in NVS.
  static unsigned int s_sinceSave = 0;

  // Momentul ultimului pachet primit pe radio, valid sau nu. Restul
  // sistemului se uita aici inainte de a face ceva lung - vezi lastRxMs()
  // in SensorLink.h.
  static unsigned long s_lastRxMs = 0;

  // Carligul pentru telemetrie, neinregistrat deocamdata.
  static ReadingHandler s_readingHandler = NULL;

  bool isRunning()     { return s_ready; }
  bool isPairingMode() { return s_pairingMode; }

  // -------------------------------------------------------------------
  // Identificarea senzorului in jurnal
  // -------------------------------------------------------------------

  /*
   * "Senzor #3 (0x03)" - numarul si adresa sunt acelasi octet, dar sunt
   * afisate amandoua dinadins: numarul este ce foloseste omul, adresa
   * este ce se vede pe fir cand cineva se uita cu un analizor.
   *
   * Cu un singur senzor, jurnalul putea sa nu spuna de la cine vine
   * pachetul. Cu cinci, fiecare linie trebuie sa inceapa cu asta.
   */
  static void printSensorTag(uint8_t devAddr) {
    Serial.print(F("Senzor #"));
    Serial.print(devAddr);
    Serial.print(F(" (0x"));
    if (devAddr < 0x10) Serial.print('0');
    Serial.print(devAddr, HEX);
    Serial.print(')');
  }

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
  // Supravegherea senzorilor tacuti
  // -------------------------------------------------------------------

  /*
   * Cu un singur senzor, disparitia lui era evidenta: nu mai curgea
   * nimic pe Serial. Cu cinci, jurnalul curge la fel de repede si numai
   * unul lipseste - exact cazul pe care ochiul nu il prinde.
   *
   * Se anunta o SINGURA data caderea si o singura data revenirea.
   * Senzorii marcati pentru dezinrolare sunt sariti: acolo tacerea este
   * rezultatul dorit, nu o defectiune, si o are pe conditia ei
   * (servicePendingRemovals).
   */
  static void serviceOfflineWatch() {
    for (uint8_t i = 0; i < DeviceRegistry::count(); i++) {
      DeviceRecord* device = DeviceRegistry::at(i);
      if (device == nullptr || device->pendingReset) continue;

      // Fara niciun pachet in sesiunea asta nu avem de la ce sa masuram
      // tacerea: dupa o repornire a hub-ului toti senzorii sunt "nevazuti"
      // pana la primul lor pachet, ceea ce nu este acelasi lucru cu a fi
      // cazut.
      if (device->lastSeenMs == 0) continue;

      bool silent = (millis() - device->lastSeenMs) >= SENSOR_OFFLINE_MS;

      if (silent && !device->offlineReported) {
        device->offlineReported = true;
        Serial.println();
        Serial.print(F(">> NU SE MAI AUDE: "));
        printSensorTag(device->devAddr);
        Serial.print(F(" - niciun pachet de "));
        Serial.print((millis() - device->lastSeenMs) / 1000UL);
        Serial.println(F(" s."));
        Serial.println(F(">> Verifica alimentarea placii, antena si distanta. Sesiunea lui ramane"));
        Serial.println(F(">> valida in registru, deci daca revine isi continua numerotarea."));
      }
      else if (!silent && device->offlineReported) {
        device->offlineReported = false;
        Serial.println();
        Serial.print(F(">> A REVENIT: "));
        printSensorTag(device->devAddr);
        Serial.println(F("."));
      }
    }
  }

  // -------------------------------------------------------------------
  // Dezinrolarea confirmata (F-031)
  // -------------------------------------------------------------------

  // Trimite un RESET catre un device marcat si porneste (sau reporneste)
  // ceasul de tacere. Apelantul a verificat deja ca device-ul este al
  // nostru: pachetul care a declansat retrimiterea a venit de la adresa lui.
  static void sendRemovalReset(DeviceRecord& device);

  /*
   * Confirmarea dezinrolarii, chemata din tick() - deci fara delay() si
   * fara vreun timer nou.
   *
   * Inregistrarea unui device marcat NU se sterge la trimiterea
   * RESET-ului, ci abia cand senzorul tace: cat timp se aude, inseamna ca
   * nu a primit comanda si mai are nevoie de una. Stergerea imediata era
   * bug-ul F-031 - un singur downlink pierdut lasa hub-ul fara cheie si
   * senzorul in retea pentru totdeauna.
   */
  static void servicePendingRemovals() {
    uint8_t i = 0;

    while (i < DeviceRegistry::count()) {
      DeviceRecord* device = DeviceRegistry::at(i);

      if (device == nullptr || !device->pendingReset) {
        i++;
        continue;
      }

      // resetSentMs == 0 inseamna ca in ACEASTA sesiune nu i-am trimis
      // inca niciun RESET: fie tocmai a fost marcat, fie hub-ul s-a
      // repornit in mijlocul dezinrolarii (load() pune campul pe 0).
      // Tacerea nu dovedeste nimic atunci - nu avem de unde sti daca
      // senzorul a auzit vreodata comanda -, deci asteptam un pachet de
      // la el, care va declansa o retrimitere si va porni ceasul.
      if (device->resetSentMs == 0) {
        i++;
        continue;
      }

      if ((unsigned long)(millis() - device->resetSentMs) < REMOVE_CONFIRM_SILENCE_MS) {
        i++;
        continue;
      }

      // Copiem ce ne trebuie INAINTE de stergere: removeByEui muta
      // ultimul element peste cel sters, deci "device" devine invalid.
      uint8_t  eui[DEV_EUI_LEN];
      memcpy(eui, device->devEui, DEV_EUI_LEN);
      uint16_t attempts = device->resetAttempts;
      uint8_t  number   = device->devAddr;

      DeviceRegistry::removeByEui(eui);
      s_sinceSave = 0;
      s_removalsConfirmed++;

      Serial.println();
      Serial.print(F(">> DEZINROLARE CONFIRMATA: Senzor #"));
      Serial.print(number);
      Serial.print(F(", DevEUI "));
      SensorPacketCodec::printEui(eui);
      Serial.print(F(" a tacut "));
      Serial.print(REMOVE_CONFIRM_SILENCE_MS / 1000UL);
      Serial.print(F(" s dupa ultimul RESET (trimise: "));
      Serial.print(attempts);
      Serial.println(F(")."));
      Serial.print(F(">> Numarul #"));
      Serial.print(number);
      Serial.println(F(" ramane rezervat placii: vine din tabelul de provisioning,"));
      Serial.println(F(">> deci la o reinrolare primeste exact acelasi numar inapoi."));
      Serial.println(F(">> Sters din registru. Cheia lui de sesiune nu mai este valida nicaieri:"));
      Serial.println(F(">> senzorul si-a sters-o din HEF, hub-ul a sters inregistrarea."));
      Serial.println(F(">> Ca sa il aduci inapoi: 'pair' pe hub SI trei secunde pe butonul 2 al"));
      Serial.println(F(">> senzorului. Fara apasarea aceea senzorul ramane in repaus si nu cere"));
      Serial.println(F(">> inrolarea singur."));

#if PAIRING_REOPEN_AFTER_REMOVE
      // Fereastra se redeschide ca sa nu fie nevoie de inca un `pair`.
      // Nu inlocuieste apasarea de pe senzor, doar o asteapta.
      enterPairingMode();
#endif

      // Nu incrementam i: pe pozitia lui a fost mutat ultimul element.
    }
  }

  // -------------------------------------------------------------------
  // Trimiterea unui downlink
  // -------------------------------------------------------------------

  static void sendCommand(const DeviceRecord& device, uint8_t commandType) {
    uint8_t packet[CMD_DOWN_LEN];
    SensorPacketCodec::buildCommand(packet, device.devAddr, commandType);

    LoRaRadio::sendRaw(packet, CMD_DOWN_LEN);

    Serial.print(F("      -> CMD_DOWN "));
    Serial.print(commandType == CMD_TYPE_RESET ? F("RESET") : F("ACK"));
    Serial.print(F(" catre "));
    printSensorTag(device.devAddr);
    Serial.print(F(" (counter "));
    Serial.print(device.downCounter);
    Serial.println(F(")"));
  }

  static void sendRemovalReset(DeviceRecord& device) {
    device.downCounter++;
    device.resetAttempts++;
    device.resetSentMs = millis();
    sendCommand(device, CMD_TYPE_RESET);
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

    // 1. Are voie senzorul asta sa se inroleze? Este singura verificare
    //    ramasa la inrolare: fara MIC, apartenenta la lista de
    //    provisioning nu mai este DOVEDITA, ci doar declarata. Oricine
    //    poate emite un JOIN_REQ cu un DevEUI din lista.
    if (!DeviceRegistry::isProvisioned(request.devEui)) {
      s_joinsRejected++;
      Serial.println(F("      REFUZAT: DevEUI-ul nu este in lista de provisioning din Config.h."));
      return;
    }

    DeviceRecord* existing = DeviceRegistry::findByEui(request.devEui);

    // 2. NUMARUL senzorului, care este si adresa lui. Nu se aloca "prima
    //    libera": este pozitia in tabelul de provisioning din Config.h,
    //    plus unu. Asa aceeasi placa primeste acelasi numar la fiecare
    //    inrolare, indiferent de ordinea in care au fost pornite placile
    //    si indiferent daca registrul a fost golit intre timp - numarul
    //    poate fi deci scris pe cutie.
    uint8_t devAddr = DeviceRegistry::addressForEui(request.devEui);
    if (devAddr == 0) {
      s_joinsRejected++;
      Serial.println(F("      REFUZAT: DevEUI-ul este in lista de provisioning, dar pe o pozitie"));
      Serial.println(F("      peste HUB_MAX_SENSORS. Creste HUB_MAX_SENSORS in Config.h sau muta"));
      Serial.println(F("      randul mai sus in PROVISIONED_DEVICES_INIT."));
      return;
    }

    // Nu poate fi ocupat de altcineva - pozitia din tabel este unica per
    // DevEUI - dar daca in NVS a ramas o inregistrare dintr-o versiune in
    // care adresele se alocau in ordinea inrolarii, ea poate sta fix pe
    // numarul asta. Se spune limpede, in loc sa se suprascrie in tacere
    // cheia altui senzor.
    DeviceRecord* squatter = DeviceRegistry::findByAddr(devAddr);
    if (squatter != nullptr && squatter != existing) {
      s_joinsRejected++;
      Serial.print(F("      REFUZAT: numarul #"));
      Serial.print(devAddr);
      Serial.println(F(" este ocupat in registru de alt DevEUI (inregistrare veche)."));
      Serial.println(F("      Sterge-l cu 'remove <DevEUI> force' si repeta inrolarea."));
      return;
    }

    DeviceRecord* record = DeviceRegistry::add(request.devEui, devAddr);
    if (record == nullptr) {
      s_joinsRejected++;
      Serial.println(F("      REFUZAT: registrul este plin."));
      return;
    }

    // 3. Raspunsul. Senzorul asculta imediat dupa ce a emis, deci nu
    //    trebuie sa intarziem.
    uint8_t accept[JOIN_ACCEPT_LEN];
    SensorPacketCodec::buildJoinAccept(accept, devAddr);
    LoRaRadio::sendRaw(accept, JOIN_ACCEPT_LEN);

    s_joinsAccepted++;
    Leds::pulse(PIN_LED_1);

    Serial.print(F("      ACCEPTAT ca "));
    printSensorTag(devAddr);
    Serial.println(F(" - JOIN_ACCEPT trimis."));
    Serial.print(F("      Inrolati acum: "));
    Serial.print(DeviceRegistry::count());
    Serial.print(F(" din "));
    Serial.print(HUB_MAX_SENSORS);
    Serial.println(F(". Din numar iese si slotul lui de somn, deci de acum"));
    Serial.println(F("      emite pe alt ritm decat ceilalti."));
  }

  // -------------------------------------------------------------------
  // DATA_UP
  // -------------------------------------------------------------------

  static void handleData(const uint8_t* buffer, int length,
                                  int rssi, float snr) {
    SensorData data;
    if (!SensorPacketCodec::parseData(buffer, length, data)) {
      s_dataUnknown++;
      return;
    }

    // Aici se raspunde la intrebarea "de la cine vine data": DevAddr
    // circula in clar in octetul [2]. ATENTIE: de cand nu mai exista MIC,
    // raspunsul este DECLARATIV - orice emitator poate pretinde orice
    // numar. Ce ramane adevarat este ca doua pachete simultane se pierd
    // amandoua pe radio, deci nu exista "date amestecate".
    DeviceRecord* device = DeviceRegistry::findByAddr(data.devAddr);
    if (device == nullptr) {
      s_dataUnknown++;
      Serial.print(F("[DATA] IGNORAT: DevAddr 0x"));
      if (data.devAddr < 0x10) Serial.print('0');
      Serial.print(data.devAddr, HEX);
      Serial.println(F(" nu este inrolat."));

      // Sfat de recuperare, dar rar: un senzor ramas cu o cheie veche
      // emite la fiecare 5 s si ar ineca Serial-ul. Hub-ul NU mai are
      // cheia lui, deci nu il mai poate opri prin radio - de asta
      // stergerea nu se mai face la prima trimitere de RESET (F-031).
      s_unknownSeen++;
      if (((s_unknownSeen - 1) % PAIRING_UNKNOWN_HINT_EVERY) == 0) {
        Serial.println(F("       Senzorul se crede inrolat, dar nu are inregistrare aici. Nici"));
        Serial.println(F("       oprirea alimentarii nu ajuta: starea sta in HEF. Recuperare:"));
        Serial.println(F("       'pair' pe hub, apoi butonul 2 tinut trei secunde pe senzor."));
      }
      return;
    }

    // Anti-replay INAINTE de orice operatie criptografica: un pachet
    // rejucat nu merita nici macar o cifrare.
    if (device->hasUplink && data.frameCounter <= device->lastFrameCounterUp) {
      s_dataReplay++;
      Serial.print(F("[DATA] REPLAY de la "));
      printSensorTag(data.devAddr);
      Serial.print(F(": counter "));
      Serial.print(data.frameCounter);
      Serial.print(F(" <= ultimul acceptat "));
      Serial.println(device->lastFrameCounterUp);
      return;
    }

    // Dezinrolare in curs. Faptul ca senzorul inca emite este dovada ca
    // nu a primit RESET-ul precedent, deci i se retrimite. Pachetul NU se
    // decodeaza si nu se numara ca date valide: senzorul este pe iesire,
    // masuratoarea lui nu mai intereseaza pe nimeni (F-031).
    if (device->pendingReset) {
      // Contorul se avanseaza si aici, altfel un pachet retrimis de
      // senzor ar trece de anti-replay dupa ce dezinrolarea se incheie.
      device->lastFrameCounterUp = data.frameCounter;
      device->hasUplink = true;
      device->lastSeenMs = millis();

      Serial.print(F("[DATA] "));
      printSensorTag(data.devAddr);
      Serial.print(F(" - dezinrolare in curs, inca se aude. Retrimit RESET (incercarea "));
      Serial.print((unsigned int)(device->resetAttempts + 1));
      Serial.println(F(")."));

      sendRemovalReset(*device);
      return;
    }

    // Payload-ul este EXACT pachetul de 6 octeti al senzorului, deci il
    // preia codul existent, neschimbat: nu exista doua cai diferite de
    // interpretare a temperaturii.
    const uint8_t* plain = data.payload;

    SensorPacket packet;
    if (!SensorPacketCodec::decode(plain, SENSOR_PACKET_LEN, packet)) {
      // Pachetul a trecut de CRC-ul LoRa si de verificarea de lungime,
      // dar cei 6 octeti nu se valideaza. Fara MIC asta nu mai inseamna
      // "cheie gresita": inseamna ori un emitator strain care nimereste
      // aceiasi parametri radio si aceeasi adresa, ori un capat ramas pe
      // firmware vechi.
      s_dataBadMic++;
      Serial.print(F("[DATA] "));
      printSensorTag(data.devAddr);
      Serial.println(F(": payload-ul nu trece de checksum. Emitator strain sau"));
      Serial.println(F("       firmware nesincronizat intre senzor si hub."));
      SensorPacketCodec::printRaw(plain, SENSOR_PACKET_LEN);
      Serial.println();
      return;
    }

    /*
     * Pachetele care NU au ajuns, deduse din golul de frame counter.
     * Senzorul isi incrementeaza contorul la fiecare transmisie, chiar
     * si la una esuata (senzor/main.c, sectiunea 16), deci un salt de la
     * 41 la 44 inseamna exact doua pachete pierdute pe drum. Cu mai
     * multi senzori, cauza obisnuita este o coliziune; cu unul singur,
     * acoperirea. Este singura urma pe care o lasa o coliziune: pachetele
     * suprapuse nu ajung la hub in nicio forma.
     *
     * Doua goluri NU sunt pierderi si ar falsifica tocmai cifra dupa care
     * se judeca coliziunile:
     *   - primul pachet de dupa o inrolare (hasUplink inca fals) - nu
     *     avem de la ce sa scadem;
     *   - saltul cu FCNT_CHECKPOINT_EVERY pe care senzorul il face la
     *     fiecare pornire la rece. Peste SENSOR_FCNT_GAP_RESTART se
     *     considera repornire si se spune asta, in loc sa se adune
     *     cincizeci de pierderi imaginare.
     */
    if (device->hasUplink) {
      uint32_t gap = data.frameCounter - device->lastFrameCounterUp - 1;

      if (gap >= SENSOR_FCNT_GAP_RESTART) {
        Serial.print(F("       ("));
        printSensorTag(data.devAddr);
        Serial.print(F(" a sarit "));
        Serial.print(gap);
        Serial.println(F(" valori de counter: a repornit. Nu se numara ca pierderi.)"));
      }
      else if (gap > 0) {
        device->lostPackets += gap;
        s_lostTotal += gap;
      }
    }

    device->lastFrameCounterUp = data.frameCounter;
    device->hasUplink = true;
    device->packets++;
    device->lastSeenMs = millis();
    device->lastTempX100 = packet.tempX100;
    device->lastRssi = (int16_t)rssi;
    device->hasReading = true;

    // Daca tocmai a revenit dupa o tacere lunga, serviceOfflineWatch()
    // anunta revenirea la urmatorul tick.

    s_dataValid++;
    Leds::pulse(PIN_LED_1);

    /*
     * ACK-ul pleaca INAINTE de blocul de afisare de mai jos, nu dupa el.
     *
     * Senzorul isi tine fereastra de receptie deschisa DOWNLINK_WINDOW_MS
     * = 600 ms de la sfarsitul propriei transmisii, iar hub-ul raspundea
     * in ~55 ms - marja este confortabila, dar linia de jurnal de mai jos
     * are vreo 110 caractere, adica ~9,5 ms la 115200 baud, cheltuiti
     * degeaba din interiorul ferestrei. Ordinea de aici costa zero si
     * face loc si carligului de telemetrie de mai jos, care va creste.
     *
     * Dezinrolarea a fost tratata mai sus, inainte de decodare: aici
     * ajung doar pachetele unui device sanatos.
     */
#if PAIRING_SEND_ACK
    device->downCounter++;
    sendCommand(*device, CMD_TYPE_ACK);
#endif

    // Carligul pentru telemetrie. Astazi nu este inregistrat nimeni;
    // cand va fi, are voie DOAR sa puna intr-o coada (vezi SensorLink.h).
    if (s_readingHandler != NULL) {
      s_readingHandler(data.devAddr, packet.tempX100, data.frameCounter,
                       (int16_t)rssi, packet.reason);
    }

    Serial.print(F("[#"));
    Serial.print(s_dataValid);
    Serial.print(F("] "));
    printSensorTag(data.devAddr);
    Serial.print(F("  counter "));
    Serial.print(data.frameCounter);
    Serial.print(F("  "));
    SensorPacketCodec::print(packet);
    Serial.print(F("  RSSI: "));
    Serial.print(rssi);
    Serial.print(F(" dBm  SNR: "));
    Serial.print(snr, 1);
    Serial.print(F(" dB"));
    if (device->lostPackets != 0) {
      Serial.print(F("  (pierdute pana acum: "));
      Serial.print(device->lostPackets);
      Serial.print(')');
    }
    Serial.println();

    // Registrul se salveaza rar: NVS este flash, iar contorul din RAM
    // este oricum suficient pentru anti-replay intre doua salvari.
    s_sinceSave++;
    if (s_sinceSave >= REGISTRY_SAVE_EVERY) {
      s_sinceSave = 0;
      DeviceRegistry::save();
    }
  }

  // -------------------------------------------------------------------
  // Interfata runtime-ului
  // -------------------------------------------------------------------

  bool begin() {
    printTitle("LEGATURA CU SENZORII - INROLARE SI DATE");
    Serial.println(F("Ascult JOIN_REQ (0x10) si DATA_UP (0x12) de la nodurile senzor."));
    Serial.print(F("Reteaua are loc pentru "));
    Serial.print(HUB_MAX_SENSORS);
    Serial.println(F(" senzori; fiecare are un numar fix, dat de pozitia lui"));
    Serial.println(F("in tabelul de provisioning din Config.h."));
    Serial.println(F("Comenzi: pair | sensors | list | remove <DevEUI> | stats | help"));
    printSeparator();

    s_ready = LoRaRadio::begin();

    s_lastActivity = millis();
    s_sinceSave = 0;

    if (s_ready) {
      Serial.println(F("LoRa OK: 868.0 MHz, SF7, BW 125 kHz, CR 4/5, sync 0x12, CRC on."));
      DeviceRegistry::printSensorTable();
      Leds::set(PIN_LED_2, true);      // LED de stare: radioul asculta
    }

    return s_ready;
  }

  void tick() {
    if (!s_ready) return;

    Leds::service();                   // stinge pulsul precedent la scadenta
    servicePairingWindow();
    servicePendingRemovals();          // confirma dezinrolarile prin tacere
    serviceOfflineWatch();             // anunta senzorii care au amutit

    uint8_t buffer[RX_BUFFER_SIZE];
    int   length = 0;
    int   rssi = 0;
    float snr = 0.0f;

    // Polling, nu callback pe DIO0: un callback ar accesa SPI din
    // context de intrerupere, posibil in mijlocul unui transfer Ethernet.
    if (LoRaRadio::receiveRaw(buffer, RX_BUFFER_SIZE, length, rssi, snr)) {
      s_lastActivity = millis();

      // Orice pachet auzit, valid sau nu, inseamna ca un senzor tocmai a
      // emis si isi tine fereastra de downlink deschisa. Marcam momentul
      // aici, inaintea oricarei interpretari.
      s_lastRxMs = s_lastActivity;

      switch (SensorPacketCodec::messageType(buffer, length)) {
        case SENSOR_MSG_JOIN_REQ:
          handleJoinRequest(buffer, length, rssi, snr);
          break;

        case SENSOR_MSG_DATA_UP:
          handleData(buffer, length, rssi, snr);
          break;

        case SENSOR_MSG_TEMPERATURE:
          // Un senzor neinrolat, care emite pachetul fara adresa: nu
          // este o eroare, dar nu poate fi atribuit nimanui.
          s_foreign++;
          Serial.println(F("[PLAIN] Pachet de temperatura fara adresa - senzorul nu este inrolat."));
          Serial.println(F("        Inroleaza-l cu 'pair' plus butonul 2 tinut 3 s pe placa."));
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
      Serial.print(F("...niciun pachet in ultimele 15 s.  (senzori inrolati: "));
      Serial.print(DeviceRegistry::count());
      Serial.print('/');
      Serial.print(HUB_MAX_SENSORS);
      Serial.print(F(", date valide: "));
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

  unsigned long lastRxMs() { return s_lastRxMs; }

  void onReading(ReadingHandler handler) { s_readingHandler = handler; }

  /*
   * Exista vreo dezinrolare in curs?
   *
   * Cat timp raspunsul este da, singurul lucru care conteaza pe radio
   * este ca RESET-ul sa prinda fereastra de downlink a senzorului marcat.
   * Un `remove` care nu ajunge lasa senzorul in retea, iar registrul
   * blocat pana la confirmare (F-031), deci restul sistemului se da la o
   * parte pana se termina.
   */
  bool hasPendingRemoval() {
    for (uint8_t i = 0; i < DeviceRegistry::count(); i++) {
      DeviceRecord* device = DeviceRegistry::at(i);
      if (device != NULL && device->pendingReset) return true;
    }
    return false;
  }

  void printStats() {
    printSeparator();
    Serial.println(F("Contoare pairing:"));
    Serial.print(F("  inrolari acceptate : ")); Serial.println(s_joinsAccepted);
    Serial.print(F("  inrolari refuzate  : ")); Serial.println(s_joinsRejected);
    Serial.print(F("  pachete de date OK : ")); Serial.println(s_dataValid);
    Serial.print(F("  replay respinse    : ")); Serial.println(s_dataReplay);
    Serial.print(F("  payload invalid    : ")); Serial.println(s_dataBadMic);
    Serial.print(F("  adresa necunoscuta : ")); Serial.println(s_dataUnknown);
    Serial.print(F("  pachete straine    : ")); Serial.println(s_foreign);
    Serial.print(F("  pachete pierdute   : ")); Serial.print(s_lostTotal);
    Serial.println(F("   (goluri in frame counter, pe toti senzorii)"));
    Serial.print(F("  dezinrolari confirmate: ")); Serial.println(s_removalsConfirmed);
    Serial.print(F("  senzori inrolati   : "));
    Serial.print(DeviceRegistry::count());
    Serial.print('/');
    Serial.println(HUB_MAX_SENSORS);
    Serial.print(F("  mod pairing        : "));
    Serial.println(s_pairingMode ? F("ACTIV") : F("inchis"));
  }
}
