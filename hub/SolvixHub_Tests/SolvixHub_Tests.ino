/*
  =====================================================================
  SolvixHub_Tests - suita de teste hardware pentru hub, ESP32
  (varianta cu PAIRING CRIPTAT)
  =====================================================================
  Toate testele care existau ca sketch-uri separate sunt aici, intr-un
  singur program, selectabile din meniul de pe portul serial. Se incarca
  o singura data pe placa si se trece de la un test la altul fara
  reprogramare.

  UTILIZARE
    1. Deschide Serial Monitor la 115200 baud.
    2. Alege terminatorul de linie "Newline" (sau "Both NL & CR").
    3. Scrie cifra testului si apasa Enter. Tasta 'm' reafiseaza meniul,
       tasta '0' opreste testul curent.

  COMENZI DE PAIRING (cuvinte, nu cifre; merg si in timpul unui test)
    pair              - deschide fereastra de inrolare (porneste testul 8
                        daca nu ruleaza deja)
    list              - senzorii inrolati, din registrul salvat in NVS
    provisioned       - senzorii care AU VOIE sa se inroleze (Config.h)
    remove <DevEUI>   - scoate un senzor din retea; la primul lui contact
                        primeste un CMD_DOWN de tip RESET si abia apoi
                        este sters din registru
    remove <DevEUI> force - il sterge imediat, fara sa il mai anunte
    stats             - contoarele testului de pairing
    help              - lista aceasta

  MAGISTRALA SPI PARTAJATA - de citit inainte de a modifica ceva
    Modulul Ethernet ENC28J60 si modulul LoRa SX1276 sunt legate pe
    aceiasi pini: SCK 18, MISO 19, MOSI 23. Se despart doar prin chip
    select: CS_ETH = GPIO4, NSS_LoRa = GPIO5. Doua module selectate in
    acelasi timp inseamna doua iesiri care trag simultan de linia MISO:
    date corupte si, in timp, iesiri arse.

    Regulile care tin bus-ul curat sunt implementate in SpiBus.h/.cpp si
    explicate acolo pe larg. Pe scurt:
      - SPI.begin() se apeleaza o singura data, in setup();
      - ambele CS-uri sunt OUTPUT si HIGH inainte de orice trafic;
      - fiecare acces trece printr-o tranzactie SPI, deci fiecare modul
        isi impune propria viteza fara sa il afecteze pe celalalt;
      - inainte de a folosi un modul se apeleaza claimEthernet() sau
        claimLoRa(), care ridica CS-ul celuilalt;
      - nu se apeleaza niciodata LoRa.end() sau SPI.end(): ar inchide
        magistrala si pentru Ethernet;
      - LoRa se citeste prin polling, nu prin callback pe DIO0, ca sa nu
        existe acces SPI din intrerupere in mijlocul unui transfer.

  LIBRARII NECESARE (Library Manager)
    - EthernetENC  (Juraj Andrassy)     - pentru ENC28J60
    - LoRa         (Sandeep Mistry)     - pentru SX1276/78
    Placa: "ESP32 Dev Module" din esp32 by Espressif Systems.

  STRUCTURA FISIERELOR
    Config.h            - toti pinii si constantele, intr-un singur loc
    SpiBus.h/.cpp       - arbitrajul magistralei SPI partajate
    TestBase.h/.cpp     - interfata comuna a testelor si ajutoare de afisare
    EthernetLink.*      - invelis DHCP + HTTP peste EthernetENC
    LoRaRadio.*         - invelis emisie/receptie peste libraria LoRa
    Leds.h/.cpp         - cele doua LED-uri, D22 si D21
    SensorPacket.*      - formatul TUTUROR pachetelor schimbate cu senzorul
    HubCrypto.*         - XTEA-128, CBC-MAC si CTR (fara biblioteci)
    DeviceRegistry.*    - registrul senzorilor inrolati, salvat in NVS
    TestButtons.*       - butoanele de pe GPIO34 / GPIO35
    TestEncSpi.*        - diagnostic SPI de nivel jos pentru ENC28J60
    TestEthernet.*      - DHCP, DNS si cerere HTTP catre internet
    TestLoRaTx.*        - emisie LoRa periodica
    TestLoRaRx.*        - receptie LoRa cu RSSI si SNR
    TestCoexistence.*   - ambele module active alternativ pe acelasi bus
    TestSensorRx.*      - temperatura primita in CLAR de la nodul senzor
    TestPairing.*       - inrolare, date criptate, dezinrolare
*/

#include "Config.h"
#include "SpiBus.h"
#include "Leds.h"
#include "TestBase.h"
#include "DeviceRegistry.h"
#include "TestButtons.h"
#include "TestEncSpi.h"
#include "TestEthernet.h"
#include "TestLoRaTx.h"
#include "TestLoRaRx.h"
#include "TestCoexistence.h"
#include "TestSensorRx.h"
#include "TestPairing.h"

// Catalogul testelor. Pozitia din tablou + 1 este cifra din meniu.
static const Test TESTS[] = {
  { "Butoane",
    "Citeste GPIO34 si GPIO35 si semnaleaza liniile flotante",
    TestButtons::begin,     TestButtons::tick,     TestButtons::stop },

  { "Diagnostic SPI ENC28J60",
    "Test de cablaj si citire de registre; verifica EREVID",
    TestEncSpi::begin,      TestEncSpi::tick,      TestEncSpi::stop },

  { "Ethernet: DHCP + internet",
    "Ia IP prin DHCP, rezolva DNS si face o cerere HTTP",
    TestEthernet::begin,    TestEthernet::tick,    TestEthernet::stop },

  { "LoRa: emisie",
    "Trimite un pachet numerotat la fiecare 2 secunde",
    TestLoRaTx::begin,      TestLoRaTx::tick,      TestLoRaTx::stop },

  { "LoRa: receptie",
    "Afiseaza pachetele primite, cu RSSI si SNR",
    TestLoRaRx::begin,      TestLoRaRx::tick,      TestLoRaRx::stop },

  { "Coexistenta LoRa + Ethernet",
    "Ambele module active alternativ pe aceeasi magistrala SPI",
    TestCoexistence::begin, TestCoexistence::tick, TestCoexistence::stop },

  { "Senzor: receptie temperatura (in clar)",
    "Asculta nodul PIC16LF1508 neinrolat si afiseaza temperatura",
    TestSensorRx::begin,    TestSensorRx::tick,    TestSensorRx::stop },

  { "Pairing criptat: inrolare + date",
    "Inroleaza senzori, primeste temperatura criptata, sterge device-uri",
    TestPairing::begin,     TestPairing::tick,     TestPairing::stop },
};

static const uint8_t TEST_COUNT = sizeof(TESTS) / sizeof(TESTS[0]);

// Indexul testului de pairing in tabloul de mai sus. Comanda `pair` il
// porneste singura daca nu ruleaza deja.
static const uint8_t PAIRING_TEST_INDEX = TEST_COUNT - 1;

static int  s_activeTest = -1;   // -1 = niciun test in rulare
static bool s_activeOk   = false;

// Butonul 1 ca declansator de pairing: se retine starea precedenta ca sa
// se reactioneze pe FRONT, nu pe nivel. GPIO34 nu are pull intern, deci
// fara rezistor extern linia este zgomot (F-008) - de aceea comanda
// seriala `pair` ramane calea sigura.
static int s_lastButton1 = LOW;

static void printMenu() {
  Serial.println();
  Serial.println(F("=================================================="));
  Serial.println(F("  SOLVIX HUB - SUITA DE TESTE"));
  Serial.println(F("=================================================="));
  for (uint8_t i = 0; i < TEST_COUNT; i++) {
    Serial.print(F("  "));
    Serial.print(i + 1);
    Serial.print(F(") "));
    Serial.println(TESTS[i].name);
    Serial.print(F("     "));
    Serial.println(TESTS[i].description);
  }
  Serial.println(F("  0) Opreste testul curent"));
  Serial.println(F("  m) Reafiseaza acest meniu"));
  Serial.println(F("--------------------------------------------------"));
  Serial.println(F("  Comenzi de pairing: pair | list | provisioned |"));
  Serial.println(F("                      remove <DevEUI> [force] | stats | help"));
  Serial.println(F("=================================================="));
  Serial.println(F("Scrie o cifra sau o comanda si apasa Enter (Serial Monitor pe \"Newline\")."));
  Serial.println();
}

static void printCommandHelp() {
  Serial.println();
  Serial.println(F("Comenzi de pairing:"));
  Serial.println(F("  pair                    deschide fereastra de inrolare (porneste testul 8)"));
  Serial.println(F("  list                    senzorii inrolati (registrul din NVS)"));
  Serial.println(F("  provisioned             senzorii care au voie sa se inroleze (Config.h)"));
  Serial.println(F("  remove <DevEUI>         il scoate din retea; ii trimite RESET la primul contact"));
  Serial.println(F("  remove <DevEUI> force   il sterge imediat din registru, fara sa il anunte"));
  Serial.println(F("  stats                   contoarele testului de pairing"));
  Serial.println(F("DevEUI se scrie ca 16 cifre hexazecimale, ex: 534F4C5649580001"));
  Serial.println();
}

static void stopActiveTest() {
  if (s_activeTest < 0) return;
  Serial.println();
  Serial.print(F("Opresc testul: "));
  Serial.println(TESTS[s_activeTest].name);
  TESTS[s_activeTest].stop();
  SpiBus::deselectAll();   // plasa de siguranta: bus-ul ramane liber
  Leds::allOff();          // idem pentru LED-uri: testul urmator porneste curat
  s_activeTest = -1;
  s_activeOk = false;
}

static void startTest(uint8_t index) {
  stopActiveTest();
  s_activeTest = index;
  s_activeOk = TESTS[index].begin();

  if (!s_activeOk) {
    Serial.println();
    Serial.println(F(">> Initializarea testului a esuat. Vezi mesajele de mai sus."));
    Serial.println(F(">> Testul ramane selectat, dar nu ruleaza. Alege altul cu o cifra."));
  }
}

// ---------------------------------------------------------------------
// Comenzile de pairing
// ---------------------------------------------------------------------

// Transforma 16 cifre hexazecimale intr-un DevEUI. Accepta si separatori
// (`-`, `:`, spatiu), ca sa se poata lipi din documentatie.
static bool parseEui(const String& text, uint8_t* eui) {
  uint8_t nibbles[DEV_EUI_LEN * 2];
  uint8_t found = 0;

  for (unsigned int i = 0; i < text.length(); i++) {
    char c = text.charAt(i);
    if (c == '-' || c == ':' || c == ' ' || c == '.') continue;

    uint8_t value;
    if      (c >= '0' && c <= '9') value = (uint8_t)(c - '0');
    else if (c >= 'a' && c <= 'f') value = (uint8_t)(c - 'a' + 10);
    else if (c >= 'A' && c <= 'F') value = (uint8_t)(c - 'A' + 10);
    else return false;

    if (found >= sizeof(nibbles)) return false;   // prea multe cifre
    nibbles[found++] = value;
  }

  if (found != sizeof(nibbles)) return false;     // prea putine cifre

  for (uint8_t i = 0; i < DEV_EUI_LEN; i++) {
    eui[i] = (uint8_t)((nibbles[i * 2] << 4) | nibbles[i * 2 + 1]);
  }
  return true;
}

static void commandPair() {
  if (s_activeTest != PAIRING_TEST_INDEX || !s_activeOk) {
    Serial.println(F("Pornesc testul de pairing..."));
    startTest(PAIRING_TEST_INDEX);
    if (!s_activeOk) return;
  }
  TestPairing::enterPairingMode();
}

static void commandRemove(const String& argument) {
  // Ultimul cuvant poate fi "force".
  String euiText = argument;
  bool force = false;

  int space = euiText.indexOf(' ');
  if (space >= 0) {
    String tail = euiText.substring(space + 1);
    tail.trim();
    euiText = euiText.substring(0, space);
    euiText.trim();
    force = tail.equalsIgnoreCase("force");
  }

  uint8_t eui[DEV_EUI_LEN];
  if (!parseEui(euiText, eui)) {
    Serial.println(F("DevEUI invalid. Se asteapta 16 cifre hexazecimale, ex: 534F4C5649580001"));
    return;
  }

  DeviceRecord* device = DeviceRegistry::findByEui(eui);
  if (device == nullptr) {
    Serial.print(F("Nu exista niciun device inrolat cu DevEUI "));
    SensorPacketCodec::printEui(eui);
    Serial.println();
    return;
  }

  if (force) {
    DeviceRegistry::removeByEui(eui);
    Serial.print(F("Sters imediat din registru: "));
    SensorPacketCodec::printEui(eui);
    Serial.println();
    Serial.println(F("ATENTIE: senzorul nu a fost anuntat. El va continua sa emita cu vechea"));
    Serial.println(F("cheie pana la o stergere manuala a HEF-ului sau pana la un 'remove' normal."));
    return;
  }

  device->pendingReset = true;
  DeviceRegistry::save();

  Serial.print(F("Marcat pentru dezinrolare: "));
  SensorPacketCodec::printEui(eui);
  Serial.println();
  Serial.println(F("La primul lui pachet primeste CMD_DOWN(RESET), apoi dispare din registru."));
  Serial.println(F("Pana atunci, pachetele lui nu mai sunt afisate. Testul 8 trebuie sa ruleze."));
}

// Intoarce true daca linia a fost o comanda cunoscuta.
static bool handleWordCommand(const String& line) {
  String command = line;
  String argument = "";

  int space = command.indexOf(' ');
  if (space >= 0) {
    argument = command.substring(space + 1);
    argument.trim();
    command = command.substring(0, space);
  }
  command.toLowerCase();

  if (command == "pair")        { commandPair();                     return true; }
  if (command == "list")        { DeviceRegistry::printAll();        return true; }
  if (command == "provisioned") { DeviceRegistry::printProvisioned();return true; }
  if (command == "stats")       { TestPairing::printStats();
                                  DeviceRegistry::printAll();        return true; }
  if (command == "help")        { printCommandHelp();                return true; }
  if (command == "remove")      { commandRemove(argument);           return true; }

  return false;
}

static void handleSerialInput() {
  if (!Serial.available()) return;

  String line = Serial.readStringUntil('\n');
  line.trim();
  if (line.length() == 0) return;

  // O singura cifra sau 'm' inseamna meniul; orice altceva este o
  // comanda in cuvinte.
  if (line.length() == 1) {
    char c = line.charAt(0);

    if (c == 'm' || c == 'M') {
      printMenu();
      return;
    }

    if (c >= '0' && c <= '9') {
      uint8_t choice = (uint8_t)(c - '0');

      if (choice == 0) {
        stopActiveTest();
        printMenu();
        return;
      }

      if (choice > TEST_COUNT) {
        Serial.println(F("Nu exista test cu acest numar."));
        return;
      }

      startTest((uint8_t)(choice - 1));
      return;
    }
  }

  if (handleWordCommand(line)) return;

  Serial.println(F("Comanda necunoscuta. Apasa 'm' pentru meniu sau scrie 'help'."));
}

// Butonul 1 deschide fereastra de pairing, ca sa nu fie nevoie de un
// calculator langa hub. Se reactioneaza pe FRONTUL crescator.
static void handleButton() {
  // Doar cand nu ruleaza niciun test sau ruleaza chiar testul de
  // pairing. Altfel o apasare in timpul testului 1 (butoane) ar opri
  // exact testul care le masoara, iar pe o placa fara rezistorul extern
  // de pe GPIO34 zgomotul ar comuta testele de unul singur (F-008).
  if (s_activeTest >= 0 && s_activeTest != PAIRING_TEST_INDEX) {
    s_lastButton1 = digitalRead(PIN_BUTTON_1);
    return;
  }

  int level = digitalRead(PIN_BUTTON_1);

  if (level == HIGH && s_lastButton1 == LOW) {
    Serial.println();
    Serial.println(F("Buton 1 apasat -> deschid fereastra de pairing."));
    commandPair();
  }

  s_lastButton1 = level;
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(1500);

  Serial.println();
  Serial.println(F("Pornire hub..."));

  // LED-urile intai: asa se vede ca placa traieste chiar daca ceva de
  // mai jos esueaza.
  Leds::begin();

  // Magistrala SPI se initializeaza o singura data, aici. Functia duce
  // ambele CS-uri pe HIGH inainte sa existe orice trafic, deci niciun
  // modul nu este selectat la boot.
  SpiBus::begin();
  Serial.println(F("SPI pornit: SCK 18, MISO 19, MOSI 23."));
  Serial.println(F("CS_ETH (4) si NSS_LoRa (5) sunt pe HIGH - bus liber."));

  // GPIO34 este input-only si fara pull intern: rezistorul extern este
  // obligatoriu (F-008).
  pinMode(PIN_BUTTON_1, INPUT);
  s_lastButton1 = digitalRead(PIN_BUTTON_1);

  // Registrul senzorilor inrolati, din NVS. Se incarca inainte de orice
  // test, ca `list` sa functioneze si fara testul 8 pornit.
  if (DeviceRegistry::begin()) {
    Serial.print(F("Registru incarcat: "));
    Serial.print(DeviceRegistry::count());
    Serial.println(F(" senzor(i) inrolati."));
  }

  printMenu();
}

void loop() {
  handleSerialInput();
  handleButton();

  if (s_activeTest >= 0 && s_activeOk) {
    TESTS[s_activeTest].tick();
  } else {
    delay(10);
  }
}
